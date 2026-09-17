#include "aroon_trend.hpp"

AroonTrend::AroonTrend(std::size_t period, double strengthThreshold)
    : period_(period)
    , strengthThreshold_(strengthThreshold) {}

std::string AroonTrend::name() const {
    return "Aroon Trend (" + std::to_string(period_) + ")";
}

void AroonTrend::init(const StockInfo& data) {
    result_     = indicator::aroon(data.high, data.low, period_);
    startIndex_ = period_ + 1;
}

std::size_t AroonTrend::warmupPeriod() const {
    return startIndex_ + 1;
}

Signal AroonTrend::evaluate(const StockInfo& /* data */, std::size_t index) {
    if (result_.up.empty() || result_.down.empty()) {
        return Signal::HOLD;
    }
    if (index <= startIndex_) {
        return Signal::HOLD;
    }

    const std::size_t idx = index - startIndex_;
    if (idx >= result_.up.size()) {
        return Signal::HOLD;
    }

    const double prevUp   = result_.up[idx - 1];
    const double prevDown = result_.down[idx - 1];
    const double currUp   = result_.up[idx];
    const double currDown = result_.down[idx];

    if (prevUp <= prevDown && currUp > currDown && currUp >= strengthThreshold_) {
        return Signal::BUY;
    }
    if (prevDown <= prevUp && currDown > currUp && currDown >= strengthThreshold_) {
        return Signal::SELL;
    }

    return Signal::HOLD;
}
