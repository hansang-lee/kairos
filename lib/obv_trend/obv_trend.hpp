#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "strategy/istrategy.hpp"

/**
 * @brief Volume-confirmed trend strategy.
 *
 * Generates BUY when both price (SMA) and On-Balance Volume are trending
 * up together (volume confirms the price trend), and SELL when both are
 * trending down together.
 */
class ObvTrendConfirm: public IStrategy {
   public:
    explicit ObvTrendConfirm(std::size_t smaPeriod = 20);

    [[nodiscard]] std::string name() const override;

    void init(const StockInfo& data) override;

    [[nodiscard]] std::size_t warmupPeriod() const override;

    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t smaPeriod_;

    // smaVals_[0] corresponds to data index (smaPeriod_ - 1); obv_ is 1:1 aligned with the input.
    std::size_t         smaStartIndex_ = 0;
    std::vector<double> smaVals_;
    std::vector<double> obv_;
};
