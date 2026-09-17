#pragma once

#include <cstddef>
#include <string>

#include "indicator.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief SuperTrend trend-following strategy.
 *
 * Generates BUY when the SuperTrend line flips from downtrend to uptrend,
 * and SELL on the opposite flip.
 */
class SuperTrendFollow: public IStrategy {
   public:
    explicit SuperTrendFollow(std::size_t period = 10, double multiplier = 3.0);

    [[nodiscard]] std::string name() const override;

    void init(const StockInfo& data) override;

    [[nodiscard]] std::size_t warmupPeriod() const override;

    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t period_;
    double      multiplier_;

    // result_.value[0]/trend[0] correspond to data index (period_ - 1).
    std::size_t                 startIndex_ = 0;
    indicator::SuperTrendResult result_;
};
