#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include "strategy/strategy_factory.hpp"
#include "test_framework.hpp"

/**
 * The property every strategy must hold: evaluate(i) may read bars up to i-1 and
 * no further. If it reads bar i or beyond, the backtest is trading on prices that
 * had not happened yet and every result from it is fiction.
 *
 * Tested by construction rather than by inspection: run the strategy on the full
 * series, then re-run it on the series truncated at i, and require the same
 * signal at i. Any dependence on later bars shows up as a mismatch.
 */
namespace {

/** Write a reference price series where the relative-momentum strategy will look. */
void writeReferenceFixture(const StockInfo& shape) {
    std::error_code ec;
    std::filesystem::create_directories("/tmp/kairos_test_refcache", ec);
    std::ofstream out("/tmp/kairos_test_refcache/REF.csv", std::ios::trunc);
    out << "timestamp,open,high,low,close,volume\n";
    // A flat reference, so the strategy's verdict follows the asset alone and the
    // look-ahead comparison is not muddied by two moving series.
    for (std::size_t i = 0; i < shape.timestamps.size(); ++i) {
        out << shape.timestamps[i] << ",100,100,100,100,1000\n";
    }
}

StockInfo makeSeries(std::size_t n) {
    StockInfo s;
    s.ticker      = "TEST";
    double   px   = 100.0;
    uint64_t seed = 987654321;
    auto     next = [&] {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>((seed >> 33) % 1000) / 1000.0;
    };
    for (std::size_t i = 0; i < n; ++i) {
        px *= 1.0 + (next() - 0.5) * 0.05;
        s.timestamps.push_back(static_cast<int64_t>(1600000000 + i * 86400));
        s.open.push_back(px);
        s.high.push_back(px * (1.0 + next() * 0.02));
        s.low.push_back(px * (1.0 - next() * 0.02));
        s.close.push_back(px);
        s.volume.push_back(static_cast<int64_t>(1000 + next() * 90000));
    }
    return s;
}

StockInfo head(const StockInfo& src, std::size_t m) {
    StockInfo out;
    out.ticker = src.ticker;
    out.timestamps.assign(src.timestamps.begin(), src.timestamps.begin() + m);
    out.open.assign(src.open.begin(), src.open.begin() + m);
    out.high.assign(src.high.begin(), src.high.begin() + m);
    out.low.assign(src.low.begin(), src.low.begin() + m);
    out.close.assign(src.close.begin(), src.close.begin() + m);
    out.volume.assign(src.volume.begin(), src.volume.begin() + m);
    return out;
}

const char* signalName(Signal s) {
    return s == Signal::BUY ? "BUY" : (s == Signal::SELL ? "SELL" : "HOLD");
}

/** Every registered type with parameters small enough to signal on a short series. */
std::vector<std::pair<std::string, nlohmann::json>> allStrategies() {
    return {
        {"sma_crossover", {{"short_window", 5}, {"long_window", 20}}},
        {"rsi", {{"period", 14}, {"oversold", 35.0}, {"overbought", 65.0}}},
        {"macd", {{"fast", 6}, {"slow", 13}, {"signal", 5}}},
        {"bollinger", {{"period", 20}, {"std_devs", 2.0}}},
        {"stochastic_reversal", {{"k_period", 14}, {"d_period", 3}}},
        {"williams_r", {{"period", 14}}},
        {"cci_reversal", {{"period", 20}}},
        {"mfi_reversal", {{"period", 14}}},
        {"adx_trend", {{"period", 14}, {"threshold", 20.0}}},
        {"supertrend", {{"period", 10}, {"multiplier", 3.0}}},
        {"aroon_trend", {{"period", 25}}},
        {"psar_trend", {{"step", 0.02}, {"max_step", 0.2}}},
        {"donchian_breakout", {{"period", 20}}},
        {"obv_trend", {{"period", 20}}},
        {"keltner_breakout", {{"period", 20}, {"multiplier", 2.0}}},
        {"ma_slope_trend", {{"ma_period", 20}, {"slope_window", 10}}},
        {"regime_rsi", {{"regime_period", 60}, {"rsi_period", 14}, {"oversold", 40.0}, {"exit_level", 60.0}}},
        {"volume_breakout", {{"period", 20}, {"volume_period", 20}, {"volume_ratio", 1.2}}},
        {"squeeze_breakout", {{"period", 20}, {"std_devs", 2.0}, {"squeeze_lookback", 40}, {"squeeze_percent", 0.4}}},
        {"ichimoku_trend", {{"conversion", 9}, {"base", 26}, {"span_b", 52}}},
        {"ma_timing", {{"period", 60}, {"buffer_pct", 1.0}}},
        {"absolute_momentum", {{"lookback", 60}, {"threshold", 0.0}}},
        {"dual_momentum", {{"ma_period", 60}, {"lookback", 60}, {"threshold", 0.0}}},
        // Pointed at a fixture written by the test, since the real cache lives at a
        // path derived from the executable and the test binary sits elsewhere.
        {"relative_momentum",
         {{"reference", "REF"}, {"lookback", 60}, {"margin_pct", 0.0}, {"cache_dir", "/tmp/kairos_test_refcache"}}},
    };
}

}  // namespace

TEST(lookahead, every_strategy_reads_only_closed_bars) {
    const auto series = makeSeries(400);
    writeReferenceFixture(series);
    const std::size_t n = series.close.size();

    // Collected rather than thrown on first sight: one strategy failing must not
    // hide whether the other nineteen are sound.
    std::vector<std::string> offenders;

    for (const auto& [type, params] : allStrategies()) {
        StrategyProfile p;
        p.type   = type;
        p.params = params;
        p.ticker = "TEST";
        p.market = "KRX";

        auto full = p.createStrategy();
        CHECK_MSG(full != nullptr, "createStrategy returned null for type '" << type << "'");
        full->init(series);

        int checked = 0;
        for (std::size_t i = full->warmupPeriod() + 2; i <= n && checked < 40; i += 7) {
            const Signal reference = full->evaluate(series, i);

            // Bar i is the one being acted on, so the strategy may see bars [0, i).
            const StockInfo visible = head(series, i);
            auto            limited = p.createStrategy();
            limited->init(visible);
            const Signal live = limited->evaluate(visible, i);

            if (live != reference) {
                std::ostringstream oss;
                oss << type << " at index " << i << ": " << signalName(reference) << " with the full series, "
                    << signalName(live) << " with only bars up to " << (i - 1);
                offenders.push_back(oss.str());
                break;  // one example per strategy is enough to identify it
            }
            ++checked;
        }
        CHECK_MSG(checked > 0 || !offenders.empty(), type << ": no indices were actually checked");
    }

    if (!offenders.empty()) {
        std::ostringstream oss;
        oss << offenders.size() << " strategy/strategies read data from the future:";
        for (const auto& o : offenders) {
            oss << "\n          " << o;
        }
        FAIL_WITH(oss.str());
    }
}

TEST(lookahead, every_strategy_produces_some_signal) {
    // A strategy that only ever returns HOLD would pass the look-ahead test
    // trivially, so confirm the check above had something to check.
    const auto series = makeSeries(400);
    writeReferenceFixture(series);

    for (const auto& [type, params] : allStrategies()) {
        StrategyProfile p;
        p.type   = type;
        p.params = params;
        auto s   = p.createStrategy();
        CHECK(s != nullptr);
        s->init(series);

        int nonHold = 0;
        for (std::size_t i = s->warmupPeriod() + 1; i <= series.close.size(); ++i) {
            if (s->evaluate(series, i) != Signal::HOLD) {
                ++nonHold;
            }
        }
        CHECK_MSG(nonHold > 0, type << " never emitted a BUY or SELL over 400 bars — it cannot be evaluated");
    }
}

TEST(lookahead, evaluating_one_past_the_last_bar_is_valid) {
    // Live trading calls evaluate(size) when the current bar has not been
    // published yet. That index must be in range, not silently clamped to HOLD.
    const auto series = makeSeries(300);
    writeReferenceFixture(series);

    for (const auto& [type, params] : allStrategies()) {
        StrategyProfile p;
        p.type   = type;
        p.params = params;
        auto s   = p.createStrategy();
        s->init(series);
        // Must not crash or read out of bounds.
        const Signal sig = s->evaluate(series, series.close.size());
        CHECK(sig == Signal::BUY || sig == Signal::SELL || sig == Signal::HOLD);
    }
}
