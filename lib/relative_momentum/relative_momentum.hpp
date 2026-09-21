#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "data/reference_series.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief Hold this asset only while it is outrunning a reference one.
 *
 * Antonacci's relative momentum: rank two assets by trailing return and hold the
 * winner. Pointed at an equity ETF with a bond ETF as reference, it is a
 * macro-flavoured rule without any macro data — the bond/equity ratio already
 * carries what the yield curve and credit spreads are read for, and it carries it
 * as a price, which is observable today rather than revised next month.
 *
 * That last point is the reason to prefer it. Every macro series here is revised
 * after publication, so a backtest on the final values uses numbers nobody had at
 * the time. Prices are not revised.
 */
class RelativeMomentum: public IStrategy {
   public:
    /**
     * @param referenceTicker Asset to be measured against, e.g. a bond ETF.
     * @param lookback        Bars of trailing return on both sides.
     * @param marginPct       How far ahead this asset must be, in percentage points,
     *                        before it is held. A margin keeps a near-tie from
     *                        flipping the position every few bars.
     */
    /**
     * @param cacheDir Where the reference prices live. Empty resolves to
     *                 <project-root>/cache/daily, which is correct for the apps but
     *                 not for every binary — resolveFromExe counts directories up
     *                 from the executable, and the test binary sits one level
     *                 shallower than the apps do.
     */
    explicit RelativeMomentum(std::string referenceTicker = "148070", std::size_t lookback = 126,
                              double marginPct = 0.0, std::string cacheDir = "");

    [[nodiscard]] std::string name() const override;
    void                      init(const StockInfo& data) override;
    [[nodiscard]] std::size_t warmupPeriod() const override;
    [[nodiscard]] Signal      evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::string                            reference_;
    std::string                            cacheDir_;
    std::size_t                            lookback_;
    double                                 marginPct_;
    std::unique_ptr<data::ReferenceSeries> series_;
};
