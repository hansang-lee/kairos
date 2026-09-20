#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "indicator.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief Hold while price is above a long moving average; otherwise hold cash.
 *
 * Faber's tactical rule, and deliberately not a crossover: there is no attempt to
 * time an entry within a trend. It answers one question — is this asset above its
 * own long-term average — and sits in cash when it is not.
 *
 * Its purpose is drawdown, not return. A long decline is spent in cash, which is
 * where most of a buy-and-hold drawdown comes from, at the cost of re-entering
 * late after a sharp reversal.
 */
class MaTiming: public IStrategy {
   public:
    /**
     * @param period     Moving-average length; 200 daily bars is the classic.
     * @param bufferPct  Dead band around the average, in percent. Price must exceed
     *                   the average by this much to buy and fall this far below to
     *                   sell, so a series oscillating around it does not churn.
     */
    explicit MaTiming(std::size_t period = 200, double bufferPct = 0.0);

    [[nodiscard]] std::string name() const override;
    void                      init(const StockInfo& data) override;
    [[nodiscard]] std::size_t warmupPeriod() const override;
    [[nodiscard]] Signal      evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t         period_;
    double              bufferPct_;
    std::vector<double> ma_;  ///< ma_[0] is at data index (period_ - 1)
};
