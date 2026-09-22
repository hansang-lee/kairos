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
     *
     * It gates drift only. Opening and closing a position are the allocation
     * itself, not drift around it, and are never vetoed by this band — otherwise a
     * universe whose target weights all sit below it would never be bought at all.
     */
    double minWeightChange = 0.02;

    /**
     * @brief Drift worth correcting, as a fraction of the position itself.
     *
     * `minWeightChange` measures error against the whole account, which leaves a
     * small position unmaintained: a 0.5% target may drift to 2.4% before it
     * breaches a 2%-of-equity band, by which point it is five times its intended
     * size. This band asks the same question in the position's own terms, and a
     * trade happens when either one is breached.
     */
    double minPositionDrift = 0.25;

    /**
     * @brief Annual expense ratio per asset, as a fraction (0.005 = 0.5% a year).
     *
     * A fund's fee is taken out of its net asset value daily, so it never appears
     * as a transaction and no amount of trading discipline avoids it. Left out of
     * a backtest it silently flatters every buy-and-hold result and every strategy
     * that stays invested, by one to three percentage points over a decade.
     *
     * Indexed by asset, in the order of PortfolioData::tickers. Entries past the
     * end of the vector use `defaultExpenseRatio`.
     */
    std::vector<double> expenseRatios;
    double              defaultExpenseRatio = 0.0;

    /** @brief Trading bars in a year, used to prorate the annual rates above. */
    double barsPerYear = 252.0;
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

    /**
     * @brief Management fees surrendered to the funds over the run.
     *
     * Kept apart from totalCosts because the two answer different questions:
     * totalCosts is what trading decisions cost and can be reduced by trading
     * less, this is what holding costs and cannot.
     */
    double totalFees = 0.0;

    /**
     * @brief Bars at the start of the run during which nothing was held.
     *
     * The strategy cannot allocate before it has the history it ranks or measures
     * on, so the first `warmupBars` of equityCurve are flat at the initial capital.
     * That stretch is an absence of a result, not a result of zero: anything
     * reading returns out of the curve has to skip it, or it reports "was not
     * invested" as "did not lose".
     */
    std::size_t warmupBars = 0;

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

/**
 * @brief Equal-weight buy-and-hold over the same assets, as the benchmark to clear.
 *
 * Takes the same config as PortfolioEngine::run and pays the same entry, exit and
 * management costs. A benchmark held to a cheaper cost model than the strategies
 * measured against it is not a benchmark; it is a handicap of unknown size.
 */
[[nodiscard]] PortfolioResult buyAndHold(const PortfolioData& data, const PortfolioConfigBt& config = {},
                                         double initialCapital = 10000000.0);

/**
 * @brief The annual expense ratio applying to asset `index`, from a config.
 *
 * One place decides what an unlisted asset costs, so the engine, the benchmark
 * and the tests cannot disagree about it.
 */
[[nodiscard]] double expenseRatioFor(const PortfolioConfigBt& config, std::size_t index);

}  // namespace portfolio
