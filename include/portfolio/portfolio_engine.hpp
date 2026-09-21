#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "portfolio/iportfolio_strategy.hpp"

namespace portfolio {

struct PortfolioConfigBt {
    double commissionRate = 0.000177;  ///< per side, KRX online
    double slippagePct    = 0.0005;
    double sellTaxRate    = 0.0020;  ///< 증권거래세 + 농특세, sells only

    /**
     * @brief Bars between rebalances; 0 lets the strategy act on every bar.
     *
     * Rebalancing is where these strategies spend money: every adjustment pays
     * the round trip on the part that moved. Monthly (about 21 daily bars) is the
     * usual compromise between tracking the target and paying to.
     */
    int rebalanceEveryBars = 21;

    /**
     * @brief Smallest weight change worth trading, as a fraction.
     *
     * Without it a target that drifts by a hundredth of a percent triggers an
     * order, and the costs of chasing the target exceed the benefit of hitting it.
     */
    double minWeightChange = 0.02;
};

struct PortfolioResult {
    std::string strategyName;

    double initialCapital = 0.0;
    double finalCapital   = 0.0;
    double totalReturnPct = 0.0;
    double cagr           = 0.0;
    double maxDrawdownPct = 0.0;
    double sharpeRatio    = 0.0;

    std::size_t rebalances = 0;
    std::size_t orders     = 0;    ///< individual asset adjustments, each paying costs
    double      totalCosts = 0.0;  ///< commission, slippage and tax actually paid

    std::vector<double> equityCurve;
};

/**
 * @brief Backtests a strategy that allocates one account across several assets.
 *
 * Separate from BacktestEngine rather than an extension of it: that one models a
 * single position with a stop, and the questions here are different ones —
 * what fraction sits in each asset, how often that is adjusted, and what the
 * adjusting costs.
 */
class PortfolioEngine {
   public:
    explicit PortfolioEngine(double initialCapital = 10000000.0);

    [[nodiscard]] PortfolioResult run(IPortfolioStrategy& strategy, const PortfolioData& data,
                                      const PortfolioConfigBt& config = {});

   private:
    double initialCapital_;
};

/** @brief Equal-weight buy-and-hold over the same assets, as the benchmark to clear. */
[[nodiscard]] PortfolioResult buyAndHold(const PortfolioData& data, double initialCapital = 10000000.0);

}  // namespace portfolio
