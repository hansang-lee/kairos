#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "strategy/istrategy.hpp"

struct Trade {
    std::size_t buyIndex   = 0;
    std::size_t sellIndex  = 0;
    double      buyPrice   = 0.0;
    double      sellPrice  = 0.0;
    double      returnPct  = 0.0;    // (sellPrice - buyPrice) / buyPrice * 100
    bool        stoppedOut = false;  // true if this exit was forced by BacktestConfig::stopLossPct
};

struct BacktestConfig {
    double commissionRate    = 0.00015;  // brokerage commission, charged on both sides
    double slippagePct       = 0.001;    // 0.1% slippage
    double positionPct       = 1.0;      // position fraction (1.0 = full account)
    bool   reinvestDividends = false;

    // Sell-side-only cost: Korean securities transaction tax, or the US SEC fee.
    // Large enough to decide whether a high-frequency strategy is viable at all,
    // so it is modelled separately rather than folded into the commission.
    double sellTaxRate = 0.0;

    /* ----- Scaling in and out (1 = the whole position at once) -----
     *
     * These existed in live trading long before the engine could model them, so
     * every backtest run before this was of a strategy the trader was not
     * actually running. With both at 1 and addOnDrawdownPct at 0, the engine
     * behaves exactly as it did.
     */
    int entryTranches = 1;  ///< buy the target position over this many orders
    int exitTranches  = 1;  ///< sell it over this many, except on a forced exit

    /**
     * @brief Buy another tranche once the position is this far underwater.
     *
     * Averaging down. It raises the win rate — most dips do recover — and pays
     * for that with the ones that do not, by adding exposure precisely when the
     * reason for the trade is being disproved. maxAdds bounds how far that can
     * go; without a bound a single position can absorb the account.
     *
     * 0 disables it, which is the default.
     */
    double addOnDrawdownPct = 0.0;
    int    maxAdds          = 0;  ///< cap on adds per position; 0 means none are allowed

    /**
     * @brief Real-world costs for a market, as charged by KIS (checked 2026-09).
     *
     * KRX: 뱅키스 online commission 0.0140527% + 유관기관수수료 ~0.0036%, and a
     * 0.20% transaction tax on sells (KOSPI 0.05% + 농특세 0.15%; KOSDAQ 0.20%),
     * which rose from 0.18% on 2026-01-01.
     * US: 0.25% online commission per side plus a 0.00206% SEC fee on sells —
     * more than twice the KRX round trip, because of the commission.
     *
     * @param market "KRX" or "US".
     */
    [[nodiscard]] static BacktestConfig forMarket(const std::string& market);
    // Stop-loss (%, 0 = disabled). If the day's low falls this far below the entry
    // price, the position is closed that same bar without waiting for a SELL signal.
    double stopLossPct = 0.0;
};

struct BacktestResult {
    std::string ticker;
    std::string strategyName;

    double initialCapital = 0.0;
    double finalCapital   = 0.0;

    /* ----- Key Metrics ----- */
    double       totalReturnPct  = 0.0;  // Total return percentage
    double       cagr            = 0.0;  // Compound Annual Growth Rate (%)
    double       peakCapital     = 0.0;  // Highest capital during backtest
    std::int64_t peakTimestamp   = 0;    // Timestamp of peak capital
    double       lowestCapital   = 0.0;  // Lowest capital during backtest
    std::int64_t lowestTimestamp = 0;    // Timestamp of lowest capital
    double       maxDrawdownPct  = 0.0;  // Maximum drawdown percentage (negative)
    double       winRate         = 0.0;  // Winning trades / Total trades (0~1)
    double       profitFactor    = 0.0;  // Gross profit / Gross loss
    double       sharpeRatio     = 0.0;  // Annualized Sharpe ratio

    /* ----- Composite Score (0~100) ----- */
    double score = 0.0;

    /* ----- Trade History ----- */
    std::vector<Trade> trades;
};

/**
 * @brief Backtesting engine that simulates a strategy over historical data.
 *
 * Runs a strategy against StockInfo, tracks trades and portfolio equity,
 * then computes performance metrics and a composite score.
 */
class BacktestEngine {
   public:
    /**
     * @param initialCapital Starting capital for the simulation (default: $10,000).
     */
    explicit BacktestEngine(double initialCapital = 10000.0);

    /**
     * @brief Run the backtest with default configuration.
     * @param strategy The investment strategy to evaluate.
     * @param data     Historical stock data.
     * @return BacktestResult with all performance metrics and trade list.
     */
    [[nodiscard]] BacktestResult run(IStrategy& strategy, const StockInfo& data);

    /**
     * @brief Run the backtest with custom configuration.
     * @param strategy The investment strategy to evaluate.
     * @param data     Historical stock data.
     * @param config   Backtest execution configuration (commission, slippage, etc.)
     * @return BacktestResult with all performance metrics and trade list.
     */
    [[nodiscard]] BacktestResult run(IStrategy& strategy, const StockInfo& data, const BacktestConfig& config);

   private:
    double initialCapital_;

    /**
     * @brief Compute composite score from individual metrics.
     *
     * Weights: TotalReturn(35%), MDD(30%), Sharpe(20%), WinRate(15%)
     */
    [[nodiscard]] static double computeScore(double totalReturnPct, double winRate, double maxDrawdownPct,
                                             double sharpeRatio, double cagr);
};
