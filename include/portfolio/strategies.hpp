#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "portfolio/iportfolio_strategy.hpp"

namespace portfolio {

/**
 * @brief Hold the strongest few assets by trailing return, in equal weight.
 *
 * Cross-sectional momentum: rank the universe and own the leaders. The oldest
 * documented anomaly that has kept working out of sample, and unavailable to a
 * single-ticker strategy, which has nothing to rank against.
 *
 * An absolute-return floor turns it defensive. Without one the strategy always
 * holds its `topN`, including in a decline where every one of them is falling
 * and "strongest" only means least bad.
 */
class MomentumRotation: public IPortfolioStrategy {
   public:
    /**
     * @param topN       Assets held at a time.
     * @param lookback   Bars of trailing return used to rank.
     * @param minReturnPct Return an asset must clear to be held at all; assets
     *                     below it are left in cash.
     */
    explicit MomentumRotation(std::size_t topN = 3, std::size_t lookback = 126, double minReturnPct = 0.0);

    [[nodiscard]] std::string         name() const override;
    void                              init(const PortfolioData& data) override;
    [[nodiscard]] std::size_t         warmupPeriod() const override;
    [[nodiscard]] std::vector<double> targetWeights(const PortfolioData& data, std::size_t index) override;

   private:
    std::size_t topN_;
    std::size_t lookback_;
    double      minReturnPct_;
};

/**
 * @brief Weight each asset by the inverse of its volatility.
 *
 * Equal weight is not equal risk: in a basket holding both a bond fund and a
 * leveraged equity one, the equity sleeve supplies nearly all the movement, and
 * the portfolio is an equity portfolio wearing a diversified label. Scaling by
 * 1/volatility gives each asset a comparable say in the outcome, which is what
 * makes the combined drawdown smaller than the parts.
 */
class RiskParity: public IPortfolioStrategy {
   public:
    /**
     * @param lookback   Bars of returns used to measure volatility.
     * @param maxWeight  Ceiling per asset, so a very quiet one cannot become the
     *                   whole portfolio through arithmetic alone.
     */
    explicit RiskParity(std::size_t lookback = 63, double maxWeight = 0.4);

    [[nodiscard]] std::string         name() const override;
    void                              init(const PortfolioData& data) override;
    [[nodiscard]] std::size_t         warmupPeriod() const override;
    [[nodiscard]] std::vector<double> targetWeights(const PortfolioData& data, std::size_t index) override;

   private:
    std::size_t lookback_;
    double      maxWeight_;
};

/** @brief Equal weight across every available asset — the benchmark to beat. */
class EqualWeight: public IPortfolioStrategy {
   public:
    [[nodiscard]] std::string         name() const override { return "Equal weight (rebalanced)"; }
    void                              init(const PortfolioData&) override {}
    [[nodiscard]] std::size_t         warmupPeriod() const override { return 1; }
    [[nodiscard]] std::vector<double> targetWeights(const PortfolioData& data, std::size_t index) override;
};

/**
 * @brief Split the account evenly between asset classes, then within each one.
 *
 * Equal weight over a ticker list allocates to however many ways a bet happens to
 * be listed, not to the bets. In this universe that put 59% of the book in Korean
 * equity — six listings of KOSPI 200 plus eleven sector funds that are pieces of
 * it — while the three bond funds got 8.8%. Grouping first makes the split a
 * decision rather than an artefact of the list, and it does it without a single
 * fitted parameter, so there is nothing here to overfit to a period.
 *
 * Classes with no asset available at a bar are skipped and their share is spread
 * over the rest, so a class that lists late does not leave the book in cash.
 */
class GroupParity: public IPortfolioStrategy {
   public:
    /**
     * @param assetClasses One label per asset, in the order of PortfolioData::tickers.
     * @param inverseVolWithin When set, assets inside a class are weighted by the
     *        inverse of their volatility rather than equally — the class split
     *        stays fixed either way, so this only changes which names carry it.
     * @param lookback Bars of returns used for that volatility.
     * @param classWeights How much of the account each class gets, by label. Empty
     *        splits evenly, which is the version with no fitted parameter.
     *
     *        Spelling the split out matters more than it looks. Across ten ways of
     *        drawing the class lines over this universe the strategy's Sharpe ran
     *        from 0.71 to 1.11, and that spread correlates 0.89 with one number:
     *        how much of the book ended up outside equity. Redrawing the taxonomy
     *        was never a modelling choice, it was a way of setting the equity share
     *        without admitting to setting it. This parameter admits it.
     */
    explicit GroupParity(std::vector<std::string> assetClasses, bool inverseVolWithin = false,
                         std::size_t lookback = 63, std::vector<std::pair<std::string, double>> classWeights = {});

    [[nodiscard]] std::string         name() const override;
    void                              init(const PortfolioData& data) override;
    [[nodiscard]] std::size_t         warmupPeriod() const override;
    [[nodiscard]] std::vector<double> targetWeights(const PortfolioData& data, std::size_t index) override;

   private:
    std::vector<std::string>                     classes_;
    bool                                         inverseVolWithin_;
    std::size_t                                  lookback_;
    std::vector<std::pair<std::string, double>>  classWeights_;
};

/**
 * @brief Hold an allocation only while the asset is above its own trailing return.
 *
 * Relative ranking always holds something: in a decline where everything is
 * falling, "strongest" means least bad, and the book is fully invested into it.
 * An absolute floor is what turns a ranking rule into one that can stand aside,
 * and it is the oldest rule that has actually reduced drawdown out of sample.
 *
 * Assets failing the test are dropped and their weight is left in cash rather
 * than redistributed, because redistributing it would reinvest the money the rule
 * just decided not to risk.
 */
class AbsoluteMomentumFilter: public IPortfolioStrategy {
   public:
    /**
     * @param inner The allocation rule being filtered.
     * @param lookback Bars of trailing return the test is made over.
     * @param minReturnPct The return an asset must clear to be held at all.
     */
    AbsoluteMomentumFilter(std::unique_ptr<IPortfolioStrategy> inner, std::size_t lookback = 252,
                           double minReturnPct = 0.0);

    [[nodiscard]] std::string         name() const override;
    void                              init(const PortfolioData& data) override;
    [[nodiscard]] std::size_t         warmupPeriod() const override;
    [[nodiscard]] std::vector<double> targetWeights(const PortfolioData& data, std::size_t index) override;

   private:
    std::unique_ptr<IPortfolioStrategy> inner_;
    std::size_t                         lookback_;
    double                              minReturnPct_;
};

/**
 * @brief Any allocation rule, with every weight multiplied by a constant.
 *
 * Leverage is a property of the account, not of the allocation rule: the decision
 * to hold twice as much of what a strategy picked is separable from the picking.
 * Wrapping rather than parameterising each strategy keeps it that way, and means
 * a levered result and its unlevered twin are the same rule measured twice.
 *
 * The engine still caps gross exposure at its own ceiling, so a multiple above
 * `PortfolioConfigBt::maxLeverage` is trimmed rather than honoured.
 */
class Levered: public IPortfolioStrategy {
   public:
    Levered(std::unique_ptr<IPortfolioStrategy> inner, double multiple);

    [[nodiscard]] std::string         name() const override;
    void                              init(const PortfolioData& data) override;
    [[nodiscard]] std::size_t         warmupPeriod() const override;
    [[nodiscard]] std::vector<double> targetWeights(const PortfolioData& data, std::size_t index) override;

   private:
    std::unique_ptr<IPortfolioStrategy> inner_;
    double                              multiple_;
};

/**
 * @brief The set of allocation strategies the tools compare against each other.
 *
 * One list, so the sweep, the rebalance study and the rolling-window study are
 * all talking about the same strategies. When they each built their own, a
 * parameter changed in one place made two reports silently incomparable.
 */
[[nodiscard]] std::vector<std::unique_ptr<IPortfolioStrategy>> standardStrategySet(
    const std::vector<std::string>& assetClasses = {});

}  // namespace portfolio
