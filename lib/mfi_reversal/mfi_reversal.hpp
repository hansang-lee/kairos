#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "strategy/istrategy.hpp"

/**
 * @brief Money Flow Index (volume-weighted RSI) reversal strategy.
 *
 * Generates BUY when MFI drops to or below the oversold threshold, and
 * SELL when MFI rises to or above the overbought threshold.
 */
class MfiReversal: public IStrategy {
   public:
    explicit MfiReversal(std::size_t period = 14, double oversold = 20.0, double overbought = 80.0);

    [[nodiscard]] std::string name() const override;

    void init(const StockInfo& data) override;

    [[nodiscard]] std::size_t warmupPeriod() const override;

    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t period_;
    double      oversold_;
    double      overbought_;

    // mfi_[0] corresponds to data index period_.
    std::size_t         startIndex_ = 0;
    std::vector<double> mfi_;
};
