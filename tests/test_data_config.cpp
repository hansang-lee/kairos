#include <cstdio>
#include <filesystem>
#include <fstream>

#include "common/kst_time.hpp"
#include "common/util.hpp"
#include "data/bar_recorder.hpp"
#include "data/krx_calendar.hpp"
#include "strategy/strategy_catalog.hpp"
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
    // clang-format off
    writeFile(path, R"({"holidays":{"2026-06-03":"Local elections","2026-07-17":"Constitution Day"}})");
    // clang-format on

    const data::KrxCalendar c(path);
    CHECK(c.loaded());
    // Both were derived from real KIS bars: weekdays with no trading.
    CHECK(!c.isTradingDay("2026-06-03"));
    CHECK(!c.isTradingDay("2026-07-17"));
    CHECK_EQ(c.closedReason("2026-06-03"), std::string("Local elections"));
    CHECK(c.isTradingDay("2026-06-04"));
}

TEST(calendar, a_holiday_that_actually_traded_is_reported) {
    const std::string path = tmp("calendar_audit_false_holiday.json");
    // clang-format off
    // Delimited raw string: the value contains `)"`, which would end a plain R"(...)".
    writeFile(path, R"json({"holidays":{"2026-09-24":"Chuseok holiday (projected)"}})json");
    // clang-format on
    const data::KrxCalendar c(path);

    // The exchange published a bar on the "holiday": the projection was wrong and
    // the trader will have skipped that day's signals with no error.
    const auto issues = c.audit({"2026-09-23", "2026-09-24", "2026-09-25"}, "2026-09-23", "2026-09-25");
    CHECK_EQ(issues.size(), std::size_t{1});
    CHECK_EQ(issues[0].date, std::string("2026-09-24"));
    CHECK(issues[0].expected.find("closed") == 0);
    CHECK_EQ(issues[0].observed, std::string("a bar was published"));
}

TEST(calendar, an_open_day_with_no_bar_is_reported) {
    const data::KrxCalendar c(tmp("no_such_calendar.json"));
    // Wednesday 2026-09-23 has no bar in what was fetched: either a holiday the
    // list does not know about, or a data problem. Either way someone should look.
    const auto issues = c.audit({"2026-09-22", "2026-09-24"}, "2026-09-22", "2026-09-24");
    CHECK_EQ(issues.size(), std::size_t{1});
    CHECK_EQ(issues[0].date, std::string("2026-09-23"));
    CHECK_EQ(issues[0].expected, std::string("open"));
    CHECK_EQ(issues[0].observed, std::string("no bar"));
}

TEST(calendar, a_correct_calendar_produces_no_disagreements) {
    const std::string path = tmp("calendar_audit_ok.json");
    // clang-format off
    writeFile(path, R"({"holidays":{"2026-09-24":"Chuseok","2026-09-25":"Chuseok"}})");
    // clang-format on
    const data::KrxCalendar c(path);

    // Mon-Wed traded, Thu-Fri were the holiday, Sat-Sun the weekend. Every day
    // agrees, including the weekend, which has no bar and is not expected to.
    const auto issues = c.audit({"2026-09-21", "2026-09-22", "2026-09-23"}, "2026-09-21", "2026-09-27");
    CHECK(issues.empty());
}

TEST(calendar, the_audit_range_is_inclusive_at_both_ends) {
    const data::KrxCalendar c(tmp("no_such_calendar.json"));
    // Nothing traded on either end day, both weekdays: both must be reported.
    const auto issues = c.audit({}, "2026-09-22", "2026-09-23");
    CHECK_EQ(issues.size(), std::size_t{2});
    CHECK_EQ(issues[0].date, std::string("2026-09-22"));
    CHECK_EQ(issues[1].date, std::string("2026-09-23"));
}

TEST(calendar, a_malformed_audit_range_returns_nothing_rather_than_spinning) {
    const data::KrxCalendar c(tmp("no_such_calendar.json"));
    // Sorts before a real September date, so only an explicit check catches it.
    CHECK(c.audit({"2026-09-22"}, "2026-13-01", "2026-09-23").empty());
    CHECK(c.audit({"2026-09-22"}, "not-a-date", "2026-09-23").empty());
    CHECK(c.audit({"2026-09-22"}, "2026-09-23", "2026-09-22").empty());  // from > to
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
    // clang-format off
    writeFile(path, R"({"initial_capital_krw": 5000000,"risk": { "daily_loss_limit_pct": 2.5, "max_orders_per_day": 7 },"strategies": [{"id": 1, "name": "T", "ticker": "005930", "market": "KRX","type": "bollinger", "category": "swing","params": { "period": 40, "std_devs": 2.0 },"position_pct": 0.3, "stop_loss_pct": 8.0,"take_profit_pct": 12.0, "trailing_stop_pct": 5.0,"cooldown_minutes": 30, "entry_tranches": 2, "exit_tranches": 3,"trade_window": { "start": "0930", "end": "1520" },"enabled": false}]})");
    // clang-format on

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
    // clang-format off
    writeFile(path, R"({"strategies":[{"id":1,"ticker":"005930","type":"rsi"}]})");
    // clang-format on

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
    // clang-format off
    writeFile(path, R"({"strategies":[{"id":1,"ticker":"005930","type":"rsi","entry_tranches":0,"exit_tranches":-5}]})");
    // clang-format on
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
    // clang-format off
    writeFile(path, R"({"strategies":[{"id":1,"ticker":"005930","type":"rsi","enabled":false},{"id":18,"ticker":"005930","type":"sma_crossover","enabled":false},{"id":30,"ticker":"005930","type":"bollinger","enabled":true}]})");
    // clang-format on

    const auto  cfg = PortfolioConfig::loadFromFile(path);
    const auto* p   = cfg.findByTicker("005930");
    CHECK(p != nullptr);
    CHECK_EQ(p->id, 30);
}

TEST(config, find_by_ticker_falls_back_when_nothing_is_enabled) {
    const std::string path = tmp("portfolio_alldisabled.json");
    // clang-format off
    writeFile(path, R"({"strategies":[{"id":1,"ticker":"005930","type":"rsi","enabled":false},{"id":2,"ticker":"005930","type":"macd","enabled":false}]})");
    // clang-format on

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

TEST(config, the_param_reader_reports_exactly_the_keys_nothing_read) {
    const nlohmann::json p = {{"period", 14}, {"oversold", 30.0}, {"overbougth", 70.0}};
    const ParamReader    pr(p);
    CHECK_EQ(pr.value("period", 0), 14);
    CHECK_NEAR(pr.value("oversold", 0.0), 30.0, 1e-9);
    CHECK_NEAR(pr.value("overbought", 70.0), 70.0, 1e-9);  // absent: the fallback, and a typo left behind
    const auto unused = pr.unusedKeys();
    CHECK_EQ(unused.size(), std::size_t{1});
    CHECK_EQ(unused[0], std::string("overbougth"));
}

TEST(config, every_strategy_type_reports_a_misspelled_parameter) {
    // The old guard was a hand-kept list per type and covered three of twenty-four.
    // This is the property that mattered: no type, present or future, silently
    // swallows a key it never read.
    const std::vector<std::string> types = {"rsi",
                                            "macd",
                                            "bollinger",
                                            "sma_crossover",
                                            "stochastic_reversal",
                                            "williams_r",
                                            "cci_reversal",
                                            "mfi_reversal",
                                            "adx_trend",
                                            "supertrend",
                                            "aroon_trend",
                                            "psar_trend",
                                            "donchian_breakout",
                                            "obv_trend",
                                            "keltner_breakout",
                                            "ma_slope_trend",
                                            "regime_rsi",
                                            "volume_breakout",
                                            "squeeze_breakout",
                                            "ichimoku_trend",
                                            "ma_timing",
                                            "absolute_momentum",
                                            "dual_momentum",
                                            "relative_momentum",
                                            "vol_target"};
    for (const auto& type : types) {
        StrategyProfile p;
        p.type   = type;
        p.params = {{"definitely_not_a_parameter", 1}};
        std::vector<std::string> unused;
        const auto               strat = p.createStrategy(&unused);
        CHECK_MSG(strat != nullptr, type + " failed to construct");
        CHECK_MSG(unused.size() == 1 && unused[0] == "definitely_not_a_parameter", type + " did not report the typo");
    }
}

TEST(config, a_fully_used_parameter_set_reports_nothing) {
    StrategyProfile p;
    p.type                          = "rsi";
    p.params                        = {{"period", 14}, {"oversold", 30.0}, {"overbought", 70.0}};
    std::vector<std::string> unused = {"stale"};
    CHECK(p.createStrategy(&unused) != nullptr);
    CHECK(unused.empty());
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

TEST(util, resolve_from_exe_finds_the_project_root_by_what_it_contains) {
    // This binary sits at build/Release/kairos_tests — three levels down, where a
    // fixed four-level walk used to land one directory too high.
    const std::string root = util::resolveFromExe("");
    CHECK(std::filesystem::exists(root + "/CMakeLists.txt"));
    CHECK(std::filesystem::is_directory(root + "/config"));
    CHECK(std::filesystem::exists(util::resolveFromExe("config/live.json")));
    CHECK(root.find("/build") == std::string::npos);
}

TEST(util, kst_dates_are_utc_plus_nine_and_cross_midnight_where_seoul_does) {
    // Epoch zero is 1970-01-01 00:00 UTC, which is 09:00 the same day in Seoul.
    CHECK_EQ(util::kstDateOf(0), std::string("1970-01-01"));
    CHECK_EQ(util::kstTimestamp(0), std::string("1970-01-01 09:00:00"));
    // 15:00 UTC is midnight in Seoul: the KST date has already rolled over while
    // the UTC one has not. This is the case a naive local-time read gets wrong.
    CHECK_EQ(util::kstDateOf(15 * 3600), std::string("1970-01-02"));
    CHECK_EQ(util::kstDateOf(15 * 3600 - 1), std::string("1970-01-01"));
    // A KRX daily bar is stamped 09:00 UTC on its own date; that must read back as
    // that date, not the next one.
    CHECK_EQ(util::kstDateOf(1420189200), std::string("2015-01-02"));
}

TEST(util, kst_today_and_days_ago_agree_with_each_other) {
    CHECK_EQ(util::kstToday(), util::kstDate(0));
    CHECK(util::kstDate(1) < util::kstDate(0));
    CHECK_EQ(util::kstDate(0).size(), std::size_t{10});
    const auto hhmm = util::kstNow().second;
    CHECK(hhmm >= 0 && hhmm <= 2359);
}

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
    // clang-format off
    writeFile(path, R"({"strategies":[{"id":1,"ticker":"005930","type":"bollinger","position_pct": 5.0,"stop_loss_pct": -5.0,"take_profit_pct": -1.0,"trailing_stop_pct": -2.0,"cooldown_minutes": -30}]})");
    // clang-format on

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
    // clang-format off
    writeFile(path, R"({"strategies":[{"id":1,"ticker":"005930","type":"bollinger","position_pct": 0.2, "stop_loss_pct": 8.0,"take_profit_pct": 12.0, "trailing_stop_pct": 5.0, "cooldown_minutes": 30}]})");
    // clang-format on

    const auto  cfg = PortfolioConfig::loadFromFile(path);
    const auto* p   = cfg.findById(1);
    CHECK_NEAR(p->positionPct, 0.2, 1e-9);
    CHECK_NEAR(p->stopLossPct, 8.0, 1e-9);
    CHECK_NEAR(p->takeProfitPct, 12.0, 1e-9);
    CHECK_NEAR(p->trailingStopPct, 5.0, 1e-9);
    CHECK_EQ(p->cooldownMinutes, 30);
}

/* ---------------------------- StrategyCatalog ---------------------------- */

TEST(catalog, concrete_strategies_load_with_their_parameters) {
    const std::string path = tmp("catalog.json");
    // clang-format off
    writeFile(path, R"({"strategies":[{"id":"bb40","name":"Bollinger 40","type":"bollinger","category":"swing","params":{"period":40,"std_devs":2.0}},{"id":"slope","type":"ma_slope_trend","params":{"ma_period":20,"slope_window":10}}]})");
    // clang-format on

    const auto cat = StrategyCatalog::loadFromFile(path);
    CHECK(cat.loaded());
    CHECK_EQ(cat.concrete().size(), std::size_t{2});

    const auto* bb = cat.find("bb40");
    CHECK(bb != nullptr);
    CHECK_EQ(bb->type, std::string("bollinger"));
    CHECK_EQ(bb->category, std::string("swing"));
    CHECK_EQ(bb->params.value("period", 0), 40);
    CHECK(cat.find("nope") == nullptr);
}

TEST(catalog, a_grid_expands_to_every_combination) {
    const std::string path = tmp("catalog_grid.json");
    // clang-format off
    writeFile(path, R"({"grids":[{"type":"bollinger","category":"swing","params":{"period":[14,20,40],"std_devs":[1.5,2.0]}}]})");
    // clang-format on

    const auto cat  = StrategyCatalog::loadFromFile(path);
    const auto grid = cat.gridFor("bollinger");
    CHECK_EQ(grid.size(), std::size_t{6});  // 3 x 2
    CHECK(cat.concrete().empty());          // a grid produces no concrete entries

    // Ids must be stable, or the same combination is named differently next run and
    // two sweep reports cannot be compared.
    CHECK(cat.find("bollinger(14,1.5)") != nullptr);
    CHECK(cat.find("bollinger(40,2.0)") != nullptr);
    CHECK(cat.gridFor("rsi").empty());
}

TEST(catalog, entries_without_an_id_or_type_are_skipped) {
    const std::string path = tmp("catalog_bad.json");
    // clang-format off
    writeFile(path, R"({"strategies":[{"name":"no id","type":"rsi"},{"id":"no-type"},{"id":"fine","type":"rsi"}]})");
    // clang-format on

    const auto cat = StrategyCatalog::loadFromFile(path);
    CHECK_EQ(cat.concrete().size(), std::size_t{1});
    CHECK(cat.find("fine") != nullptr);
}

TEST(catalog, a_missing_file_loads_nothing_rather_than_failing) {
    const auto cat = StrategyCatalog::loadFromFile(tmp("catalog_absent.json"));
    CHECK(!cat.loaded());
    CHECK(cat.all().empty());
}

/* ----------------------- live positions + catalog ----------------------- */

TEST(config, a_position_resolves_its_strategy_from_the_catalog) {
    const std::string cat = tmp("resolve_catalog.json");
    writeFile(cat, R"({"strategies":[
      {"id":"bb40","name":"Bollinger 40","type":"bollinger","category":"swing",
       "params":{"period":40,"std_devs":2.5}}
    ]})");

    const std::string live = tmp("resolve_live.json");
    writeFile(live, R"({"positions":[
      {"id":30,"strategy":"bb40","ticker":"005930","position_pct":0.2,"stop_loss_pct":8.0}
    ]})");

    const auto  cfg = PortfolioConfig::loadFromFile(live, cat);
    const auto* p   = cfg.findById(30);
    CHECK(p != nullptr);
    // The whole point: the trader uses the catalog's parameters, not its own copy.
    CHECK_EQ(p->type, std::string("bollinger"));
    CHECK_EQ(p->category, std::string("swing"));
    CHECK_EQ(p->params.value("period", 0), 40);
    CHECK_NEAR(p->params.value("std_devs", 0.0), 2.5, 1e-9);
    CHECK_EQ(p->ticker, std::string("005930"));
    CHECK(p->createStrategy() != nullptr);
}

TEST(config, an_unresolved_strategy_reference_is_refused_not_defaulted) {
    // A mistyped id quietly falling back to some other strategy would only be
    // noticed from the trades it produced.
    const std::string cat = tmp("refuse_catalog.json");
    writeFile(cat, R"({"strategies":[{"id":"bb40","type":"bollinger","params":{"period":40}}]})");

    const std::string live = tmp("refuse_live.json");
    writeFile(live, R"({"positions":[
      {"id":30,"strategy":"bb4O","ticker":"005930"},
      {"id":31,"strategy":"bb40","ticker":"000660"}
    ]})");

    const auto cfg = PortfolioConfig::loadFromFile(live, cat);
    CHECK(cfg.findById(30) == nullptr);  // the typo does not trade
    CHECK(cfg.findById(31) != nullptr);  // the valid one still does
    CHECK_EQ(cfg.unresolvedStrategies().size(), std::size_t{1});
    CHECK_EQ(cfg.unresolvedStrategies()[0], std::string("bb4O"));
}

TEST(config, an_inline_type_still_works_without_a_catalog) {
    const std::string live = tmp("inline_live.json");
    writeFile(live, R"({"positions":[
      {"id":1,"ticker":"005930","type":"rsi","params":{"period":21}}
    ]})");

    const auto  cfg = PortfolioConfig::loadFromFile(live, tmp("no_such_catalog.json"));
    const auto* p   = cfg.findById(1);
    CHECK(p != nullptr);
    CHECK_EQ(p->type, std::string("rsi"));
    CHECK_EQ(p->params.value("period", 0), 21);
}

TEST(config, a_position_can_override_the_catalog_parameters) {
    const std::string cat = tmp("override_catalog.json");
    writeFile(cat, R"({"strategies":[{"id":"bb","type":"bollinger","params":{"period":20,"std_devs":2.0}}]})");

    const std::string live = tmp("override_live.json");
    writeFile(live, R"({"positions":[
      {"id":1,"strategy":"bb","ticker":"005930","params":{"period":60,"std_devs":2.0}}
    ]})");

    const auto  cfg = PortfolioConfig::loadFromFile(live, cat);
    const auto* p   = cfg.findById(1);
    // One position can be adjusted without forking the shared definition.
    CHECK_EQ(p->params.value("period", 0), 60);
    CHECK_EQ(p->type, std::string("bollinger"));
}
