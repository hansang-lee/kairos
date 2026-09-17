#pragma once

#include <cstddef>
#include <string>

#include "indicator.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief Aroon Up/Down trend-following strategy.
 *
 * Generates BUY when Aroon Up crosses above Aroon Down above the strength
 * threshold, and SELL on the opposite crossover.
 */
class AroonTrend: public IStrategy {
   public:
    explicit AroonTrend(std::size_t period = 25, double strengthThreshold = 70.0);

    [[nodiscard]] std::string name() const override;

    void init(const StockInfo& data) override;

    [[nodiscard]] std::size_t warmupPeriod() const override;

    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t period_;
    double      strengthThreshold_;

    // result_.up[0]/down[0] correspond to data index period_.
    std::size_t            startIndex_ = 0;
    indicator::AroonResult result_;
};
