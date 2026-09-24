#include "backtest/backtest_engine.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

BacktestConfig BacktestConfig::forMarket(const std::string& market) {
    BacktestConfig cfg;
    if (market == "US") {
        cfg.commissionRate = 0.0025;     // 0.25% online, each side
        cfg.sellTaxRate    = 0.0000206;  // 0.00206% SEC fee, sells only
        cfg.slippagePct    = 0.0005;
    } else {
        cfg.commissionRate = 0.000177;  // 0.0140527% commission + ~0.0036% 유관기관수수료
        cfg.sellTaxRate    = 0.0020;    // 0.20% 증권거래세 + 농특세, sells only
        cfg.slippagePct    = 0.0005;
    }
    return cfg;
}

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
    double      buyPrice = 0.0;  ///< average entry price across every tranche bought
    std::size_t buyIdx   = 0;

    // Scaling state. A position is built over entryTranches buys and unwound over
    // exitTranches sells; adds triggered by drawdown are counted separately so
    // maxAdds bounds them independently of the planned entry.
    const int entryTranches = std::max(1, config.entryTranches);
    const int exitTranches  = std::max(1, config.exitTranches);
    int       entriesDone   = 0;
    int       exitsDone     = 0;
    int       addsDone      = 0;
    double    targetCapital = 0.0;  ///< cash earmarked for the whole position, fixed at first entry
    // Planned entries and drawdown adds share one budget, so enabling adds makes
    // each slice smaller rather than making the position bigger.
    const int totalSlices  = entryTranches + std::max(0, config.maxAdds);
    double    lastAddPrice = 0.0;  ///< price of the most recent buy, so adds step down rather than repeat

    // Equity curve for drawdown & sharpe calculation
    std::vector<double> equity;
    equity.reserve(n);

    double       peakEq   = initialCapital_;
    double       lowestEq = initialCapital_;
    std::int64_t peakTs   = data.timestamps.empty() ? 0 : data.timestamps.front();
    std::int64_t lowestTs = data.timestamps.empty() ? 0 : data.timestamps.front();

    for (std::size_t i = 0; i < n; ++i) {
        const double price          = data.close[i];
        bool         stoppedThisBar = false;

        // Stop-loss: if the day's low breached the stop, exit immediately — this takes
        // priority over the strategy's own signal, and must happen BEFORE this bar's
        // equity snapshot below, so mark-to-market/drawdown reflects the capped exit
        // rather than the (possibly much lower) close price on a gap-down day.
        if (inPos && config.stopLossPct > 0.0 && i > buyIdx && i < data.low.size()) {
            const double stopPrice = buyPrice * (1.0 - config.stopLossPct / 100.0);
            if (data.low[i] <= stopPrice) {
                const double effectiveSellPrice =
                    stopPrice * (1.0 - config.slippagePct) * (1.0 - config.commissionRate) * (1.0 - config.sellTaxRate);
                capital += shares * effectiveSellPrice;

                Trade trade;
                trade.buyIndex   = buyIdx;
                trade.sellIndex  = i;
                trade.buyPrice   = buyPrice;
                trade.sellPrice  = effectiveSellPrice;
                trade.returnPct  = (effectiveSellPrice - buyPrice) / buyPrice * 100.0;
                trade.stoppedOut = true;

                result.trades.push_back(trade);

                shares         = 0.0;
                inPos          = false;
                stoppedThisBar = true;
                entriesDone    = 0;
                exitsDone      = 0;
                addsDone       = 0;
            }
        }

        // Equity = idle cash (capital) + mark-to-market value of any open position.
        // capital already excludes allocCapital once a position is opened, so this
        // must be a sum, not a branch — otherwise a partial position (positionPct < 1.0)
        // makes the un-invested cash vanish from the equity curve while inPos is true.
        const double currentEquity = capital + (inPos ? (shares * price) : 0.0);
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

        if (i < warmup || stoppedThisBar) {
            continue;
        }

        const auto signal = strategy.evaluate(data, i);

        const double effectiveBuyPrice = price * (1.0 + config.slippagePct) * (1.0 + config.commissionRate);
        const double effectiveSellPrice =
            price * (1.0 - config.slippagePct) * (1.0 - config.commissionRate) * (1.0 - config.sellTaxRate);

        // Buying one tranche: the first fixes how much cash the whole position may
        // use, so later tranches cannot quietly grow it as the account moves.
        auto buyTranche = [&](int ofTranches) {
            // The budget a first tranche would fix, computed before anything is
            // committed: a buy that cannot afford a whole share must leave the
            // position untouched, and marking it open with nothing in it would
            // start a trade that never happened.
            const double budget = inPos ? targetCapital : capital * std::clamp(config.positionPct, 0.1, 1.0);
            const double slice  = std::min(budget / std::max(1, ofTranches), capital);
            if (slice <= 0.0) {
                return false;
            }
            // Whole shares only. A backtest that buys 3.7 shares is reporting a trade
            // nobody can place, and on a 10M account holding a 60,000-won ETF the
            // rounding is worth several percent of the position, not a rounding error.
            const double bought = std::floor(slice / effectiveBuyPrice);
            if (bought < 1.0) {
                return false;
            }
            const double spent = bought * effectiveBuyPrice;

            if (!inPos) {
                targetCapital = budget;
                buyIdx        = i;
                inPos         = true;
            }
            // Average entry price, which is what the stop and the trade record use.
            buyPrice = (shares + bought) > 0.0 ? (buyPrice * shares + spent) / (shares + bought) : effectiveBuyPrice;
            shares += bought;
            capital -= spent;
            lastAddPrice = effectiveBuyPrice;
            return true;
        };

        // A crossover strategy emits BUY on the transition only, so waiting for a
        // second BUY to place the second tranche would leave the position at a third
        // of its size forever. Once entered, the remaining tranches go in on
        // following bars for as long as the strategy has not called for an exit.
        const bool entering = (signal == Signal::BUY) || (inPos && signal != Signal::SELL);

        if (entering && entriesDone < entryTranches) {
            if (buyTranche(totalSlices)) {
                ++entriesDone;
            }
        } else if (inPos && config.addOnDrawdownPct > 0.0 && addsDone < config.maxAdds && lastAddPrice > 0.0
                   && price <= lastAddPrice * (1.0 - config.addOnDrawdownPct / 100.0)) {
            // Averaging down: measured from the last buy, not from the average, so a
            // position that keeps falling adds at intervals instead of all at once.
            if (buyTranche(totalSlices)) {
                ++addsDone;
            }
        } else if (signal == Signal::SELL && inPos) {
            // Unwind one tranche; the last one clears whatever is left so no dust
            // remains to be marked to market forever.
            const int remaining = std::max(1, exitTranches - exitsDone);
            // The final tranche takes whatever is left, so rounding cannot strand a
            // share that then sits marked to market for the rest of the run.
            const double sold = (remaining <= 1) ? shares : std::floor(shares / remaining);
            capital += sold * effectiveSellPrice;
            shares -= sold;
            ++exitsDone;

            if (shares <= 1e-9) {
                Trade trade;
                trade.buyIndex  = buyIdx;
                trade.sellIndex = i;
                trade.buyPrice  = buyPrice;
                trade.sellPrice = effectiveSellPrice;
                trade.returnPct = (effectiveSellPrice - buyPrice) / buyPrice * 100.0;
                result.trades.push_back(trade);

                shares      = 0.0;
                inPos       = false;
                entriesDone = 0;
                exitsDone   = 0;
                addsDone    = 0;
            }
        }
    }

    // If still in position at the end, close at last price
    if (inPos && !data.close.empty()) {
        const double lastPrice = data.close.back();
        const double effectiveSellPrice =
            lastPrice * (1.0 - config.slippagePct) * (1.0 - config.commissionRate) * (1.0 - config.sellTaxRate);
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

    result.equityCurve     = equity;
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
