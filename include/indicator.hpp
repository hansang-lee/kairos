#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
 * @return        ATR values. Size = close.size() - period + 1; result[0] corresponds to data index (period - 1).
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

/**
 * @brief Compute Weighted Moving Average (WMA) — linearly weights recent prices higher.
 * @param prices  Input price series.
 * @param window  Window size.
 * @return        WMA values. Size = prices.size() - window + 1.
 */
[[nodiscard]] inline std::vector<double> wma(const std::vector<double>& prices, std::size_t window) {
    if (window == 0 || prices.size() < window) {
        return {};
    }

    std::vector<double> result;
    result.reserve(prices.size() - window + 1);
    const double denom = static_cast<double>(window) * static_cast<double>(window + 1) / 2.0;

    for (std::size_t i = window - 1; i < prices.size(); ++i) {
        double sum = 0.0;
        for (std::size_t j = 0; j < window; ++j) {
            sum += prices[i - j] * static_cast<double>(window - j);
        }
        result.push_back(sum / denom);
    }

    return result;
}

/**
 * @brief Compute rolling standard deviation (population).
 * @param prices  Input price series.
 * @param window  Window size.
 * @return        Std-dev values. Size = prices.size() - window + 1.
 */
[[nodiscard]] inline std::vector<double> stddev(const std::vector<double>& prices, std::size_t window) {
    if (window == 0 || prices.size() < window) {
        return {};
    }

    std::vector<double> result;
    result.reserve(prices.size() - window + 1);

    for (std::size_t i = window - 1; i < prices.size(); ++i) {
        double sum = 0.0;
        for (std::size_t j = 0; j < window; ++j) {
            sum += prices[i - j];
        }
        const double mean = sum / static_cast<double>(window);

        double varSum = 0.0;
        for (std::size_t j = 0; j < window; ++j) {
            const double diff = prices[i - j] - mean;
            varSum += diff * diff;
        }
        result.push_back(std::sqrt(varSum / static_cast<double>(window)));
    }

    return result;
}

/**
 * @brief Compute Rate of Change (ROC) as a percentage.
 * @param prices  Input price series.
 * @param period  Lookback period.
 * @return        ROC values (%). Size = prices.size() - period.
 */
[[nodiscard]] inline std::vector<double> roc(const std::vector<double>& prices, std::size_t period) {
    if (period == 0 || prices.size() <= period) {
        return {};
    }

    std::vector<double> result;
    result.reserve(prices.size() - period);
    for (std::size_t i = period; i < prices.size(); ++i) {
        const double prev = prices[i - period];
        result.push_back(prev != 0.0 ? (prices[i] - prev) / prev * 100.0 : 0.0);
    }

    return result;
}

/**
 * @brief Compute Commodity Channel Index (CCI).
 * @param high    High price series.
 * @param low     Low price series.
 * @param close   Close price series.
 * @param period  Lookback period (typically 20).
 * @return        CCI values. Size = close.size() - period + 1.
 */
[[nodiscard]] inline std::vector<double> cci(const std::vector<double>& high, const std::vector<double>& low,
                                             const std::vector<double>& close, std::size_t period = 20) {
    const std::size_t n = close.size();
    if (period == 0 || n < period || high.size() != n || low.size() != n) {
        return {};
    }

    std::vector<double> typical(n);
    for (std::size_t i = 0; i < n; ++i) {
        typical[i] = (high[i] + low[i] + close[i]) / 3.0;
    }

    std::vector<double> result;
    result.reserve(n - period + 1);
    for (std::size_t i = period - 1; i < n; ++i) {
        double sum = 0.0;
        for (std::size_t j = 0; j < period; ++j) {
            sum += typical[i - j];
        }
        const double smaTp = sum / static_cast<double>(period);

        double meanDev = 0.0;
        for (std::size_t j = 0; j < period; ++j) {
            meanDev += std::abs(typical[i - j] - smaTp);
        }
        meanDev /= static_cast<double>(period);

        result.push_back(meanDev < 1e-12 ? 0.0 : (typical[i] - smaTp) / (0.015 * meanDev));
    }

    return result;
}

/**
 * @brief Compute Williams %R.
 * @param high    High price series.
 * @param low     Low price series.
 * @param close   Close price series.
 * @param period  Lookback period (typically 14).
 * @return        %R values (-100~0). Size = close.size() - period + 1.
 */
[[nodiscard]] inline std::vector<double> williamsR(const std::vector<double>& high, const std::vector<double>& low,
                                                   const std::vector<double>& close, std::size_t period = 14) {
    const std::size_t n = close.size();
    if (period == 0 || n < period || high.size() != n || low.size() != n) {
        return {};
    }

    std::vector<double> result;
    result.reserve(n - period + 1);
    for (std::size_t i = period - 1; i < n; ++i) {
        double highest = high[i];
        double lowest  = low[i];
        for (std::size_t j = 0; j < period; ++j) {
            highest = std::max(highest, high[i - j]);
            lowest  = std::min(lowest, low[i - j]);
        }
        const double diff = highest - lowest;
        result.push_back(diff < 1e-12 ? -50.0 : (highest - close[i]) / diff * -100.0);
    }

    return result;
}

/**
 * @brief Compute TRIX — rate of change of a triple-smoothed EMA.
 * @param prices  Input price series.
 * @param period  Smoothing period for each of the 3 EMA passes (typically 15).
 * @return        TRIX values (%).
 */
[[nodiscard]] inline std::vector<double> trix(const std::vector<double>& prices, std::size_t period = 15) {
    const auto ema1 = ema(prices, period);
    const auto ema2 = ema(ema1, period);
    const auto ema3 = ema(ema2, period);
    if (ema3.size() < 2) {
        return {};
    }

    std::vector<double> result;
    result.reserve(ema3.size() - 1);
    for (std::size_t i = 1; i < ema3.size(); ++i) {
        const double prev = ema3[i - 1];
        result.push_back(prev != 0.0 ? (ema3[i] - prev) / prev * 100.0 : 0.0);
    }

    return result;
}

/**
 * @brief Directional Movement Index result: +DI, -DI, and the smoothed ADX line.
 *
 * plusDI[i] and minusDI[i] correspond to data index (period + i).
 * adx[k] correspond to data index (2*period - 1 + k), and also lines up with
 * plusDI[k + period - 1] / minusDI[k + period - 1] (same data index).
 */
struct DmiResult {
    std::vector<double> plusDI;
    std::vector<double> minusDI;
    std::vector<double> adx;
};

/**
 * @brief Compute the Directional Movement Index / Average Directional Index (Wilder).
 * @param high    High price series.
 * @param low     Low price series.
 * @param close   Close price series.
 * @param period  Wilder smoothing period (typically 14).
 * @return        DmiResult with +DI, -DI, and ADX lines.
 */
[[nodiscard]] inline DmiResult adx(const std::vector<double>& high, const std::vector<double>& low,
                                   const std::vector<double>& close, std::size_t period = 14) {
    const std::size_t n = close.size();
    if (period == 0 || n <= period + 1 || high.size() != n || low.size() != n) {
        return {};
    }

    std::vector<double> tr(n, 0.0), plusDM(n, 0.0), minusDM(n, 0.0);
    for (std::size_t i = 1; i < n; ++i) {
        const double upMove   = high[i] - high[i - 1];
        const double downMove = low[i - 1] - low[i];
        plusDM[i]             = (upMove > downMove && upMove > 0.0) ? upMove : 0.0;
        minusDM[i]            = (downMove > upMove && downMove > 0.0) ? downMove : 0.0;

        const double hl = high[i] - low[i];
        const double hc = std::abs(high[i] - close[i - 1]);
        const double lc = std::abs(low[i] - close[i - 1]);
        tr[i]           = std::max({hl, hc, lc});
    }

    double smoothTr = 0.0, smoothPlusDM = 0.0, smoothMinusDM = 0.0;
    for (std::size_t i = 1; i <= period; ++i) {
        smoothTr += tr[i];
        smoothPlusDM += plusDM[i];
        smoothMinusDM += minusDM[i];
    }

    std::vector<double> plusDIvec, minusDIvec, dxVec;
    const auto          pushDI = [&]() {
        const double pDI = smoothTr < 1e-12 ? 0.0 : (smoothPlusDM / smoothTr) * 100.0;
        const double mDI = smoothTr < 1e-12 ? 0.0 : (smoothMinusDM / smoothTr) * 100.0;
        plusDIvec.push_back(pDI);
        minusDIvec.push_back(mDI);
        const double sum = pDI + mDI;
        dxVec.push_back(sum < 1e-12 ? 0.0 : std::abs(pDI - mDI) / sum * 100.0);
    };
    pushDI();

    for (std::size_t i = period + 1; i < n; ++i) {
        smoothTr      = smoothTr - (smoothTr / static_cast<double>(period)) + tr[i];
        smoothPlusDM  = smoothPlusDM - (smoothPlusDM / static_cast<double>(period)) + plusDM[i];
        smoothMinusDM = smoothMinusDM - (smoothMinusDM / static_cast<double>(period)) + minusDM[i];
        pushDI();
    }

    if (dxVec.size() < period) {
        return {std::move(plusDIvec), std::move(minusDIvec), {}};
    }

    std::vector<double> adxVec;
    adxVec.reserve(dxVec.size() - period + 1);
    double sumDx = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        sumDx += dxVec[i];
    }
    adxVec.push_back(sumDx / static_cast<double>(period));
    for (std::size_t i = period; i < dxVec.size(); ++i) {
        const double prevAdx = adxVec.back();
        adxVec.push_back((prevAdx * static_cast<double>(period - 1) + dxVec[i]) / static_cast<double>(period));
    }

    return {std::move(plusDIvec), std::move(minusDIvec), std::move(adxVec)};
}

/**
 * @brief Compute the Parabolic SAR (Wilder).
 * @param high    High price series.
 * @param low     Low price series.
 * @param afStep  Acceleration factor step (typically 0.02).
 * @param afMax   Maximum acceleration factor (typically 0.2).
 * @return        SAR values, 1:1 aligned with the input (size = high.size()).
 */
[[nodiscard]] inline std::vector<double> parabolicSar(const std::vector<double>& high, const std::vector<double>& low,
                                                      double afStep = 0.02, double afMax = 0.2) {
    const std::size_t n = high.size();
    if (n < 2 || low.size() != n) {
        return {};
    }

    std::vector<double> sar(n, 0.0);
    bool                uptrend = true;
    double              af      = afStep;
    double              ep      = high[0];
    sar[0]                      = low[0];

    for (std::size_t i = 1; i < n; ++i) {
        const double prevSar = sar[i - 1];
        double       nextSar = prevSar + af * (ep - prevSar);

        if (uptrend) {
            nextSar = std::min({nextSar, low[i - 1], i >= 2 ? low[i - 2] : low[i - 1]});
            if (low[i] < nextSar) {
                uptrend = false;
                nextSar = ep;
                ep      = low[i];
                af      = afStep;
            } else if (high[i] > ep) {
                ep = high[i];
                af = std::min(af + afStep, afMax);
            }
        } else {
            nextSar = std::max({nextSar, high[i - 1], i >= 2 ? high[i - 2] : high[i - 1]});
            if (high[i] > nextSar) {
                uptrend = true;
                nextSar = ep;
                ep      = high[i];
                af      = afStep;
            } else if (low[i] < ep) {
                ep = low[i];
                af = std::min(af + afStep, afMax);
            }
        }

        sar[i] = nextSar;
    }

    return sar;
}

/**
 * @brief SuperTrend result: the trailing stop line and its trend direction.
 */
struct SuperTrendResult {
    std::vector<double> value;  ///< SuperTrend line, aligned to data index (i + period).
    std::vector<int>    trend;  ///< 1 = uptrend, -1 = downtrend.
};

/**
 * @brief Compute the SuperTrend indicator (ATR-band trailing stop with flips).
 * @param high        High price series.
 * @param low         Low price series.
 * @param close       Close price series.
 * @param period      ATR period (typically 10).
 * @param multiplier  ATR band multiplier (typically 3.0).
 * @return            SuperTrendResult. value[i]/trend[i] correspond to data index (i + period - 1).
 */
[[nodiscard]] inline SuperTrendResult superTrend(const std::vector<double>& high, const std::vector<double>& low,
                                                 const std::vector<double>& close, std::size_t period = 10,
                                                 double multiplier = 3.0) {
    const auto atrVals = atr(high, low, close, period);
    if (atrVals.empty()) {
        return {};
    }
    const std::size_t offset = period - 1;  // atrVals[0] corresponds to data index `period - 1`.
    const std::size_t m      = atrVals.size();

    std::vector<double> finalUpper(m), finalLower(m), value(m);
    std::vector<int>    trend(m);

    for (std::size_t i = 0; i < m; ++i) {
        const std::size_t di         = i + offset;
        const double      mid        = (high[di] + low[di]) / 2.0;
        const double      basicUpper = mid + multiplier * atrVals[i];
        const double      basicLower = mid - multiplier * atrVals[i];

        if (i == 0) {
            finalUpper[i] = basicUpper;
            finalLower[i] = basicLower;
            trend[i]      = (close[di] <= basicUpper) ? -1 : 1;
            value[i]      = (trend[i] == 1) ? finalLower[i] : finalUpper[i];
            continue;
        }

        finalUpper[i] =
            (basicUpper < finalUpper[i - 1] || close[di - 1] > finalUpper[i - 1]) ? basicUpper : finalUpper[i - 1];
        finalLower[i] =
            (basicLower > finalLower[i - 1] || close[di - 1] < finalLower[i - 1]) ? basicLower : finalLower[i - 1];

        if (trend[i - 1] == 1) {
            trend[i] = (close[di] < finalLower[i]) ? -1 : 1;
        } else {
            trend[i] = (close[di] > finalUpper[i]) ? 1 : -1;
        }
        value[i] = (trend[i] == 1) ? finalLower[i] : finalUpper[i];
    }

    return {std::move(value), std::move(trend)};
}

/**
 * @brief Aroon result: Up and Down lines (0~100).
 */
struct AroonResult {
    std::vector<double> up;
    std::vector<double> down;
};

/**
 * @brief Compute the Aroon Up/Down indicator.
 * @param high    High price series.
 * @param low     Low price series.
 * @param period  Lookback period (typically 25).
 * @return        AroonResult. up[0]/down[0] correspond to data index `period`.
 */
[[nodiscard]] inline AroonResult aroon(const std::vector<double>& high, const std::vector<double>& low,
                                       std::size_t period = 25) {
    const std::size_t n = high.size();
    if (period == 0 || n <= period || low.size() != n) {
        return {};
    }

    std::vector<double> up, down;
    up.reserve(n - period);
    down.reserve(n - period);

    for (std::size_t i = period; i < n; ++i) {
        std::size_t highIdx = i, lowIdx = i;
        for (std::size_t j = i - period; j <= i; ++j) {
            if (high[j] >= high[highIdx])
                highIdx = j;
            if (low[j] <= low[lowIdx])
                lowIdx = j;
        }
        const double sinceHigh = static_cast<double>(i - highIdx);
        const double sinceLow  = static_cast<double>(i - lowIdx);
        up.push_back((static_cast<double>(period) - sinceHigh) / static_cast<double>(period) * 100.0);
        down.push_back((static_cast<double>(period) - sinceLow) / static_cast<double>(period) * 100.0);
    }

    return {std::move(up), std::move(down)};
}

/**
 * @brief Compute On-Balance Volume (OBV).
 * @param close   Close price series.
 * @param volume  Volume series.
 * @return        Cumulative OBV, 1:1 aligned with the input.
 */
[[nodiscard]] inline std::vector<double> obv(const std::vector<double>& close, const std::vector<int64_t>& volume) {
    const std::size_t n = close.size();
    if (n == 0 || volume.size() != n) {
        return {};
    }

    std::vector<double> result(n);
    result[0] = static_cast<double>(volume[0]);
    for (std::size_t i = 1; i < n; ++i) {
        if (close[i] > close[i - 1]) {
            result[i] = result[i - 1] + static_cast<double>(volume[i]);
        } else if (close[i] < close[i - 1]) {
            result[i] = result[i - 1] - static_cast<double>(volume[i]);
        } else {
            result[i] = result[i - 1];
        }
    }

    return result;
}

/**
 * @brief Compute the Money Flow Index (MFI) — volume-weighted RSI.
 * @param high    High price series.
 * @param low     Low price series.
 * @param close   Close price series.
 * @param volume  Volume series.
 * @param period  Lookback period (typically 14).
 * @return        MFI values (0~100). Size = close.size() - period.
 */
[[nodiscard]] inline std::vector<double> mfi(const std::vector<double>& high, const std::vector<double>& low,
                                             const std::vector<double>& close, const std::vector<int64_t>& volume,
                                             std::size_t period = 14) {
    const std::size_t n = close.size();
    if (period == 0 || n <= period || high.size() != n || low.size() != n || volume.size() != n) {
        return {};
    }

    std::vector<double> typical(n), rawMoneyFlow(n);
    for (std::size_t i = 0; i < n; ++i) {
        typical[i]      = (high[i] + low[i] + close[i]) / 3.0;
        rawMoneyFlow[i] = typical[i] * static_cast<double>(volume[i]);
    }

    std::vector<double> result;
    result.reserve(n - period);
    for (std::size_t i = period; i < n; ++i) {
        double posFlow = 0.0, negFlow = 0.0;
        for (std::size_t j = i - period + 1; j <= i; ++j) {
            if (typical[j] > typical[j - 1]) {
                posFlow += rawMoneyFlow[j];
            } else if (typical[j] < typical[j - 1]) {
                negFlow += rawMoneyFlow[j];
            }
        }
        result.push_back(negFlow < 1e-12 ? 100.0 : 100.0 - (100.0 / (1.0 + posFlow / negFlow)));
    }

    return result;
}

/**
 * @brief Compute Chaikin Money Flow (CMF).
 * @param high    High price series.
 * @param low     Low price series.
 * @param close   Close price series.
 * @param volume  Volume series.
 * @param period  Lookback period (typically 20).
 * @return        CMF values. Size = close.size() - period + 1.
 */
[[nodiscard]] inline std::vector<double> cmf(const std::vector<double>& high, const std::vector<double>& low,
                                             const std::vector<double>& close, const std::vector<int64_t>& volume,
                                             std::size_t period = 20) {
    const std::size_t n = close.size();
    if (period == 0 || n < period || high.size() != n || low.size() != n || volume.size() != n) {
        return {};
    }

    std::vector<double> mfv(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double range = high[i] - low[i];
        const double mult  = range < 1e-12 ? 0.0 : ((close[i] - low[i]) - (high[i] - close[i])) / range;
        mfv[i]             = mult * static_cast<double>(volume[i]);
    }

    std::vector<double> result;
    result.reserve(n - period + 1);
    for (std::size_t i = period - 1; i < n; ++i) {
        double  sumMfv = 0.0;
        int64_t sumVol = 0;
        for (std::size_t j = 0; j < period; ++j) {
            sumMfv += mfv[i - j];
            sumVol += volume[i - j];
        }
        result.push_back(sumVol == 0 ? 0.0 : sumMfv / static_cast<double>(sumVol));
    }

    return result;
}

/**
 * @brief Compute the Accumulation/Distribution Line.
 * @param high    High price series.
 * @param low     Low price series.
 * @param close   Close price series.
 * @param volume  Volume series.
 * @return        Cumulative A/D line, 1:1 aligned with the input.
 */
[[nodiscard]] inline std::vector<double> adLine(const std::vector<double>& high, const std::vector<double>& low,
                                                const std::vector<double>& close, const std::vector<int64_t>& volume) {
    const std::size_t n = close.size();
    if (n == 0 || high.size() != n || low.size() != n || volume.size() != n) {
        return {};
    }

    std::vector<double> result(n);
    double              cum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double range = high[i] - low[i];
        const double mult  = range < 1e-12 ? 0.0 : ((close[i] - low[i]) - (high[i] - close[i])) / range;
        cum += mult * static_cast<double>(volume[i]);
        result[i] = cum;
    }

    return result;
}

/**
 * @brief Donchian Channel result.
 */
struct DonchianResult {
    std::vector<double> upper;
    std::vector<double> middle;
    std::vector<double> lower;
};

/**
 * @brief Compute Donchian Channels (highest-high / lowest-low breakout bands).
 * @param high    High price series.
 * @param low     Low price series.
 * @param period  Lookback period (typically 20).
 * @return        DonchianResult. Size = high.size() - period + 1.
 */
[[nodiscard]] inline DonchianResult donchian(const std::vector<double>& high, const std::vector<double>& low,
                                             std::size_t period = 20) {
    const std::size_t n = high.size();
    if (period == 0 || n < period || low.size() != n) {
        return {};
    }

    std::vector<double> upper, lower, middle;
    const std::size_t   m = n - period + 1;
    upper.reserve(m);
    lower.reserve(m);
    middle.reserve(m);

    for (std::size_t i = period - 1; i < n; ++i) {
        double hi = high[i], lo = low[i];
        for (std::size_t j = 0; j < period; ++j) {
            hi = std::max(hi, high[i - j]);
            lo = std::min(lo, low[i - j]);
        }
        upper.push_back(hi);
        lower.push_back(lo);
        middle.push_back((hi + lo) / 2.0);
    }

    return {std::move(upper), std::move(middle), std::move(lower)};
}

/**
 * @brief Keltner Channel result.
 */
struct KeltnerResult {
    std::vector<double> upper;
    std::vector<double> middle;
    std::vector<double> lower;
};

/**
 * @brief Compute Keltner Channels (EMA midline +/- ATR multiple).
 * @param high        High price series.
 * @param low         Low price series.
 * @param close       Close price series.
 * @param emaPeriod   EMA midline period (typically 20).
 * @param atrPeriod   ATR period (typically 10).
 * @param multiplier  ATR band multiplier (typically 2.0).
 * @return            KeltnerResult aligned to data index `max(emaPeriod - 1, atrPeriod - 1)` onward.
 */
[[nodiscard]] inline KeltnerResult keltner(const std::vector<double>& high, const std::vector<double>& low,
                                           const std::vector<double>& close, std::size_t emaPeriod = 20,
                                           std::size_t atrPeriod = 10, double multiplier = 2.0) {
    const auto middleEma = ema(close, emaPeriod);
    const auto atrVals   = atr(high, low, close, atrPeriod);
    if (middleEma.empty() || atrVals.empty()) {
        return {};
    }

    const std::size_t emaOffset = emaPeriod - 1;
    const std::size_t atrOffset = atrPeriod - 1;  // atrVals[0] corresponds to data index `atrPeriod - 1`.
    const std::size_t start     = std::max(emaOffset, atrOffset);

    std::vector<double> upper, middle, lower;
    for (std::size_t di = start; di < close.size(); ++di) {
        const double mid    = middleEma[di - emaOffset];
        const double atrVal = atrVals[di - atrOffset];
        middle.push_back(mid);
        upper.push_back(mid + multiplier * atrVal);
        lower.push_back(mid - multiplier * atrVal);
    }

    return {std::move(upper), std::move(middle), std::move(lower)};
}

/**
 * @brief Compute the linear-regression slope of a moving average, normalized to
 *        %-per-bar relative to the MA's own level (so a threshold is comparable
 *        across tickers/price levels). Smoother than a point-to-point slope,
 *        since it fits a line across `slopeWindow` MA points rather than reacting
 *        to any single bar.
 * @param prices      Input price series.
 * @param maPeriod    SMA period for the underlying moving average (e.g. 20).
 * @param slopeWindow Number of MA points the regression line is fit over (e.g. 10).
 * @return            Slope values (% per bar). result[0] corresponds to data index
 *                     (maPeriod + slopeWindow - 2).
 */
[[nodiscard]] inline std::vector<double> maSlope(const std::vector<double>& prices, std::size_t maPeriod,
                                                 std::size_t slopeWindow) {
    if (slopeWindow < 2) {
        return {};
    }

    const auto ma = sma(prices, maPeriod);
    if (ma.size() < slopeWindow) {
        return {};
    }

    std::vector<double> result;
    result.reserve(ma.size() - slopeWindow + 1);

    const double n    = static_cast<double>(slopeWindow);
    double       sumX = 0.0, sumXX = 0.0;
    for (std::size_t j = 0; j < slopeWindow; ++j) {
        const double x = static_cast<double>(j);
        sumX += x;
        sumXX += x * x;
    }
    const double denom = n * sumXX - sumX * sumX;

    for (std::size_t i = slopeWindow - 1; i < ma.size(); ++i) {
        double sumY = 0.0, sumXY = 0.0;
        for (std::size_t j = 0; j < slopeWindow; ++j) {
            const double x = static_cast<double>(j);
            const double y = ma[i - slopeWindow + 1 + j];
            sumY += y;
            sumXY += x * y;
        }
        const double slope = (denom != 0.0) ? (n * sumXY - sumX * sumY) / denom : 0.0;
        const double meanY = sumY / n;
        result.push_back(meanY != 0.0 ? slope / meanY * 100.0 : 0.0);
    }

    return result;
}

/**
 * @brief Relative volume — current volume against its own recent average.
 *
 * A breakout on thin volume is usually noise, so this is mostly used as a
 * confirmation filter rather than a signal of its own. 1.0 means "average";
 * 2.0 means twice the usual participation.
 *
 * @param volume Volume series.
 * @param period Lookback for the average (e.g. 20).
 * @return       Ratios. result[0] corresponds to data index (period - 1).
 */
[[nodiscard]] inline std::vector<double> relativeVolume(const std::vector<int64_t>& volume, std::size_t period = 20) {
    if (period == 0 || volume.size() < period) {
        return {};
    }

    std::vector<double> result;
    result.reserve(volume.size() - period + 1);

    double sum = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        sum += static_cast<double>(volume[i]);
    }
    for (std::size_t i = period - 1; i < volume.size(); ++i) {
        if (i >= period) {
            sum += static_cast<double>(volume[i]) - static_cast<double>(volume[i - period]);
        }
        const double avg = sum / static_cast<double>(period);
        result.push_back(avg > 0.0 ? static_cast<double>(volume[i]) / avg : 0.0);
    }

    return result;
}

/**
 * @brief Bollinger %B and bandwidth.
 *
 * %B locates price within the bands (0 = lower, 1 = upper), which survives
 * changes in volatility that make the raw band levels incomparable over time.
 * Bandwidth is the band width relative to the middle band — a low value is the
 * "squeeze" that often precedes an expansion.
 */
struct BollingerPositionResult {
    std::vector<double> percentB;   ///< (price - lower) / (upper - lower)
    std::vector<double> bandwidth;  ///< (upper - lower) / middle, as a fraction
};

/**
 * @param prices   Close prices.
 * @param period   Moving-average period (e.g. 20).
 * @param stdDevs  Band width in standard deviations (e.g. 2.0).
 * @return         Both series. result[0] corresponds to data index (period - 1).
 */
[[nodiscard]] inline BollingerPositionResult bollingerPosition(const std::vector<double>& prices,
                                                               std::size_t period = 20, double stdDevs = 2.0) {
    BollingerPositionResult result;

    const auto bands = bollinger(prices, period, stdDevs);
    if (bands.middle.empty()) {
        return result;
    }

    result.percentB.reserve(bands.middle.size());
    result.bandwidth.reserve(bands.middle.size());

    for (std::size_t i = 0; i < bands.middle.size(); ++i) {
        const double price = prices[i + period - 1];
        const double width = bands.upper[i] - bands.lower[i];
        result.percentB.push_back(width != 0.0 ? (price - bands.lower[i]) / width : 0.5);
        result.bandwidth.push_back(bands.middle[i] != 0.0 ? width / bands.middle[i] : 0.0);
    }

    return result;
}

/**
 * @brief Choppiness Index — is the market trending or ranging?
 *
 * Near 100 the market is covering the same ground repeatedly (range); near 0 it
 * is moving directionally. It says nothing about direction, which is the point:
 * it is a gate for trend strategies, which lose money in chop, and for
 * mean-reversion strategies, which lose money in trends.
 *
 * @param high   High prices.
 * @param low    Low prices.
 * @param close  Close prices.
 * @param period Lookback (e.g. 14).
 * @return       Values 0~100. result[0] corresponds to data index period.
 */
[[nodiscard]] inline std::vector<double> choppinessIndex(const std::vector<double>& high,
                                                         const std::vector<double>& low,
                                                         const std::vector<double>& close, std::size_t period = 14) {
    if (period < 2 || high.size() != low.size() || high.size() != close.size() || close.size() <= period) {
        return {};
    }

    // True range per bar, starting at data index 1 (needs the previous close).
    std::vector<double> tr;
    tr.reserve(close.size() - 1);
    for (std::size_t i = 1; i < close.size(); ++i) {
        const double a = high[i] - low[i];
        const double b = std::fabs(high[i] - close[i - 1]);
        const double c = std::fabs(low[i] - close[i - 1]);
        tr.push_back(std::max({a, b, c}));
    }

    std::vector<double> result;
    result.reserve(tr.size() - period + 1);

    const double logPeriod = std::log10(static_cast<double>(period));
    for (std::size_t end = period; end <= tr.size(); ++end) {
        double sumTr = 0.0;
        for (std::size_t j = end - period; j < end; ++j) {
            sumTr += tr[j];
        }
        // The price range over the same window, in data-index terms.
        const std::size_t dataEnd   = end;  // tr[j] belongs to data index j + 1
        const std::size_t dataStart = dataEnd - period + 1;
        double            hh        = high[dataStart];
        double            ll        = low[dataStart];
        for (std::size_t j = dataStart; j <= dataEnd; ++j) {
            hh = std::max(hh, high[j]);
            ll = std::min(ll, low[j]);
        }
        const double range = hh - ll;
        result.push_back((range > 0.0 && sumTr > 0.0 && logPeriod > 0.0) ? 100.0 * std::log10(sumTr / range) / logPeriod
                                                                         : 50.0);
    }

    return result;
}

/**
 * @brief Chandelier Exit — an ATR-distance trailing stop level.
 *
 * The long exit hangs a multiple of ATR below the highest high since entry, so
 * the stop widens in volatile conditions instead of being hit by ordinary noise.
 * This returns the *level*; acting on it is the strategy's business.
 *
 * @param high       High prices.
 * @param low        Low prices.
 * @param close      Close prices.
 * @param period     Lookback for both the high and the ATR (e.g. 22).
 * @param multiplier ATR multiple (e.g. 3.0).
 * @return           Exit levels for a long position. result[0] corresponds to
 *                    data index (period - 1).
 */
[[nodiscard]] inline std::vector<double> chandelierExit(const std::vector<double>& high, const std::vector<double>& low,
                                                        const std::vector<double>& close, std::size_t period = 22,
                                                        double multiplier = 3.0) {
    if (period == 0 || high.size() != low.size() || high.size() != close.size() || close.size() < period) {
        return {};
    }

    const auto atrValues = atr(high, low, close, period);  // atrValues[0] is at data index (period - 1)
    if (atrValues.empty()) {
        return {};
    }

    std::vector<double> result;
    result.reserve(atrValues.size());

    for (std::size_t k = 0; k < atrValues.size(); ++k) {
        const std::size_t i  = k + period - 1;  // data index
        double            hh = high[i - period + 1];
        for (std::size_t j = i - period + 1; j <= i; ++j) {
            hh = std::max(hh, high[j]);
        }
        result.push_back(hh - multiplier * atrValues[k]);
    }

    return result;
}

/**
 * @brief Annualized historical volatility from log returns.
 *
 * Useful for sizing and for refusing to trade an instrument whose volatility has
 * moved outside the range a strategy was tuned for.
 *
 * @param prices          Close prices.
 * @param period          Lookback (e.g. 20).
 * @param periodsPerYear  252 for daily bars; for minute bars, bars per trading year.
 * @return                Annualized volatility in %. result[0] corresponds to data index period.
 */
[[nodiscard]] inline std::vector<double> historicalVolatility(const std::vector<double>& prices,
                                                              std::size_t period = 20, double periodsPerYear = 252.0) {
    if (period < 2 || prices.size() <= period) {
        return {};
    }

    std::vector<double> logReturns;
    logReturns.reserve(prices.size() - 1);
    for (std::size_t i = 1; i < prices.size(); ++i) {
        logReturns.push_back((prices[i - 1] > 0.0 && prices[i] > 0.0) ? std::log(prices[i] / prices[i - 1]) : 0.0);
    }

    std::vector<double> result;
    result.reserve(logReturns.size() - period + 1);

    for (std::size_t end = period; end <= logReturns.size(); ++end) {
        double sum = 0.0;
        for (std::size_t j = end - period; j < end; ++j) {
            sum += logReturns[j];
        }
        const double mean = sum / static_cast<double>(period);
        double       var  = 0.0;
        for (std::size_t j = end - period; j < end; ++j) {
            const double d = logReturns[j] - mean;
            var += d * d;
        }
        var /= static_cast<double>(period - 1);
        result.push_back(std::sqrt(var) * std::sqrt(periodsPerYear) * 100.0);
    }

    return result;
}

/**
 * @brief Ichimoku Cloud (일목균형표).
 *
 * Widely followed in Korean and Japanese markets, which matters in itself: the
 * levels attract orders because participants watch them. All series are returned
 * aligned to the input, with leading values left at 0 until they are defined,
 * because the components have different start points and the forward-shifted
 * spans have no natural trailing alignment.
 *
 * @param high        High prices.
 * @param low         Low prices.
 * @param close       Close prices.
 * @param conversion  Tenkan-sen period (9).
 * @param base        Kijun-sen period (26).
 * @param spanB       Senkou Span B period (52).
 * @return            Series the same length as the input; index i is data index i.
 *                     senkouA/senkouB are the cloud values *plotted at* i, i.e.
 *                     computed from data at (i - base), so they can be compared
 *                     with price at i without look-ahead.
 */
struct IchimokuResult {
    std::vector<double> tenkan;   ///< conversion line
    std::vector<double> kijun;    ///< base line
    std::vector<double> senkouA;  ///< leading span A, already shifted forward
    std::vector<double> senkouB;  ///< leading span B, already shifted forward
};

[[nodiscard]] inline IchimokuResult ichimoku(const std::vector<double>& high, const std::vector<double>& low,
                                             const std::vector<double>& close, std::size_t conversion = 9,
                                             std::size_t base = 26, std::size_t spanB = 52) {
    IchimokuResult    result;
    const std::size_t n = close.size();
    if (high.size() != n || low.size() != n || n == 0 || conversion == 0 || base == 0 || spanB == 0) {
        return result;
    }

    result.tenkan.assign(n, 0.0);
    result.kijun.assign(n, 0.0);
    result.senkouA.assign(n, 0.0);
    result.senkouB.assign(n, 0.0);

    // Midpoint of the high/low range over the last `window` bars ending at i.
    auto midpoint = [&](std::size_t i, std::size_t window) -> double {
        if (i + 1 < window) {
            return 0.0;
        }
        double hh = high[i - window + 1];
        double ll = low[i - window + 1];
        for (std::size_t j = i - window + 1; j <= i; ++j) {
            hh = std::max(hh, high[j]);
            ll = std::min(ll, low[j]);
        }
        return (hh + ll) / 2.0;
    };

    for (std::size_t i = 0; i < n; ++i) {
        result.tenkan[i] = midpoint(i, conversion);
        result.kijun[i]  = midpoint(i, base);
    }

    // The cloud is drawn `base` bars ahead, so the value sitting at i was computed
    // from bar (i - base) — which is exactly why it is usable without look-ahead.
    for (std::size_t i = base; i < n; ++i) {
        const std::size_t src = i - base;
        if (result.tenkan[src] > 0.0 && result.kijun[src] > 0.0) {
            result.senkouA[i] = (result.tenkan[src] + result.kijun[src]) / 2.0;
        }
        result.senkouB[i] = midpoint(src, spanB);
    }

    return result;
}

}  // namespace indicator