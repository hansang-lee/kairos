#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "indicator.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief Bollinger Bands Mean Reversion Strategy.
 *
 * Generates BUY when price crosses or bounces above the lower band (oversold),
 * and SELL when price touches or crosses above the upper band (overbought).
 */
class BollingerStrategy: public IStrategy {
   public:
    explicit BollingerStrategy(std::size_t period = 20, double numStdDev = 2.0);

    [[nodiscard]] std::string name() const override;

    void init(const StockInfo& data) override;

    [[nodiscard]] std::size_t warmupPeriod() const override;

    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t period_;
    double      numStdDev_;

    indicator::BollingerBands bands_;
    std::size_t               startIndex_ = 0;
};
