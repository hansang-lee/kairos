#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "strategy/istrategy.hpp"

/**
 * @brief CCI (Commodity Channel Index) reversal strategy.
 *
 * Generates BUY when CCI crosses above -100 (exiting oversold), and SELL
 * when CCI crosses below +100 (exiting overbought).
 */
class CciReversal: public IStrategy {
   public:
    explicit CciReversal(std::size_t period = 20, double oversold = -100.0, double overbought = 100.0);

    [[nodiscard]] std::string name() const override;

    void init(const StockInfo& data) override;

    [[nodiscard]] std::size_t warmupPeriod() const override;

    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t period_;
    double      oversold_;
    double      overbought_;

    // cci_[0] corresponds to data index (period_ - 1).
    std::size_t         startIndex_ = 0;
    std::vector<double> cci_;
};
