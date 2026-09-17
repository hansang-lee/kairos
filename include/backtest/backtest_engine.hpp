#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "strategy/istrategy.hpp"

struct Trade {
    std::size_t buyIndex  = 0;
    std::size_t sellIndex = 0;
    double      buyPrice  = 0.0;
    double      sellPrice = 0.0;
    double      returnPct = 0.0;  // (sellPrice - buyPrice) / buyPrice * 100
};

struct BacktestConfig {
    double commissionRate    = 0.00015;  // 0.015% 수수료
    double slippagePct       = 0.001;    // 0.1% 슬리피지
    double positionPct       = 1.0;      // 포지션 비율 (1.0 = 전액)
    bool   reinvestDividends = false;
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
