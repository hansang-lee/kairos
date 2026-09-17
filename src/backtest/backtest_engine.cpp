#include "backtest/backtest_engine.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

BacktestEngine::BacktestEngine(double initialCapital)
    : initialCapital_(initialCapital) {}

BacktestResult BacktestEngine::run(IStrategy& strategy, const StockInfo& data) {
    BacktestConfig defaultConfig;
    return run(strategy, data, defaultConfig);
}

BacktestResult BacktestEngine::run(IStrategy& strategy, const StockInfo& data, const BacktestConfig& config) {
    BacktestResult result;
    result.ticker         = data.ticker;
    result.strategyName   = strategy.name();
    result.initialCapital = initialCapital_;

    if (data.close.empty()) {
        result.finalCapital  = initialCapital_;
        result.peakCapital   = initialCapital_;
        result.lowestCapital = initialCapital_;
        return result;
    }

    // Initialize strategy (precompute indicators)
    strategy.init(data);

    const auto warmup = strategy.warmupPeriod();
    const auto n      = data.close.size();

    // Simulation state
    double      capital  = initialCapital_;
    double      shares   = 0.0;
    bool        inPos    = false;
    double      buyPrice = 0.0;
    std::size_t buyIdx   = 0;

    // Equity curve for drawdown & sharpe calculation
    std::vector<double> equity;
    equity.reserve(n);

    double       peakEq   = initialCapital_;
    double       lowestEq = initialCapital_;
    std::int64_t peakTs   = data.timestamps.empty() ? 0 : data.timestamps.front();
    std::int64_t lowestTs = data.timestamps.empty() ? 0 : data.timestamps.front();

    for (std::size_t i = 0; i < n; ++i) {
        const double price         = data.close[i];
        const double currentEquity = inPos ? (shares * price) : capital;
        equity.push_back(currentEquity);

        const std::int64_t ts = (i < data.timestamps.size()) ? data.timestamps[i] : 0;
        if (currentEquity > peakEq) {
            peakEq = currentEquity;
            peakTs = ts;
        }
        if (currentEquity < lowestEq) {
            lowestEq = currentEquity;
            lowestTs = ts;
        }

        if (i < warmup) {
            continue;
        }

        const auto signal = strategy.evaluate(data, i);

        if (signal == Signal::BUY && !inPos) {
            // Buy: apply commission & slippage to entry price
            const double effectiveBuyPrice = price * (1.0 + config.slippagePct) * (1.0 + config.commissionRate);
            const double allocCapital      = capital * std::clamp(config.positionPct, 0.1, 1.0);
            shares                         = allocCapital / effectiveBuyPrice;
            buyPrice                       = effectiveBuyPrice;
            buyIdx                         = i;
            inPos                          = true;
            capital -= allocCapital;
        } else if (signal == Signal::SELL && inPos) {
            // Sell: apply commission & slippage to exit price
            const double effectiveSellPrice = price * (1.0 - config.slippagePct) * (1.0 - config.commissionRate);
            capital += shares * effectiveSellPrice;

            Trade trade;
            trade.buyIndex  = buyIdx;
            trade.sellIndex = i;
            trade.buyPrice  = buyPrice;
            trade.sellPrice = effectiveSellPrice;
            trade.returnPct = (effectiveSellPrice - buyPrice) / buyPrice * 100.0;

            result.trades.push_back(trade);

            shares = 0.0;
            inPos  = false;
        }
    }

    // If still in position at the end, close at last price
    if (inPos && !data.close.empty()) {
        const double lastPrice          = data.close.back();
        const double effectiveSellPrice = lastPrice * (1.0 - config.slippagePct) * (1.0 - config.commissionRate);
        capital += shares * effectiveSellPrice;

        Trade trade;
        trade.buyIndex  = buyIdx;
        trade.sellIndex = n - 1;
        trade.buyPrice  = buyPrice;
        trade.sellPrice = effectiveSellPrice;
        trade.returnPct = (effectiveSellPrice - buyPrice) / buyPrice * 100.0;

        result.trades.push_back(trade);

        shares = 0.0;
        inPos  = false;
    }

    result.finalCapital    = capital;
    result.peakCapital     = peakEq;
    result.peakTimestamp   = peakTs;
    result.lowestCapital   = lowestEq;
    result.lowestTimestamp = lowestTs;

    // --- Compute metrics ---

    // 1. Total Return
    result.totalReturnPct = (result.finalCapital - initialCapital_) / initialCapital_ * 100.0;

    // 2. CAGR (Compound Annual Growth Rate)
    if (data.timestamps.size() >= 2) {
        const double totalSeconds = static_cast<double>(data.timestamps.back() - data.timestamps.front());
        const double totalYears   = totalSeconds / (365.25 * 86400.0);
        if (totalYears > 0.01 && result.finalCapital > 0.0) {
            result.cagr = (std::pow(result.finalCapital / initialCapital_, 1.0 / totalYears) - 1.0) * 100.0;
        }
    }

    // 3. Win Rate & Profit Factor
    if (!result.trades.empty()) {
        std::size_t wins      = 0;
        double      grossWins = 0.0;
        double      grossLoss = 0.0;

        for (const auto& t : result.trades) {
            if (t.returnPct > 0.0) {
                wins++;
                grossWins += t.returnPct;
            } else {
                grossLoss += std::abs(t.returnPct);
            }
        }
        result.winRate      = static_cast<double>(wins) / static_cast<double>(result.trades.size());
        result.profitFactor = (grossLoss > 1e-9) ? (grossWins / grossLoss) : (grossWins > 0 ? 99.99 : 0.0);
    }

    // 4. Max Drawdown
    if (!equity.empty()) {
        double peak  = equity[0];
        double maxDD = 0.0;
        for (const auto& eq : equity) {
            peak            = std::max(peak, eq);
            const double dd = (eq - peak) / peak * 100.0;
            maxDD           = std::min(maxDD, dd);
        }
        result.maxDrawdownPct = maxDD;
    }

    // 5. Sharpe Ratio (annualized, assuming daily data, risk-free = 0)
    if (equity.size() > 1) {
        std::vector<double> dailyReturns;
        dailyReturns.reserve(equity.size() - 1);
        for (std::size_t i = 1; i < equity.size(); ++i) {
            if (equity[i - 1] > 0.0) {
                dailyReturns.push_back((equity[i] - equity[i - 1]) / equity[i - 1]);
            }
        }

        if (!dailyReturns.empty()) {
            const double mean = std::accumulate(dailyReturns.begin(), dailyReturns.end(), 0.0)
                              / static_cast<double>(dailyReturns.size());

            double variance = 0.0;
            for (const auto& r : dailyReturns) {
                variance += (r - mean) * (r - mean);
            }
            variance /= static_cast<double>(dailyReturns.size());

            const double stdDev = std::sqrt(variance);
            if (stdDev > 1e-12) {
                // Annualize: multiply by sqrt(252 trading days)
                result.sharpeRatio = (mean / stdDev) * std::sqrt(252.0);
            }
        }
    }

    // 6. Composite Score
    result.score =
        computeScore(result.totalReturnPct, result.winRate, result.maxDrawdownPct, result.sharpeRatio, result.cagr);

    return result;
}

double BacktestEngine::computeScore(double totalReturnPct, double winRate, double maxDrawdownPct, double sharpeRatio,
                                    double cagr) {
    // Total Return: clamp [-50, 100], map to [0, 1]
    const double retNorm = std::clamp((totalReturnPct + 50.0) / 150.0, 0.0, 1.0);

    // Win Rate: [0, 1]
    const double wrNorm = std::clamp(winRate, 0.0, 1.0);

    // Max Drawdown: range [-50, 0], lower is worse
    const double mddNorm = std::clamp(1.0 + (maxDrawdownPct / 50.0), 0.0, 1.0);

    // Sharpe Ratio: clamp [-1, 3], map to [0, 1]
    const double sharpeNorm = std::clamp((sharpeRatio + 1.0) / 4.0, 0.0, 1.0);

    // CAGR: clamp [-20, 40], map to [0, 1]
    const double cagrNorm = std::clamp((cagr + 20.0) / 60.0, 0.0, 1.0);

    // Weighted sum: Total Return(30%), MDD(25%), Sharpe(20%), WinRate(15%), CAGR(10%)
    const double weighted =
        (retNorm * 0.30) + (mddNorm * 0.25) + (sharpeNorm * 0.20) + (wrNorm * 0.15) + (cagrNorm * 0.10);

    return std::clamp(weighted * 100.0, 0.0, 100.0);
}
