#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "indicator.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief Trend-aligned mean reversion: buy dips, but only in an uptrend.
 *
 * Plain RSI reversion buys every oversold reading, including the ones on the way
 * down in a bear market — which is where the strategy does most of its losing.
 * A long moving average decides the regime, and entries are only taken on the
 * side the regime favours; exits are not gated, so a position is always allowed
 * to close.
 */
class RegimeRsi: public IStrategy {
   public:
    /**
     * @param regimePeriod MA period defining the regime (e.g. 200 daily, 60 intraday).
     * @param rsiPeriod    RSI lookback.
     * @param oversold     RSI level that triggers an entry while the regime is up.
     * @param exitLevel    RSI level that closes the position.
     */
    explicit RegimeRsi(std::size_t regimePeriod = 200, std::size_t rsiPeriod = 14, double oversold = 35.0,
                       double exitLevel = 65.0);

    [[nodiscard]] std::string name() const override;
    void                      init(const StockInfo& data) override;
    [[nodiscard]] std::size_t warmupPeriod() const override;
    [[nodiscard]] Signal      evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t regimePeriod_;
    std::size_t rsiPeriod_;
    double      oversold_;
    double      exitLevel_;

    std::vector<double> regimeMa_;  ///< regimeMa_[0] is at data index (regimePeriod_ - 1)
    std::vector<double> rsi_;       ///< rsi_[0] is at data index rsiPeriod_
};
