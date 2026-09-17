#include "adx_trend.hpp"

AdxTrend::AdxTrend(std::size_t period, double adxThreshold)
    : period_(period)
    , adxThreshold_(adxThreshold) {}

std::string AdxTrend::name() const {
    return "ADX Trend (" + std::to_string(period_) + ", th=" + std::to_string(static_cast<int>(adxThreshold_)) + ")";
}

void AdxTrend::init(const StockInfo& data) {
    dmi_           = indicator::adx(data.high, data.low, data.close, period_);
    diStartIndex_  = period_ + 1;
    adxStartIndex_ = 2 * period_;
}

std::size_t AdxTrend::warmupPeriod() const {
    return adxStartIndex_ + 1;
}

Signal AdxTrend::evaluate(const StockInfo& /* data */, std::size_t index) {
    if (dmi_.adx.empty() || dmi_.plusDI.empty() || dmi_.minusDI.empty()) {
        return Signal::HOLD;
    }
    if (index <= adxStartIndex_) {
        return Signal::HOLD;
    }

    const std::size_t diIdx  = index - diStartIndex_;
    const std::size_t adxIdx = index - adxStartIndex_;
    if (diIdx == 0 || diIdx >= dmi_.plusDI.size() || adxIdx >= dmi_.adx.size()) {
        return Signal::HOLD;
    }

    const double prevPlus  = dmi_.plusDI[diIdx - 1];
    const double prevMinus = dmi_.minusDI[diIdx - 1];
    const double currPlus  = dmi_.plusDI[diIdx];
    const double currMinus = dmi_.minusDI[diIdx];
    const double currAdx   = dmi_.adx[adxIdx];

    // +DI crosses above -DI with a confirmed strong trend -> BUY.
    if (prevPlus <= prevMinus && currPlus > currMinus && currAdx >= adxThreshold_) {
        return Signal::BUY;
    }

    // -DI crosses above +DI with a confirmed strong trend -> SELL.
    if (prevPlus >= prevMinus && currPlus < currMinus && currAdx >= adxThreshold_) {
        return Signal::SELL;
    }

    return Signal::HOLD;
}
