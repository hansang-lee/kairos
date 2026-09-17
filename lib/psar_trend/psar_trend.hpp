#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "strategy/istrategy.hpp"

/**
 * @brief Parabolic SAR trend-following strategy.
 *
 * Generates BUY when price crosses above the SAR (trend flips bullish),
 * and SELL when price crosses below it (trend flips bearish).
 */
class PsarTrend: public IStrategy {
   public:
    explicit PsarTrend(double afStep = 0.02, double afMax = 0.2);

    [[nodiscard]] std::string name() const override;

    void init(const StockInfo& data) override;

    [[nodiscard]] std::size_t warmupPeriod() const override;

    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;

   private:
    double afStep_;
    double afMax_;

    // sar_ is 1:1 aligned with the input (sar_[i] corresponds to data index i).
    std::vector<double> sar_;
};
