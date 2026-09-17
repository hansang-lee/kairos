#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

namespace indicator {

/**
 * @brief Compute Simple Moving Average (SMA).
 * @param prices  Input price series.
 * @param window  Window size for the moving average.
 * @return        SMA values. Size = prices.size() - window + 1.
 *                An empty vector is returned if prices.size() < window.
 */
[[nodiscard]] inline std::vector<double> sma(const std::vector<double>& prices, std::size_t window) {
    if (window == 0 || prices.size() < window) {
        return {};
    }

    std::vector<double> result;
    result.reserve(prices.size() - window + 1);

    double sum = 0.0;
    for (std::size_t i = 0; i < window; ++i) {
        sum += prices[i];
    }
    result.push_back(sum / static_cast<double>(window));

    for (std::size_t i = window; i < prices.size(); ++i) {
        sum += prices[i] - prices[i - window];
        result.push_back(sum / static_cast<double>(window));
    }

    return result;
}

/**
 * @brief Compute Exponential Moving Average (EMA).
 * @param prices  Input price series.
 * @param window  Lookback period for smoothing factor alpha = 2 / (window + 1).
 * @return        EMA values. Size = prices.size() - window + 1.
 *                First point is seeded with the initial SMA.
 */
[[nodiscard]] inline std::vector<double> ema(const std::vector<double>& prices, std::size_t window) {
    if (window == 0 || prices.size() < window) {
        return {};
    }

    std::vector<double> result;
    result.reserve(prices.size() - window + 1);

    // Initial seed: SMA of first 'window' points
    double sum = 0.0;
    for (std::size_t i = 0; i < window; ++i) {
        sum += prices[i];
    }
    double prevEma = sum / static_cast<double>(window);
    result.push_back(prevEma);

    const double alpha = 2.0 / static_cast<double>(window + 1);

    for (std::size_t i = window; i < prices.size(); ++i) {
        prevEma = (prices[i] * alpha) + (prevEma * (1.0 - alpha));
        result.push_back(prevEma);
    }

    return result;
}

/**
 * @brief Compute Relative Strength Index (RSI).
 * @param prices  Input price series.
 * @param period  Lookback period (typically 14).
 * @return        RSI values (0~100). Size = prices.size() - period.
 *
 * Uses Wilder's smoothing method.
 */
[[nodiscard]] inline std::vector<double> rsi(const std::vector<double>& prices, std::size_t period) {
    if (period == 0 || prices.size() <= period) {
        return {};
    }

    std::vector<double> result;
    result.reserve(prices.size() - period);

    double avgGain = 0.0;
    double avgLoss = 0.0;
    for (std::size_t i = 1; i <= period; ++i) {
        const double change = prices[i] - prices[i - 1];
        if (change > 0.0) {
            avgGain += change;
        } else {
            avgLoss += -change;
        }
    }
    avgGain /= static_cast<double>(period);
    avgLoss /= static_cast<double>(period);

    if (avgLoss < 1e-12) {
        result.push_back(100.0);
    } else {
        const double rs = avgGain / avgLoss;
        result.push_back(100.0 - (100.0 / (1.0 + rs)));
    }

    const double smooth = static_cast<double>(period - 1) / static_cast<double>(period);
    const double inv    = 1.0 / static_cast<double>(period);

    for (std::size_t i = period + 1; i < prices.size(); ++i) {
        const double change = prices[i] - prices[i - 1];
        if (change > 0.0) {
            avgGain = avgGain * smooth + change * inv;
            avgLoss = avgLoss * smooth;
        } else {
            avgGain = avgGain * smooth;
            avgLoss = avgLoss * smooth + (-change) * inv;
        }

        if (avgLoss < 1e-12) {
            result.push_back(100.0);
        } else {
            const double rs = avgGain / avgLoss;
            result.push_back(100.0 - (100.0 / (1.0 + rs)));
        }
    }

    return result;
}

/**
 * @brief MACD Result structure.
 */
struct MacdResult {
    std::vector<double> macd;       ///< MACD Line = EMA(fast) - EMA(slow)
    std::vector<double> signal;     ///< Signal Line = EMA(MACD Line, signalPeriod)
    std::vector<double> histogram;  ///< Histogram = MACD Line - Signal Line
};

/**
 * @brief Compute Moving Average Convergence Divergence (MACD).
 * @param prices        Input price series.
 * @param fastPeriod    Fast EMA period (typically 12).
 * @param slowPeriod    Slow EMA period (typically 26).
 * @param signalPeriod  Signal EMA period (typically 9).
 * @return              MacdResult with aligned vectors matching the signal line length.
 */
[[nodiscard]] inline MacdResult macd(const std::vector<double>& prices, std::size_t fastPeriod = 12,
                                     std::size_t slowPeriod = 26, std::size_t signalPeriod = 9) {
    if (fastPeriod >= slowPeriod || prices.size() < slowPeriod + signalPeriod - 1) {
        return {};
    }

    const auto fastEma = ema(prices, fastPeriod);
    const auto slowEma = ema(prices, slowPeriod);

    // fastEma starts at index (fastPeriod - 1)
    // slowEma starts at index (slowPeriod - 1)
    // Offset between slowEma and fastEma
    const std::size_t offset = slowPeriod - fastPeriod;

    std::vector<double> rawMacd;
    rawMacd.reserve(slowEma.size());
    for (std::size_t i = 0; i < slowEma.size(); ++i) {
        rawMacd.push_back(fastEma[i + offset] - slowEma[i]);
    }

    const auto signal = ema(rawMacd, signalPeriod);
    if (signal.empty()) {
        return {};
    }

    // Align MACD line with signal line (offset by signalPeriod - 1)
    const std::size_t   sigOffset = signalPeriod - 1;
    std::vector<double> alignedMacd(rawMacd.begin() + sigOffset, rawMacd.end());

    std::vector<double> hist;
    hist.reserve(signal.size());
    for (std::size_t i = 0; i < signal.size(); ++i) {
        hist.push_back(alignedMacd[i] - signal[i]);
    }

    return {std::move(alignedMacd), signal, std::move(hist)};
}

/**
 * @brief Bollinger Bands Result structure.
 */
struct BollingerBands {
    std::vector<double> upper;   ///< Upper Band = Middle + (numStdDev * stdDev)
    std::vector<double> middle;  ///< Middle Band = SMA(period)
    std::vector<double> lower;   ///< Lower Band = Middle - (numStdDev * stdDev)
};

/**
 * @brief Compute Bollinger Bands.
 * @param prices     Input price series.
 * @param period     Lookback period (typically 20).
 * @param numStdDev  Number of standard deviations (typically 2.0).
 * @return           BollingerBands with aligned upper, middle, and lower vectors.
 */
[[nodiscard]] inline BollingerBands bollinger(const std::vector<double>& prices, std::size_t period = 20,
                                              double numStdDev = 2.0) {
    if (period == 0 || prices.size() < period) {
        return {};
    }

    const auto        middle = sma(prices, period);
    const std::size_t n      = middle.size();

    std::vector<double> upper;
    std::vector<double> lower;
    upper.reserve(n);
    lower.reserve(n);

    for (std::size_t i = 0; i < n; ++i) {
        const double mean   = middle[i];
        double       varSum = 0.0;
        for (std::size_t j = 0; j < period; ++j) {
            const double diff = prices[i + j] - mean;
            varSum += diff * diff;
        }
        const double stdDev = std::sqrt(varSum / static_cast<double>(period));

        upper.push_back(mean + (numStdDev * stdDev));
        lower.push_back(mean - (numStdDev * stdDev));
    }

    return {std::move(upper), middle, std::move(lower)};
}

/**
 * @brief Compute Average True Range (ATR).
 * @param high    High price series.
 * @param low     Low price series.
 * @param close   Close price series.
 * @param period  Lookback period (typically 14).
 * @return        ATR values. Size = close.size() - period.
 *
 * Measures market volatility. True Range = max(H - L, |H - C_prev|, |L - C_prev|).
 */
[[nodiscard]] inline std::vector<double> atr(const std::vector<double>& high, const std::vector<double>& low,
                                             const std::vector<double>& close, std::size_t period = 14) {
    const std::size_t n = close.size();
    if (period == 0 || n <= period || high.size() != n || low.size() != n) {
        return {};
    }

    std::vector<double> tr;
    tr.reserve(n);

    // First TR is simply high[0] - low[0]
    tr.push_back(high[0] - low[0]);

    for (std::size_t i = 1; i < n; ++i) {
        const double hl = high[i] - low[i];
        const double hc = std::abs(high[i] - close[i - 1]);
        const double lc = std::abs(low[i] - close[i - 1]);
        tr.push_back(std::max({hl, hc, lc}));
    }

    // Initial ATR: simple average of first 'period' TR values
    double atrVal = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        atrVal += tr[i];
    }
    atrVal /= static_cast<double>(period);

    std::vector<double> result;
    result.reserve(n - period);
    result.push_back(atrVal);

    // Subsequent values via Wilder's smoothing
    const double smooth = static_cast<double>(period - 1);
    const double invP   = 1.0 / static_cast<double>(period);

    for (std::size_t i = period; i < n; ++i) {
        atrVal = (atrVal * smooth + tr[i]) * invP;
        result.push_back(atrVal);
    }

    return result;
}

/**
 * @brief Compute Volume Weighted Average Price (VWAP).
 * @param high    High price series.
 * @param low     Low price series.
 * @param close   Close price series.
 * @param volume  Volume series.
 * @return        Cumulative VWAP series of the same length.
 */
[[nodiscard]] inline std::vector<double> vwap(const std::vector<double>& high, const std::vector<double>& low,
                                              const std::vector<double>& close, const std::vector<int64_t>& volume) {
    const std::size_t n = close.size();
    if (n == 0 || high.size() != n || low.size() != n || volume.size() != n) {
        return {};
    }

    std::vector<double> result;
    result.reserve(n);

    double cumTypicalVol = 0.0;
    double cumVol        = 0.0;

    for (std::size_t i = 0; i < n; ++i) {
        const double typicalPrice = (high[i] + low[i] + close[i]) / 3.0;
        const double vol          = static_cast<double>(volume[i]);

        cumTypicalVol += typicalPrice * vol;
        cumVol += vol;

        if (cumVol > 0.0) {
            result.push_back(cumTypicalVol / cumVol);
        } else {
            result.push_back(typicalPrice);
        }
    }

    return result;
}

/**
 * @brief Stochastic Oscillator Result structure.
 */
struct StochasticResult {
    std::vector<double> k;  ///< %K Line = (Close - LowestLow) / (HighestHigh - LowestLow) * 100
    std::vector<double> d;  ///< %D Line = SMA(%K, dPeriod)
};

/**
 * @brief Compute Fast/Slow Stochastic Oscillator.
 * @param high     High price series.
 * @param low      Low price series.
 * @param close    Close price series.
 * @param kPeriod  Lookback window for %K (typically 14).
 * @param dPeriod  Smoothing window for %D (typically 3).
 * @return         StochasticResult with aligned %K and %D lines.
 */
[[nodiscard]] inline StochasticResult stochastic(const std::vector<double>& high, const std::vector<double>& low,
                                                 const std::vector<double>& close, std::size_t kPeriod = 14,
                                                 std::size_t dPeriod = 3) {
    const std::size_t n = close.size();
    if (kPeriod == 0 || dPeriod == 0 || n < kPeriod + dPeriod - 1 || high.size() != n || low.size() != n) {
        return {};
    }

    std::vector<double> rawK;
    rawK.reserve(n - kPeriod + 1);

    for (std::size_t i = kPeriod - 1; i < n; ++i) {
        double highest = high[i];
        double lowest  = low[i];
        for (std::size_t j = 0; j < kPeriod; ++j) {
            highest = std::max(highest, high[i - j]);
            lowest  = std::min(lowest, low[i - j]);
        }

        const double diff = highest - lowest;
        if (diff < 1e-12) {
            rawK.push_back(50.0);
        } else {
            rawK.push_back((close[i] - lowest) / diff * 100.0);
        }
    }

    const auto d = sma(rawK, dPeriod);
    if (d.empty()) {
        return {};
    }

    // Align %K with %D (offset by dPeriod - 1)
    std::vector<double> alignedK(rawK.begin() + (dPeriod - 1), rawK.end());

    return {std::move(alignedK), d};
}

}  // namespace indicator
