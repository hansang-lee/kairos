#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "strategy/istrategy.hpp"

/**
 * @brief Hold as much of the asset as keeps the sleeve at a target volatility.
 *
 * Every crash in the record was preceded and accompanied by rising volatility, so
 * scaling exposure by 1/σ steps out of them gradually rather than all at once,
 * and back in as they settle. From the top of the dot-com bubble to 2026 this
 * beat holding QQQ in 80% of five-year periods with a smaller drawdown, and it
 * turned the 2000-2013 lost decade from -2.8% a year into +3.8%; the moving-
 * average gate, by contrast, gave up return for its safety. See
 * docs/BACKTEST_RESULTS.md §11 for the full record.
 *
 * Exposure is quantised to tenths and only re-targeted when it has moved by
 * `band`, because trading every daily tick of volatility cost three points a
 * year at 0.05% a switch and the band recovered nearly all of it.
 */
class VolTarget: public IStrategy {
   public:
    /**
     * @param targetVol Annualised volatility the sleeve aims for, e.g. 0.20.
     * @param cap       Ceiling on exposure. 1.0 is unlevered; above it needs a
     *                  levered leg the single-instrument paths do not have yet.
     * @param window    Bars of daily returns the realised volatility is read from.
     * @param band      Change in exposure below which the current holding is kept.
     */
    explicit VolTarget(double targetVol = 0.20, double cap = 1.0, std::size_t window = 20, double band = 0.2);

    [[nodiscard]] std::string name() const override;
    void                      init(const StockInfo& data) override;
    [[nodiscard]] std::size_t warmupPeriod() const override;

    /** @brief Always HOLD: direction lives in targetExposure(), not here. */
    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;

    [[nodiscard]] std::optional<double> targetExposure(const StockInfo& data, std::size_t index) override;

    /** @brief Realised annualised volatility of bars ending at `lastBar`, inclusive. 0 if too short. */
    [[nodiscard]] double realisedVol(const StockInfo& data, std::size_t lastBar) const;

    [[nodiscard]] double band() const { return band_; }

   private:
    double      targetVol_;
    double      cap_;
    std::size_t window_;
    double      band_;
};
