#include "absolute_momentum.hpp"

AbsoluteMomentum::AbsoluteMomentum(std::size_t lookback, double threshold)
    : lookback_(lookback)
    , threshold_(threshold) {}

std::string AbsoluteMomentum::name() const {
    return "Absolute Momentum (" + std::to_string(lookback_) + ")";
}

void AbsoluteMomentum::init(const StockInfo& /* data */) {
    // Nothing to precompute: the trailing return is two array reads.
}

std::size_t AbsoluteMomentum::warmupPeriod() const {
    return lookback_ + 1;
}

Signal AbsoluteMomentum::evaluate(const StockInfo& data, std::size_t index) {
    if (index == 0) {
        return Signal::HOLD;
    }
    const std::size_t bar = index - 1;  // the last closed bar
    if (bar < lookback_) {
        return Signal::HOLD;
    }

    const double past = data.close[bar - lookback_];
    if (past <= 0.0) {
        return Signal::HOLD;
    }
    const double trailingPct = (data.close[bar] - past) / past * 100.0;

    return trailingPct > threshold_ ? Signal::BUY : Signal::SELL;
}
