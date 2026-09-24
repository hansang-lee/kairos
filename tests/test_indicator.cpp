#include <numeric>

#include "indicator.hpp"
#include "test_framework.hpp"

namespace {

/** Deterministic OHLCV, so a failure is reproducible rather than seed-dependent. */
struct Series {
    std::vector<double>  high, low, close;
    std::vector<int64_t> volume;
};

Series makeSeries(std::size_t n) {
    Series   s;
    double   px   = 100.0;
    uint64_t seed = 12345;
    auto     next = [&] {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>((seed >> 33) % 1000) / 1000.0;
    };
    for (std::size_t i = 0; i < n; ++i) {
        px *= 1.0 + (next() - 0.5) * 0.04;
        s.high.push_back(px * (1.0 + next() * 0.02));
        s.low.push_back(px * (1.0 - next() * 0.02));
        s.close.push_back(px);
        s.volume.push_back(static_cast<int64_t>(1000 + next() * 9000));
    }
    return s;
}

}  // namespace

/* ---------------- alignment: the convention every strategy depends on ---------------- */

TEST(indicator, sma_length_and_alignment) {
    const std::vector<double> p = {1, 2, 3, 4, 5, 6};
    const auto                r = indicator::sma(p, 3);
    CHECK_EQ(r.size(), std::size_t{4});  // n - period + 1
    CHECK_NEAR(r[0], 2.0, 1e-12);        // mean of 1,2,3 — so r[0] is at data index 2
    CHECK_NEAR(r.back(), 5.0, 1e-12);
}

TEST(indicator, atr_result_starts_at_period_minus_one) {
    const auto s = makeSeries(50);
    const auto r = indicator::atr(s.high, s.low, s.close, 14);
    // Documented contract: size is n - period + 1, r[0] sits at data index period - 1.
    // SuperTrend and Keltner were once built against a wrong version of this docstring.
    CHECK_EQ(r.size(), s.close.size() - 14 + 1);
    CHECK(r[0] > 0.0);
}

TEST(indicator, rsi_bounded_0_100) {
    const auto s = makeSeries(120);
    for (const double v : indicator::rsi(s.close, 14)) {
        CHECK_MSG(v >= 0.0 && v <= 100.0, "RSI out of range: " << v);
    }
}

TEST(indicator, sma_shorter_than_window_is_empty) {
    CHECK(indicator::sma({1, 2}, 5).empty());
    CHECK(indicator::sma({}, 5).empty());
}

TEST(indicator, zero_period_does_not_crash) {
    CHECK(indicator::sma({1, 2, 3}, 0).empty());
    const auto s = makeSeries(30);
    CHECK(indicator::atr(s.high, s.low, s.close, 0).empty());
    CHECK(indicator::relativeVolume(s.volume, 0).empty());
}

TEST(indicator, mismatched_series_lengths_are_rejected) {
    const auto          s = makeSeries(40);
    std::vector<double> shortHigh(s.high.begin(), s.high.begin() + 10);
    CHECK(indicator::atr(shortHigh, s.low, s.close, 14).empty());
    CHECK(indicator::choppinessIndex(shortHigh, s.low, s.close, 14).empty());
}

/* ---------------- numeric correctness against hand-computed references ---------------- */

TEST(indicator, relative_volume_matches_manual_average) {
    const auto s = makeSeries(60);
    const auto r = indicator::relativeVolume(s.volume, 20);
    CHECK_EQ(r.size(), s.volume.size() - 20 + 1);
    for (std::size_t k = 0; k < r.size(); ++k) {
        const std::size_t i   = k + 19;
        double            sum = 0.0;
        for (std::size_t j = i - 19; j <= i; ++j) {
            sum += static_cast<double>(s.volume[j]);
        }
        CHECK_NEAR(r[k], static_cast<double>(s.volume[i]) / (sum / 20.0), 1e-9);
    }
}

TEST(indicator, historical_volatility_matches_manual_stddev) {
    const auto s = makeSeries(60);
    const auto r = indicator::historicalVolatility(s.close, 20, 252.0);

    std::vector<double> lr;
    for (std::size_t i = 1; i < s.close.size(); ++i) {
        lr.push_back(std::log(s.close[i] / s.close[i - 1]));
    }
    CHECK_EQ(r.size(), lr.size() - 20 + 1);

    const double mean = std::accumulate(lr.begin(), lr.begin() + 20, 0.0) / 20.0;
    double       var  = 0.0;
    for (std::size_t j = 0; j < 20; ++j) {
        var += (lr[j] - mean) * (lr[j] - mean);
    }
    var /= 19.0;  // sample variance
    CHECK_NEAR(r[0], std::sqrt(var) * std::sqrt(252.0) * 100.0, 1e-9);
}

TEST(indicator, bollinger_percent_b_is_zero_at_lower_and_one_at_upper) {
    const auto s     = makeSeries(60);
    const auto bands = indicator::bollinger(s.close, 20, 2.0);
    const auto pos   = indicator::bollingerPosition(s.close, 20, 2.0);
    CHECK_EQ(pos.percentB.size(), bands.middle.size());

    for (std::size_t k = 0; k < pos.percentB.size(); ++k) {
        const double price = s.close[k + 19];
        const double width = bands.upper[k] - bands.lower[k];
        CHECK_NEAR(pos.percentB[k], (price - bands.lower[k]) / width, 1e-9);
        CHECK_NEAR(pos.bandwidth[k], width / bands.middle[k], 1e-9);
    }
}

TEST(indicator, bollinger_width_responds_to_std_devs) {
    // This is the bug that made a whole parameter sweep return identical rows:
    // the caller passed a width that never reached the indicator.
    const auto s  = makeSeries(60);
    const auto b1 = indicator::bollinger(s.close, 20, 1.0);
    const auto b2 = indicator::bollinger(s.close, 20, 2.0);
    CHECK(b1.upper[0] < b2.upper[0]);
    CHECK(b1.lower[0] > b2.lower[0]);
}

TEST(indicator, choppiness_index_within_bounds) {
    const auto s = makeSeries(120);
    const auto r = indicator::choppinessIndex(s.high, s.low, s.close, 14);
    CHECK(!r.empty());
    for (const double v : r) {
        CHECK_MSG(v >= 0.0 && v <= 100.0, "choppiness out of range: " << v);
    }
}

TEST(indicator, ichimoku_cloud_uses_no_future_data) {
    // senkou[i] must be computable from bars up to (i - base). If it were not,
    // every Ichimoku backtest would be reading tomorrow's prices.
    const auto s    = makeSeries(200);
    const auto full = indicator::ichimoku(s.high, s.low, s.close, 9, 26, 52);

    const std::size_t cut = 150;
    Series            trimmed;
    trimmed.high.assign(s.high.begin(), s.high.begin() + cut);
    trimmed.low.assign(s.low.begin(), s.low.begin() + cut);
    trimmed.close.assign(s.close.begin(), s.close.begin() + cut);
    const auto partial = indicator::ichimoku(trimmed.high, trimmed.low, trimmed.close, 9, 26, 52);

    for (std::size_t i = 0; i < cut; ++i) {
        CHECK_NEAR(partial.senkouA[i], full.senkouA[i], 1e-12);
        CHECK_NEAR(partial.senkouB[i], full.senkouB[i], 1e-12);
        CHECK_NEAR(partial.tenkan[i], full.tenkan[i], 1e-12);
        CHECK_NEAR(partial.kijun[i], full.kijun[i], 1e-12);
    }
}

TEST(indicator, chandelier_exit_sits_below_the_recent_high) {
    const auto s = makeSeries(80);
    const auto r = indicator::chandelierExit(s.high, s.low, s.close, 22, 3.0);
    CHECK(!r.empty());
    for (std::size_t k = 0; k < r.size(); ++k) {
        const std::size_t i  = k + 21;
        double            hh = s.high[i - 21];
        for (std::size_t j = i - 21; j <= i; ++j) {
            hh = std::max(hh, s.high[j]);
        }
        CHECK_MSG(r[k] < hh, "chandelier level " << r[k] << " not below high " << hh);
    }
}

TEST(indicator, all_indicators_survive_flat_prices) {
    // A halted or limit-locked stock produces a flat series; division by a zero
    // range must not produce NaN that then propagates into a trading signal.
    const std::vector<double>  flat(60, 100.0);
    const std::vector<int64_t> vol(60, 1000);

    auto finite = [](const std::vector<double>& v, const char* what) {
        for (const double x : v) {
            CHECK_MSG(std::isfinite(x), what << " produced a non-finite value");
        }
    };
    finite(indicator::sma(flat, 20), "sma");
    finite(indicator::rsi(flat, 14), "rsi");
    finite(indicator::atr(flat, flat, flat, 14), "atr");
    finite(indicator::choppinessIndex(flat, flat, flat, 14), "choppiness");
    finite(indicator::historicalVolatility(flat, 20), "historicalVolatility");
    finite(indicator::relativeVolume(vol, 20), "relativeVolume");
    finite(indicator::bollingerPosition(flat, 20, 2.0).percentB, "bollinger %B");
    finite(indicator::cci(flat, flat, flat, 20), "cci");
    finite(indicator::mfi(flat, flat, flat, vol, 14), "mfi");
}
