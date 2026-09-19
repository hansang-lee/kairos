#pragma once

#include <cstddef>
#include <string>

#include "indicator.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief Ichimoku cloud breakout (일목균형표).
 *
 * Buys when price closes above the cloud with the conversion line above the base
 * line, and sells when price falls back into or below it. The cloud is already
 * shifted forward by the indicator, so the value compared against price at a bar
 * was computed from data well before it — no look-ahead.
 */
class IchimokuTrend: public IStrategy {
   public:
    explicit IchimokuTrend(std::size_t conversion = 9, std::size_t base = 26, std::size_t spanB = 52);

    [[nodiscard]] std::string name() const override;
    void                      init(const StockInfo& data) override;
    [[nodiscard]] std::size_t warmupPeriod() const override;
    [[nodiscard]] Signal      evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t conversion_;
    std::size_t base_;
    std::size_t spanB_;

    indicator::IchimokuResult ichimoku_;  ///< aligned 1:1 with the input series
};
