#pragma once

#include <cstddef>
#include <string>

#include "indicator.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief ADX/DMI trend-following strategy.
 *
 * Generates BUY when +DI crosses above -DI while ADX confirms a strong
 * trend (>= adxThreshold), and SELL on the opposite +DI/-DI crossover.
 */
class AdxTrend: public IStrategy {
   public:
    explicit AdxTrend(std::size_t period = 14, double adxThreshold = 25.0);

    [[nodiscard]] std::string name() const override;

    void init(const StockInfo& data) override;

    [[nodiscard]] std::size_t warmupPeriod() const override;

    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t period_;
    double      adxThreshold_;

    // plusDI[0]/minusDI[0] correspond to data index period_; adx[0] corresponds to data index (2*period_ - 1).
    std::size_t          diStartIndex_  = 0;
    std::size_t          adxStartIndex_ = 0;
    indicator::DmiResult dmi_;
};
