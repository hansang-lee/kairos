#include "donchian_breakout.hpp"

DonchianBreakout::DonchianBreakout(std::size_t period)
    : period_(period) {}

std::string DonchianBreakout::name() const {
    return "Donchian Breakout (" + std::to_string(period_) + ")";
}

void DonchianBreakout::init(const StockInfo& data) {
    result_     = indicator::donchian(data.high, data.low, period_);
    startIndex_ = period_;
}

std::size_t DonchianBreakout::warmupPeriod() const {
    return startIndex_ + 1;
}

Signal DonchianBreakout::evaluate(const StockInfo& data, std::size_t index) {
    if (result_.upper.empty() || index < 2) {
        return Signal::HOLD;
    }
    if (index <= startIndex_) {
        return Signal::HOLD;
    }

    const std::size_t idx = index - startIndex_;
    if (idx == 0 || idx >= result_.upper.size()) {
        return Signal::HOLD;
    }

    // Compare the most recently closed bar against the channel computed
    // WITHOUT that bar (idx - 1), so the breakout is a genuine new extreme
    // rather than the channel simply widening to include itself.
    const double refClose   = data.close[index - 1];
    const double priorUpper = result_.upper[idx - 1];
    const double priorLower = result_.lower[idx - 1];

    if (refClose > priorUpper) {
        return Signal::BUY;
    }
    if (refClose < priorLower) {
        return Signal::SELL;
    }

    return Signal::HOLD;
}
