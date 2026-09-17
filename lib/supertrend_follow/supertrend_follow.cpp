#include "supertrend_follow.hpp"

SuperTrendFollow::SuperTrendFollow(std::size_t period, double multiplier)
    : period_(period)
    , multiplier_(multiplier) {}

std::string SuperTrendFollow::name() const {
    return "SuperTrend (" + std::to_string(period_) + ", " + std::to_string(static_cast<int>(multiplier_)) + ")";
}

void SuperTrendFollow::init(const StockInfo& data) {
    result_     = indicator::superTrend(data.high, data.low, data.close, period_, multiplier_);
    startIndex_ = period_;
}

std::size_t SuperTrendFollow::warmupPeriod() const {
    return startIndex_ + 1;
}

Signal SuperTrendFollow::evaluate(const StockInfo& /* data */, std::size_t index) {
    if (result_.trend.empty()) {
        return Signal::HOLD;
    }
    if (index <= startIndex_) {
        return Signal::HOLD;
    }

    const std::size_t idx = index - startIndex_;
    if (idx >= result_.trend.size()) {
        return Signal::HOLD;
    }

    const int prevTrend = result_.trend[idx - 1];
    const int currTrend = result_.trend[idx];

    if (prevTrend == -1 && currTrend == 1) {
        return Signal::BUY;
    }
    if (prevTrend == 1 && currTrend == -1) {
        return Signal::SELL;
    }

    return Signal::HOLD;
}
