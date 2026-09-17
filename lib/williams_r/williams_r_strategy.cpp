#include "williams_r_strategy.hpp"

#include "indicator.hpp"

WilliamsRStrategy::WilliamsRStrategy(std::size_t period, double oversold, double overbought)
    : period_(period)
    , oversold_(oversold)
    , overbought_(overbought) {}

std::string WilliamsRStrategy::name() const {
    return "Williams %R (" + std::to_string(period_) + ")";
}

void WilliamsRStrategy::init(const StockInfo& data) {
    willR_      = indicator::williamsR(data.high, data.low, data.close, period_);
    startIndex_ = period_;
}

std::size_t WilliamsRStrategy::warmupPeriod() const {
    return startIndex_ + 1;
}

Signal WilliamsRStrategy::evaluate(const StockInfo& /* data */, std::size_t index) {
    if (willR_.empty()) {
        return Signal::HOLD;
    }
    if (index <= startIndex_) {
        return Signal::HOLD;
    }

    const std::size_t idx = index - startIndex_;
    if (idx >= willR_.size()) {
        return Signal::HOLD;
    }

    const double prev = willR_[idx - 1];
    const double curr = willR_[idx];

    if (prev <= oversold_ && curr > oversold_) {
        return Signal::BUY;
    }
    if (prev >= overbought_ && curr < overbought_) {
        return Signal::SELL;
    }

    return Signal::HOLD;
}
