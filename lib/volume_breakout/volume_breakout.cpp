#include "volume_breakout.hpp"

#include <algorithm>

VolumeBreakout::VolumeBreakout(std::size_t period, std::size_t volumePeriod, double volumeRatio)
    : period_(period)
    , volumePeriod_(volumePeriod)
    , volumeRatio_(volumeRatio) {}

std::string VolumeBreakout::name() const {
    return "Volume-Confirmed Breakout (" + std::to_string(period_) + "d, vol>" + std::to_string(volumeRatio_) + "x)";
}

void VolumeBreakout::init(const StockInfo& data) {
    channel_   = indicator::donchian(data.high, data.low, period_);
    relVolume_ = indicator::relativeVolume(data.volume, volumePeriod_);
}

std::size_t VolumeBreakout::warmupPeriod() const {
    return std::max(period_, volumePeriod_) + 1;
}

Signal VolumeBreakout::evaluate(const StockInfo& data, std::size_t index) {
    if (index == 0 || channel_.upper.empty() || relVolume_.empty()) {
        return Signal::HOLD;
    }

    const std::size_t bar = index - 1;  // last fully closed bar
    if (bar + 1 < period_ || bar + 1 < volumePeriod_ || bar == 0) {
        return Signal::HOLD;
    }

    // The channel the break is measured against must exclude the breaking bar
    // itself, so the *previous* bar's channel is used.
    const std::size_t chIdx  = bar - period_;  // channel at bar-1
    const std::size_t volIdx = bar - (volumePeriod_ - 1);
    if (chIdx >= channel_.upper.size() || volIdx >= relVolume_.size()) {
        return Signal::HOLD;
    }

    const double price = data.close[bar];

    // Exits are not gated on volume: a breakdown on quiet volume still ends the trade.
    if (price < channel_.lower[chIdx]) {
        return Signal::SELL;
    }
    if (price > channel_.upper[chIdx] && relVolume_[volIdx] >= volumeRatio_) {
        return Signal::BUY;
    }
    return Signal::HOLD;
}
