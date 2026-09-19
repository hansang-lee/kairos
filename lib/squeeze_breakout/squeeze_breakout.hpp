#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "indicator.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief Trade the expansion that follows a volatility squeeze.
 *
 * When Bollinger bandwidth drops to the low end of its own recent range, price
 * has gone quiet — and quiet periods resolve into moves. This waits for that
 * compression and then takes the direction price breaks in, which avoids the
 * usual failure of breakout systems: firing on every minor push during an
 * already-volatile stretch.
 */
class SqueezeBreakout: public IStrategy {
   public:
    /**
     * @param period          Bollinger period.
     * @param stdDevs         Bollinger width in standard deviations.
     * @param squeezeLookback Window the current bandwidth is ranked within.
     * @param squeezePercent  How low in that range bandwidth must sit to count as a
     *                        squeeze, 0~1 (e.g. 0.25 = bottom quarter).
     */
    explicit SqueezeBreakout(std::size_t period = 20, double stdDevs = 2.0, std::size_t squeezeLookback = 60,
                             double squeezePercent = 0.25);

    [[nodiscard]] std::string name() const override;
    void                      init(const StockInfo& data) override;
    [[nodiscard]] std::size_t warmupPeriod() const override;
    [[nodiscard]] Signal      evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t period_;
    double      stdDevs_;
    std::size_t squeezeLookback_;
    double      squeezePercent_;

    indicator::BollingerBands          bands_;     ///< bands_.middle[0] is at data index (period_ - 1)
    indicator::BollingerPositionResult position_;  ///< same alignment as bands_
};
