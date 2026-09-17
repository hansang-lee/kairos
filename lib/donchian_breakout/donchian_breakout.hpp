#pragma once

#include <cstddef>
#include <string>

#include "indicator.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief Donchian Channel breakout strategy (turtle-trading style).
 *
 * Generates BUY when the close breaks above the prior N-day high channel,
 * and SELL when it breaks below the prior N-day low channel.
 */
class DonchianBreakout: public IStrategy {
   public:
    explicit DonchianBreakout(std::size_t period = 20);

    [[nodiscard]] std::string name() const override;

    void init(const StockInfo& data) override;

    [[nodiscard]] std::size_t warmupPeriod() const override;

    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t period_;

    // result_.upper[0]/lower[0] correspond to data index (period_ - 1).
    std::size_t               startIndex_ = 0;
    indicator::DonchianResult result_;
};
