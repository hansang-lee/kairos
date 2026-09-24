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
     * @brief An annual holding cost per asset NOT already in its price series.
     *
     * A listed fund's own fee is taken out of net asset value daily, so a price
     * series already carries it and charging it again here is double counting.
     * That is what this field was doing when it defaulted to 0.3%. It stays for the
     * costs a price cannot contain — an advisory fee, a wrap charge, or a synthetic
     * series built from an index rather than from a fund.
     *
     * Both markets' price series are total returns, so neither needs a fee here and
     * neither is missing its payouts. Yahoo supplies the adjusted series for the US
     * funds, and KIS's 수정주가 turned out to carry 분배금 already — checked against
     * Yahoo's dividend-adjusted close on five KRX funds over eleven years, matching
     * to within 1.25% while the raw series differed by up to 19%.
     *
     * Indexed by asset, in the order of PortfolioData::tickers. Entries past the
     * end of the vector use `defaultExpenseRatio`.
     */
    std::vector<double> expenseRatios;
    double              defaultExpenseRatio = 0.0;

    /** @brief Trading bars in a year, used to prorate the annual rates above. */
    double barsPerYear = 252.0;

    /**
     * @brief Gross exposure ceiling as a multiple of equity. 1.0 is unlevered.
     *
     * A strategy whose drawdown is a third of the market's has bought safety it
     * may not want; borrowing is how that safety is traded back for return. The
     * reason it must be modelled rather than multiplied in afterwards is that
     * leverage is path dependent: doubling the exposure does not double the
     * return, it doubles the drawdown, pays interest along the way, and turns a
     * decline that was merely painful into one the account does not come back
     * from.
     */
    double maxLeverage = 1.0;

    /**
     * @brief Annual interest on a negative cash balance.
     *
     * KRX 신용융자 runs around 5-9% a year depending on the broker and the term.
     * A backtest that borrows for nothing is measuring a product nobody sells.
     */
    double marginRateAnnual = 0.0;

    /**
     * @brief Gross exposure at which the broker forces a sale, as a multiple of equity.
     *
     * Above it the engine cuts the position back to `maxLeverage` at that bar's
     * prices, which is what a margin call does: it sells the most after prices
     * have already fallen. Zero disables the check, which models an investor who
     * is never called — worth running to see how much of leverage's cost is the
     * call rather than the interest.
     */
    double marginCallLeverage = 0.0;
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

    /** @brief Interest paid on borrowed cash over the run. */
    double totalInterest = 0.0;

    /** @brief Times the position had to be cut back to satisfy the exposure limit. */
    std::size_t marginCalls = 0;

    /**
     * @brief Whether equity reached zero, ending the run there.
     *
     * A levered account can be wiped out, and a backtest that lets it go negative
     * and recover is describing a loan nobody would extend. Any other metric on a
     * ruined result is about the stretch before the wipeout, not about a strategy
     * anyone could have held.
     */
    bool ruined = false;

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
