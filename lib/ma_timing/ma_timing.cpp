#include "ma_timing.hpp"

MaTiming::MaTiming(std::size_t period, double bufferPct)
    : period_(period)
    , bufferPct_(bufferPct) {}

std::string MaTiming::name() const {
    return "MA Timing (" + std::to_string(period_) + ")";
}

void MaTiming::init(const StockInfo& data) {
    ma_ = indicator::sma(data.close, period_);
}

std::size_t MaTiming::warmupPeriod() const {
    return period_;
}

Signal MaTiming::evaluate(const StockInfo& data, std::size_t index) {
    if (index == 0 || ma_.empty()) {
        return Signal::HOLD;
    }

    const std::size_t bar = index - 1;  // the last closed bar
    if (bar + 1 < period_) {
        return Signal::HOLD;
    }
    const std::size_t idx = bar - (period_ - 1);
    if (idx >= ma_.size()) {
        return Signal::HOLD;
    }

    const double price = data.close[bar];
    const double avg   = ma_[idx];
    const double band  = avg * bufferPct_ / 100.0;

    // Above the band: invested. Below it: cash. Inside it: whatever we were doing,
    // which is what stops a series hovering at the average from trading every bar.
    if (price > avg + band) {
        return Signal::BUY;
    }
    if (price < avg - band) {
        return Signal::SELL;
    }
    return Signal::HOLD;
}
