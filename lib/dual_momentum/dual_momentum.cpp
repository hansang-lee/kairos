#include "dual_momentum.hpp"

#include <algorithm>

DualMomentum::DualMomentum(std::size_t maPeriod, std::size_t lookback, double threshold)
    : maPeriod_(maPeriod)
    , lookback_(lookback)
    , threshold_(threshold) {}

std::string DualMomentum::name() const {
    return "Dual Momentum (MA" + std::to_string(maPeriod_) + "/" + std::to_string(lookback_) + ")";
}

void DualMomentum::init(const StockInfo& data) {
    ma_ = indicator::sma(data.close, maPeriod_);
}

std::size_t DualMomentum::warmupPeriod() const {
    return std::max(maPeriod_, lookback_ + 1);
}

Signal DualMomentum::evaluate(const StockInfo& data, std::size_t index) {
    if (index == 0 || ma_.empty()) {
        return Signal::HOLD;
    }

    const std::size_t bar = index - 1;  // the last closed bar
    if (bar + 1 < maPeriod_ || bar < lookback_) {
        return Signal::HOLD;
    }
    const std::size_t idx = bar - (maPeriod_ - 1);
    if (idx >= ma_.size()) {
        return Signal::HOLD;
    }

    const double price      = data.close[bar];
    const double past       = data.close[bar - lookback_];
    const bool   aboveTrend = price > ma_[idx];
    const bool   rising     = past > 0.0 && (price - past) / past * 100.0 > threshold_;

    // Both must agree to be invested; either turning is enough to leave.
    if (aboveTrend && rising) {
        return Signal::BUY;
    }
    if (!aboveTrend || !rising) {
        return Signal::SELL;
    }
    return Signal::HOLD;
}
