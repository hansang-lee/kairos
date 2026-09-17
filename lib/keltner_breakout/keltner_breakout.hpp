#pragma once

#include <cstddef>
#include <string>

#include "indicator.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief Keltner Channel volatility breakout strategy.
 *
 * Generates BUY when the close crosses above the upper Keltner band, and
 * SELL when it crosses below the lower band.
 */
class KeltnerBreakout: public IStrategy {
   public:
    explicit KeltnerBreakout(std::size_t emaPeriod = 20, std::size_t atrPeriod = 10, double multiplier = 2.0);

    [[nodiscard]] std::string name() const override;

    void init(const StockInfo& data) override;

    [[nodiscard]] std::size_t warmupPeriod() const override;

    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t emaPeriod_;
    std::size_t atrPeriod_;
    double      multiplier_;

    // result_.upper[0] corresponds to data index max(emaPeriod_ - 1, atrPeriod_ - 1).
    std::size_t              startIndex_ = 0;
    indicator::KeltnerResult result_;
};
