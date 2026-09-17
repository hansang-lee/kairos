#include "mfi_reversal.hpp"

#include "indicator.hpp"

MfiReversal::MfiReversal(std::size_t period, double oversold, double overbought)
    : period_(period)
    , oversold_(oversold)
    , overbought_(overbought) {}

std::string MfiReversal::name() const {
    return "MFI Reversal (" + std::to_string(period_) + ")";
}

void MfiReversal::init(const StockInfo& data) {
    mfi_        = indicator::mfi(data.high, data.low, data.close, data.volume, period_);
    startIndex_ = period_ + 1;
}

std::size_t MfiReversal::warmupPeriod() const {
    return startIndex_ + 1;
}

Signal MfiReversal::evaluate(const StockInfo& /* data */, std::size_t index) {
    if (mfi_.empty()) {
        return Signal::HOLD;
    }
    if (index <= startIndex_) {
        return Signal::HOLD;
    }

    const std::size_t idx = index - startIndex_;
    if (idx >= mfi_.size()) {
        return Signal::HOLD;
    }

    const double val = mfi_[idx];

    if (val <= oversold_) {
        return Signal::BUY;
    }
    if (val >= overbought_) {
        return Signal::SELL;
    }

    return Signal::HOLD;
}
