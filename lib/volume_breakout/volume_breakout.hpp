#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "indicator.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief Channel breakout that only counts when volume confirms it.
 *
 * Most channel breaks fail. The ones that tend not to are the ones the rest of
 * the market showed up for, so a break on below-average volume is ignored here
 * rather than traded. Exits are unconditional — volume confirmation is a reason
 * to enter, never a reason to stay.
 */
class VolumeBreakout: public IStrategy {
   public:
    /**
     * @param period       Donchian channel lookback.
     * @param volumePeriod Lookback for the volume average.
     * @param volumeRatio  Minimum volume multiple required to accept a breakout (e.g. 1.5).
     */
    explicit VolumeBreakout(std::size_t period = 20, std::size_t volumePeriod = 20, double volumeRatio = 1.5);

    [[nodiscard]] std::string name() const override;
    void                      init(const StockInfo& data) override;
    [[nodiscard]] std::size_t warmupPeriod() const override;
    [[nodiscard]] Signal      evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t period_;
    std::size_t volumePeriod_;
    double      volumeRatio_;

    indicator::DonchianResult channel_;    ///< channel_.upper[0] is at data index (period_ - 1)
    std::vector<double>       relVolume_;  ///< relVolume_[0] is at data index (volumePeriod_ - 1)
};
