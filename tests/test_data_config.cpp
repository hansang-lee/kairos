#include <cstdio>
#include <filesystem>
#include <fstream>

#include "common/util.hpp"
#include "data/bar_recorder.hpp"
#include "data/krx_calendar.hpp"
#include "strategy/strategy_factory.hpp"
#include "test_framework.hpp"

namespace {

std::string tmp(const std::string& name) {
    return "/tmp/kairos_test_" + name;
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream(path, std::ios::trunc) << content;
}

}  // namespace

/* ------------------------------ KrxCalendar ------------------------------ */

TEST(calendar, weekends_are_closed_without_any_holiday_file) {
    const data::KrxCalendar c(tmp("no_such_calendar.json"));
    CHECK(!c.loaded());
    CHECK(!c.isTradingDay("2026-09-19"));  // Saturday
    CHECK(!c.isTradingDay("2026-09-20"));  // Sunday
    CHECK_EQ(c.closedReason("2026-09-19"), std::string("weekend"));
}

TEST(calendar, unknown_dates_fail_open_as_trading_days) {
    // A stale list must cost a wasted poll, never a silently skipped trading day.
    const data::KrxCalendar c(tmp("no_such_calendar.json"));
    CHECK(c.isTradingDay("2031-03-12"));  // a Wednesday far outside any list
}

TEST(calendar, listed_holidays_are_closed) {
    const std::string path = tmp("calendar.json");
    writeFile(path, R"({"holidays":{"2026-06-03":"Local elections","2026-07-17":"Constitution Day"}})");

    const data::KrxCalendar c(path);
    CHECK(c.loaded());
    // Both were derived from real KIS bars: weekdays with no trading.
    CHECK(!c.isTradingDay("2026-06-03"));
    CHECK(!c.isTradingDay("2026-07-17"));
    CHECK_EQ(c.closedReason("2026-06-03"), std::string("Local elections"));
    CHECK(c.isTradingDay("2026-06-04"));
}

TEST(calendar, malformed_dates_do_not_crash) {
    const data::KrxCalendar c(tmp("no_such_calendar.json"));
    CHECK(c.isTradingDay(""));
    CHECK(c.isTradingDay("not-a-date"));
    CHECK(c.isTradingDay("2026-13-45"));
}

/* ------------------------------ BarRecorder ------------------------------ */

namespace {

StockInfo bars(int64_t startTs, std::size_t n) {
    StockInfo s;
    s.ticker = "TEST";
    for (std::size_t i = 0; i < n; ++i) {
        s.timestamps.push_back(startTs + static_cast<int64_t>(i) * 60);
        s.open.push_back(100.0 + i);
        s.high.push_back(101.0 + i);
        s.low.push_back(99.0 + i);
        s.close.push_back(100.5 + i);
        s.volume.push_back(1000 + static_cast<int64_t>(i));
    }
    return s;
}

}  // namespace

TEST(recorder, recording_is_idempotent) {
    const std::string root = tmp("bars_idem");
    std::filesystem::remove_all(root);

    data::BarRecorder r(root);
    const auto        b = bars(1789344000, 30);
    CHECK_EQ(r.record("005930", b), 30);
    // Polls overlap heavily; a second pass over the same bars must add nothing.
    CHECK_EQ(r.record("005930", b), 0);
}

TEST(recorder, overlapping_polls_merge_rather_than_duplicate) {
    const std::string root = tmp("bars_merge");
    std::filesystem::remove_all(root);

    data::BarRecorder r(root);
    r.record("005930", bars(1789344000, 30));
    CHECK_EQ(r.record("005930", bars(1789344000 + 20 * 60, 30)), 20);  // 10 overlap, 20 new

    const auto loaded = r.load("005930", "2000-01-01", "2099-01-01");
    CHECK(loaded != nullptr);
    CHECK_EQ(loaded->close.size(), std::size_t{50});
}

TEST(recorder, loaded_bars_are_sorted_and_unique) {
    const std::string root = tmp("bars_sorted");
    std::filesystem::remove_all(root);

    data::BarRecorder r(root);
    r.record("005930", bars(1789344000 + 40 * 60, 20));  // later chunk first
    r.record("005930", bars(1789344000, 20));

    const auto loaded = r.load("005930", "2000-01-01", "2099-01-01");
    CHECK(loaded != nullptr);
    for (std::size_t i = 1; i < loaded->timestamps.size(); ++i) {
        // The backtest engine assumes strictly increasing timestamps.
        CHECK_MSG(loaded->timestamps[i] > loaded->timestamps[i - 1], "timestamps not strictly increasing");
    }
}

TEST(recorder, intervals_do_not_share_storage) {
    // A 5-minute series shares timestamps with every fifth 1-minute bar, so one
    // shared file would silently replace those bars with 5-minute aggregates and
    // leave a mixed-resolution series that still looks valid.
    const std::string root = tmp("bars_intervals");
    std::filesystem::remove_all(root);

    data::BarRecorder r(root);
    r.record("005930", bars(1789344000, 30), "1m");
    // Same timestamps, different resolution.
    r.record("005930", bars(1789344000, 6), "5m");

    const auto oneMin  = r.load("005930", "2000-01-01", "2099-01-01", "1m");
    const auto fiveMin = r.load("005930", "2000-01-01", "2099-01-01", "5m");
    CHECK(oneMin != nullptr);
    CHECK(fiveMin != nullptr);
    CHECK_EQ(oneMin->close.size(), std::size_t{30});
    CHECK_EQ(fiveMin->close.size(), std::size_t{6});
}

TEST(recorder, an_unseen_interval_reads_as_empty) {
    const std::string root = tmp("bars_interval_absent");
    std::filesystem::remove_all(root);

    data::BarRecorder r(root);
    r.record("005930", bars(1789344000, 10), "1m");
    CHECK(r.load("005930", "2000-01-01", "2099-01-01", "1h") == nullptr);
    CHECK(r.storedDates("005930", "1h").empty());
}

TEST(recorder, a_named_series_stays_in_one_file_and_merges) {
    // Daily bars are one per day; the per-date layout would make one file per row.
    const std::string root = tmp("bars_series");
    std::filesystem::remove_all(root);

    data::BarRecorder r(root);
    CHECK_EQ(r.recordSeries("005930", "daily", bars(1789344000, 10)), 10);
    CHECK_EQ(r.recordSeries("005930", "daily", bars(1789344000, 10)), 0);           // idempotent
    CHECK_EQ(r.recordSeries("005930", "daily", bars(1789344000 + 5 * 60, 10)), 5);  // overlap merges

    const auto loaded = r.loadSeries("005930", "daily");
    CHECK(loaded != nullptr);
    CHECK_EQ(loaded->close.size(), std::size_t{15});
    for (std::size_t i = 1; i < loaded->timestamps.size(); ++i) {
        CHECK(loaded->timestamps[i] > loaded->timestamps[i - 1]);
    }
}

TEST(recorder, a_named_series_is_not_mistaken_for_a_date) {
    // storedDates() feeds a range query; a file called "daily.csv" compared as a
    // date would silently fall inside or outside any range asked for.
    const std::string root = tmp("bars_series_dates");
    std::filesystem::remove_all(root);

    data::BarRecorder r(root);
    r.record("005930", bars(1789344000, 10));  // per-day file
    r.recordSeries("005930", "daily", bars(1789344000, 10));

    const auto dates = r.storedDates("005930");
    for (const auto& d : dates) {
        CHECK_MSG(d != "daily", "storedDates returned a named series as a date");
    }
    CHECK_EQ(dates.size(), std::size_t{1});

    // The per-day load must not pick up the series file either.
    const auto ranged = r.load("005930", "2000-01-01", "2099-01-01");
    CHECK(ranged != nullptr);
    CHECK_EQ(ranged->close.size(), std::size_t{10});
}

TEST(recorder, loading_an_absent_series_returns_null) {
    const std::string root = tmp("bars_series_absent");
    std::filesystem::remove_all(root);
    const data::BarRecorder r(root);
    CHECK(r.loadSeries("005930", "daily") == nullptr);
}

TEST(recorder, loading_an_empty_archive_returns_null) {
    const std::string root = tmp("bars_empty");
    std::filesystem::remove_all(root);
    const data::BarRecorder r(root);
    CHECK(r.load("005930", "2000-01-01", "2099-01-01") == nullptr);
    CHECK(r.storedDates("005930").empty());
}

TEST(recorder, recording_nothing_is_not_an_error) {
    const std::string root = tmp("bars_none");
    std::filesystem::remove_all(root);
    data::BarRecorder r(root);
    CHECK_EQ(r.record("005930", StockInfo{}), 0);
}

/* --------------------------- Portfolio config --------------------------- */

TEST(config, profile_fields_are_parsed) {
    const std::string path = tmp("portfolio.json");
    writeFile(path, R"({
      "initial_capital_krw": 5000000,
      "risk": { "daily_loss_limit_pct": 2.5, "max_orders_per_day": 7 },
      "strategies": [{
        "id": 1, "name": "T", "ticker": "005930", "market": "KRX",
        "type": "bollinger", "category": "swing",
        "params": { "period": 40, "std_devs": 2.0 },
        "position_pct": 0.3, "stop_loss_pct": 8.0,
        "take_profit_pct": 12.0, "trailing_stop_pct": 5.0,
        "cooldown_minutes": 30, "entry_tranches": 2, "exit_tranches": 3,
        "trade_window": { "start": "0930", "end": "1520" },
        "enabled": false
      }]
    })");

    const auto cfg = PortfolioConfig::loadFromFile(path);
    CHECK_NEAR(cfg.getInitialCapitalKrw(), 5000000.0, 1e-6);
    CHECK_NEAR(cfg.getRiskLimits().dailyLossLimitPct, 2.5, 1e-9);
    CHECK_EQ(cfg.getRiskLimits().maxOrdersPerDay, 7);

    const auto* p = cfg.findById(1);
    CHECK(p != nullptr);
    CHECK_NEAR(p->positionPct, 0.3, 1e-9);
    CHECK_NEAR(p->stopLossPct, 8.0, 1e-9);
    CHECK_NEAR(p->takeProfitPct, 12.0, 1e-9);
    CHECK_NEAR(p->trailingStopPct, 5.0, 1e-9);
    CHECK_EQ(p->cooldownMinutes, 30);
    CHECK_EQ(p->entryTranches, 2);
    CHECK_EQ(p->exitTranches, 3);
    CHECK_EQ(p->tradeStart, std::string("0930"));
    CHECK_EQ(p->enabled, false);
}

TEST(config, defaults_apply_when_fields_are_absent) {
    const std::string path = tmp("portfolio_min.json");
    writeFile(path, R"({"strategies":[{"id":1,"ticker":"005930","type":"rsi"}]})");

    const auto  cfg = PortfolioConfig::loadFromFile(path);
    const auto* p   = cfg.findById(1);
    CHECK(p != nullptr);
    CHECK_EQ(p->enabled, true);  // profiles trade unless disabled
    CHECK_EQ(p->entryTranches, 1);
    CHECK_EQ(p->exitTranches, 1);
    CHECK_NEAR(p->stopLossPct, 0.0, 1e-9);
    CHECK_EQ(p->market, std::string("KRX"));
}

TEST(config, tranche_counts_below_one_are_clamped) {
    const std::string path = tmp("portfolio_tranche.json");
    writeFile(path, R"({"strategies":[{"id":1,"ticker":"005930","type":"rsi",
                        "entry_tranches":0,"exit_tranches":-5}]})");
    const auto  cfg = PortfolioConfig::loadFromFile(path);
    const auto* p   = cfg.findById(1);
    // Zero tranches would mean a position that can never be entered or exited.
    CHECK_EQ(p->entryTranches, 1);
    CHECK_EQ(p->exitTranches, 1);
}

TEST(config, find_by_ticker_prefers_the_enabled_profile) {
    // Several profiles share a ticker once retired ones are kept for backtesting.
    // Returning the first in file order credited a live holding to a strategy that
    // was not trading, which the dashboard then displayed as fact.
    const std::string path = tmp("portfolio_dupe.json");
    writeFile(path, R"({"strategies":[
      {"id":1,"ticker":"005930","type":"rsi","enabled":false},
      {"id":18,"ticker":"005930","type":"sma_crossover","enabled":false},
      {"id":30,"ticker":"005930","type":"bollinger","enabled":true}
    ]})");

    const auto  cfg = PortfolioConfig::loadFromFile(path);
    const auto* p   = cfg.findByTicker("005930");
    CHECK(p != nullptr);
    CHECK_EQ(p->id, 30);
}

TEST(config, find_by_ticker_falls_back_when_nothing_is_enabled) {
    const std::string path = tmp("portfolio_alldisabled.json");
    writeFile(path, R"({"strategies":[
      {"id":1,"ticker":"005930","type":"rsi","enabled":false},
      {"id":2,"ticker":"005930","type":"macd","enabled":false}
    ]})");

    const auto  cfg = PortfolioConfig::loadFromFile(path);
    const auto* p   = cfg.findByTicker("005930");
    // Naming a retired strategy beats naming none: the holding came from somewhere.
    CHECK(p != nullptr);
    CHECK_EQ(p->id, 1);
    CHECK(cfg.findByTicker("999999") == nullptr);
}

TEST(config, a_missing_file_yields_an_empty_config) {
    const auto cfg = PortfolioConfig::loadFromFile(tmp("no_such_portfolio.json"));
    CHECK(cfg.getProfiles().empty());
}

TEST(config, an_unknown_strategy_type_returns_null) {
    StrategyProfile p;
    p.type = "not_a_real_strategy";
    CHECK(p.createStrategy() == nullptr);
}

TEST(config, bollinger_accepts_both_spellings_of_the_width) {
    // "std_dev" is what the original config shipped; "std_devs" is what the docs
    // and every other band strategy use. Reading only one silently ignored the other.
    StrategyProfile a;
    a.type   = "bollinger";
    a.params = {{"period", 20}, {"std_dev", 3.0}};
    CHECK(a.createStrategy() != nullptr);

    StrategyProfile b;
    b.type   = "bollinger";
    b.params = {{"period", 20}, {"std_devs", 3.0}};
    CHECK(b.createStrategy() != nullptr);
    CHECK_EQ(a.createStrategy()->name(), b.createStrategy()->name());
}

/* ---------------------------------- util ---------------------------------- */

TEST(util, env_value_reads_a_file_and_falls_back_to_the_environment) {
    const std::string path = tmp("env_file");
    writeFile(path, "# comment\nFOO = bar \nEMPTY=\n");

    CHECK_EQ(util::envValue("FOO", path), std::string("bar"));
    CHECK_EQ(util::envValue("MISSING", path), std::string(""));
    // A key present but blank must fall through, not mask the environment.
    setenv("KAIROS_TEST_EMPTY", "from-env", 1);
    CHECK_EQ(util::envValue("KAIROS_TEST_EMPTY", path), std::string("from-env"));
    unsetenv("KAIROS_TEST_EMPTY");
}

TEST(util, load_json_config_reports_missing_and_malformed_files) {
    CHECK(!util::loadJsonConfig(tmp("definitely_absent.json")).has_value());

    const std::string bad = tmp("bad.json");
    writeFile(bad, "{ this is not json");
    CHECK(!util::loadJsonConfig(bad).has_value());
}

TEST(config, dangerous_percentages_are_clamped_at_load) {
    // A negative stop loss inverts its own comparison: price <= avg * (1 - (-5)/100)
    // is price <= avg * 1.05, so the position sells the moment it opens — and
    // nothing downstream would report that as anything but a working stop.
    const std::string path = tmp("portfolio_dangerous.json");
    writeFile(path, R"({"strategies":[{
      "id":1,"ticker":"005930","type":"bollinger",
      "position_pct": 5.0,
      "stop_loss_pct": -5.0,
      "take_profit_pct": -1.0,
      "trailing_stop_pct": -2.0,
      "cooldown_minutes": -30
    }]})");

    const auto  cfg = PortfolioConfig::loadFromFile(path);
    const auto* p   = cfg.findById(1);
    CHECK(p != nullptr);
    CHECK_NEAR(p->positionPct, 1.0, 1e-9);  // 500% of cash is not a position
    CHECK_NEAR(p->stopLossPct, 0.0, 1e-9);  // disabled beats inverted
    CHECK_NEAR(p->takeProfitPct, 0.0, 1e-9);
    CHECK_NEAR(p->trailingStopPct, 0.0, 1e-9);
    CHECK_EQ(p->cooldownMinutes, 0);
}

TEST(config, valid_percentages_pass_through_untouched) {
    const std::string path = tmp("portfolio_valid.json");
    writeFile(path, R"({"strategies":[{
      "id":1,"ticker":"005930","type":"bollinger",
      "position_pct": 0.2, "stop_loss_pct": 8.0,
      "take_profit_pct": 12.0, "trailing_stop_pct": 5.0, "cooldown_minutes": 30
    }]})");

    const auto  cfg = PortfolioConfig::loadFromFile(path);
    const auto* p   = cfg.findById(1);
    CHECK_NEAR(p->positionPct, 0.2, 1e-9);
    CHECK_NEAR(p->stopLossPct, 8.0, 1e-9);
    CHECK_NEAR(p->takeProfitPct, 12.0, 1e-9);
    CHECK_NEAR(p->trailingStopPct, 5.0, 1e-9);
    CHECK_EQ(p->cooldownMinutes, 30);
}
