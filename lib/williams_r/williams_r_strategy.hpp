#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "strategy/istrategy.hpp"

/**
 * @brief Williams %R reversal strategy.
 *
 * Generates BUY when %R crosses up out of the oversold zone (default -80),
 * and SELL when %R crosses down out of the overbought zone (default -20).
 */
class WilliamsRStrategy: public IStrategy {
   public:
    explicit WilliamsRStrategy(std::size_t period = 14, double oversold = -80.0, double overbought = -20.0);

    [[nodiscard]] std::string name() const override;

    void init(const StockInfo& data) override;

    [[nodiscard]] std::size_t warmupPeriod() const override;

    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t period_;
    double      oversold_;
    double      overbought_;

    // willR_[0] corresponds to data index (period_ - 1).
    std::size_t         startIndex_ = 0;
    std::vector<double> willR_;
};
