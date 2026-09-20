#pragma once

#include <cstddef>
#include <string>

#include "indicator.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief Hold while the asset's own trailing return is positive.
 *
 * Time-series momentum: what an asset did over the last N bars predicts the next
 * stretch better than chance, across most markets and most of the last century.
 * Unlike the oscillators here, it takes no view on whether price is cheap — only
 * on whether it has been going up.
 *
 * Also a drawdown tool. A sustained decline turns the trailing return negative
 * and the position goes to cash, which is the part of a bear market that does the
 * damage.
 */
class AbsoluteMomentum: public IStrategy {
   public:
    /**
     * @param lookback  Bars of trailing return; 252 is roughly a year of daily bars.
     * @param threshold Return, in percent, the lookback must exceed to stay invested.
     *                  A small positive value avoids trading on a flat market.
     */
    explicit AbsoluteMomentum(std::size_t lookback = 252, double threshold = 0.0);

    [[nodiscard]] std::string name() const override;
    void                      init(const StockInfo& data) override;
    [[nodiscard]] std::size_t warmupPeriod() const override;
    [[nodiscard]] Signal      evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t lookback_;
    double      threshold_;
};
