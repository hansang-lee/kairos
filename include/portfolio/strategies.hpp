#pragma once

#include <cstddef>
#include <memory>
#include <string>
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
 * @brief The set of allocation strategies the tools compare against each other.
 *
 * One list, so the sweep, the rebalance study and the rolling-window study are
 * all talking about the same strategies. When they each built their own, a
 * parameter changed in one place made two reports silently incomparable.
 */
[[nodiscard]] std::vector<std::unique_ptr<IPortfolioStrategy>> standardStrategySet();

}  // namespace portfolio
