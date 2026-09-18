#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "indicator.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief Moving-average slope trend strategy.
 *
 * BUY when the regression slope of a moving average rises above
 * `entryThreshold` (an uptrend has started) while ADX confirms the market is
 * actually trending (not choppy). SELL when the slope drops below
 * `exitThreshold`, which is set below `entryThreshold` (hysteresis) so the
 * position isn't churned every time the slope merely flattens near zero —
 * only a clear reversal closes it. Unlike the entry, exit has no ADX gate,
 * so a position can always be closed.
 */
class MaSlopeTrend: public IStrategy {
   public:
    explicit MaSlopeTrend(std::size_t maPeriod = 20, std::size_t slopeWindow = 10, double entryThreshold = 0.15,
                          double exitThreshold = -0.05, std::size_t adxPeriod = 14, double adxThreshold = 20.0);

    [[nodiscard]] std::string name() const override;

    void init(const StockInfo& data) override;

    [[nodiscard]] std::size_t warmupPeriod() const override;

    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t maPeriod_;
    std::size_t slopeWindow_;
    double      entryThreshold_;
    double      exitThreshold_;
    std::size_t adxPeriod_;
    double      adxThreshold_;

    // slope_[0] corresponds to data index (maPeriod_ + slopeWindow_ - 2);
    // dmi_.adx[0] corresponds to data index (2*adxPeriod_ - 1).
    std::size_t          slopeStartIndex_ = 0;
    std::size_t          adxStartIndex_   = 0;
    std::vector<double>  slope_;
    indicator::DmiResult dmi_;
};
