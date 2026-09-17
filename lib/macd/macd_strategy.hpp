#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "indicator.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief MACD Trend Following Strategy.
 *
 * Generates BUY when MACD line crosses above Signal line,
 * and SELL when MACD line crosses below Signal line.
 */
class MacdStrategy: public IStrategy {
   public:
    explicit MacdStrategy(std::size_t fastPeriod = 12, std::size_t slowPeriod = 26, std::size_t signalPeriod = 9);

    [[nodiscard]] std::string name() const override;

    void init(const StockInfo& data) override;

    [[nodiscard]] std::size_t warmupPeriod() const override;

    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t fastPeriod_;
    std::size_t slowPeriod_;
    std::size_t signalPeriod_;

    indicator::MacdResult macdResult_;
    std::size_t           startIndex_ = 0;
};
