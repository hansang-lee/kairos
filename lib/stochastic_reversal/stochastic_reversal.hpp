#pragma once

#include <cstddef>
#include <string>

#include "indicator.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief Stochastic Oscillator reversal strategy.
 *
 * Generates BUY when %K crosses above %D while coming out of the oversold
 * zone, and SELL when %K crosses below %D while coming out of the
 * overbought zone.
 */
class StochasticReversal: public IStrategy {
   public:
    explicit StochasticReversal(std::size_t kPeriod = 14, std::size_t dPeriod = 3, double oversold = 20.0,
                                double overbought = 80.0);

    [[nodiscard]] std::string name() const override;

    void init(const StockInfo& data) override;

    [[nodiscard]] std::size_t warmupPeriod() const override;

    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t kPeriod_;
    std::size_t dPeriod_;
    double      oversold_;
    double      overbought_;

    // stoch_.k[0]/stoch_.d[0] correspond to data index (kPeriod_ + dPeriod_ - 2).
    std::size_t                 startIndex_ = 0;
    indicator::StochasticResult stoch_;
};
