#include "cci_reversal.hpp"

#include "indicator.hpp"

CciReversal::CciReversal(std::size_t period, double oversold, double overbought)
    : period_(period)
    , oversold_(oversold)
    , overbought_(overbought) {}

std::string CciReversal::name() const {
    return "CCI Reversal (" + std::to_string(period_) + ")";
}

void CciReversal::init(const StockInfo& data) {
    cci_        = indicator::cci(data.high, data.low, data.close, period_);
    startIndex_ = period_;
}

std::size_t CciReversal::warmupPeriod() const {
    return startIndex_ + 1;
}

Signal CciReversal::evaluate(const StockInfo& /* data */, std::size_t index) {
    if (cci_.empty()) {
        return Signal::HOLD;
    }
    if (index <= startIndex_) {
        return Signal::HOLD;
    }

    const std::size_t idx = index - startIndex_;
    if (idx >= cci_.size()) {
        return Signal::HOLD;
    }

    const double prev = cci_[idx - 1];
    const double curr = cci_[idx];

    if (prev <= oversold_ && curr > oversold_) {
        return Signal::BUY;
    }
    if (prev >= overbought_ && curr < overbought_) {
        return Signal::SELL;
    }

    return Signal::HOLD;
}
