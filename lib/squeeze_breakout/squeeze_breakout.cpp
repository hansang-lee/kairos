#include "squeeze_breakout.hpp"

#include <algorithm>

SqueezeBreakout::SqueezeBreakout(std::size_t period, double stdDevs, std::size_t squeezeLookback, double squeezePercent)
    : period_(period)
    , stdDevs_(stdDevs)
    , squeezeLookback_(squeezeLookback)
    , squeezePercent_(squeezePercent) {}

std::string SqueezeBreakout::name() const {
    return "Squeeze Breakout (BB" + std::to_string(period_) + ", bottom "
         + std::to_string(static_cast<int>(squeezePercent_ * 100)) + "% bandwidth)";
}

void SqueezeBreakout::init(const StockInfo& data) {
    bands_    = indicator::bollinger(data.close, period_, stdDevs_);
    position_ = indicator::bollingerPosition(data.close, period_, stdDevs_);
}

std::size_t SqueezeBreakout::warmupPeriod() const {
    return period_ + squeezeLookback_;
}

Signal SqueezeBreakout::evaluate(const StockInfo& data, std::size_t index) {
    if (index == 0 || position_.bandwidth.empty()) {
        return Signal::HOLD;
    }

    const std::size_t bar = index - 1;  // last fully closed bar
    if (bar + 1 < period_) {
        return Signal::HOLD;
    }
    const std::size_t idx = bar - (period_ - 1);
    if (idx >= position_.bandwidth.size() || idx < squeezeLookback_) {
        return Signal::HOLD;
    }

    // Was the bar *before* the break compressed? The breaking bar itself expands
    // bandwidth, so testing the current one would reject every real breakout.
    const std::size_t prev = idx - 1;
    double            lo   = position_.bandwidth[prev];
    double            hi   = position_.bandwidth[prev];
    for (std::size_t j = prev + 1 - squeezeLookback_; j <= prev; ++j) {
        lo = std::min(lo, position_.bandwidth[j]);
        hi = std::max(hi, position_.bandwidth[j]);
    }
    const double span        = hi - lo;
    const double threshold   = lo + span * squeezePercent_;
    const bool   wasSqueezed = span <= 0.0 || position_.bandwidth[prev] <= threshold;

    const double price = data.close[bar];

    // Leaving the band on the downside ends the trade whether or not it followed a
    // squeeze — an exit must not depend on how the entry was justified.
    if (price < bands_.lower[idx]) {
        return Signal::SELL;
    }
    if (wasSqueezed && price > bands_.upper[idx]) {
        return Signal::BUY;
    }
    return Signal::HOLD;
}
