#include "ma_slope_trend.hpp"

#include <algorithm>

MaSlopeTrend::MaSlopeTrend(std::size_t maPeriod, std::size_t slopeWindow, double entryThreshold, double exitThreshold,
                           std::size_t adxPeriod, double adxThreshold)
    : maPeriod_(maPeriod)
    , slopeWindow_(slopeWindow)
    , entryThreshold_(entryThreshold)
    , exitThreshold_(exitThreshold)
    , adxPeriod_(adxPeriod)
    , adxThreshold_(adxThreshold) {}

std::string MaSlopeTrend::name() const {
    return "MA Slope Trend (MA" + std::to_string(maPeriod_) + "/slope" + std::to_string(slopeWindow_) + ")";
}

void MaSlopeTrend::init(const StockInfo& data) {
    slope_           = indicator::maSlope(data.close, maPeriod_, slopeWindow_);
    dmi_             = indicator::adx(data.high, data.low, data.close, adxPeriod_);
    slopeStartIndex_ = maPeriod_ + slopeWindow_ - 1;
    adxStartIndex_   = 2 * adxPeriod_;
}

std::size_t MaSlopeTrend::warmupPeriod() const {
    return std::max(slopeStartIndex_, adxStartIndex_) + 1;
}

Signal MaSlopeTrend::evaluate(const StockInfo& /* data */, std::size_t index) {
    if (slope_.empty() || dmi_.adx.empty()) {
        return Signal::HOLD;
    }

    const std::size_t guardIndex = std::max(slopeStartIndex_, adxStartIndex_);
    if (index <= guardIndex) {
        return Signal::HOLD;
    }

    const std::size_t slopeIdx = index - slopeStartIndex_;
    const std::size_t adxIdx   = index - adxStartIndex_;
    if (slopeIdx == 0 || slopeIdx >= slope_.size() || adxIdx >= dmi_.adx.size()) {
        return Signal::HOLD;
    }

    const double prevSlope = slope_[slopeIdx - 1];
    const double currSlope = slope_[slopeIdx];
    const double currAdx   = dmi_.adx[adxIdx];

    // Uptrend starting, and ADX confirms it's a real trend, not chop -> BUY.
    if (prevSlope <= entryThreshold_ && currSlope > entryThreshold_ && currAdx >= adxThreshold_) {
        return Signal::BUY;
    }

    // Slope turns clearly negative (hysteresis: exitThreshold_ < entryThreshold_) -> SELL.
    // No ADX gate on exit, so a position can always be closed.
    if (prevSlope >= exitThreshold_ && currSlope < exitThreshold_) {
        return Signal::SELL;
    }

    return Signal::HOLD;
}
