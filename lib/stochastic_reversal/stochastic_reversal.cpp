#include "stochastic_reversal.hpp"

StochasticReversal::StochasticReversal(std::size_t kPeriod, std::size_t dPeriod, double oversold, double overbought)
    : kPeriod_(kPeriod)
    , dPeriod_(dPeriod)
    , oversold_(oversold)
    , overbought_(overbought) {}

std::string StochasticReversal::name() const {
    return "Stochastic Reversal (" + std::to_string(kPeriod_) + "/" + std::to_string(dPeriod_) + ")";
}

void StochasticReversal::init(const StockInfo& data) {
    stoch_      = indicator::stochastic(data.high, data.low, data.close, kPeriod_, dPeriod_);
    startIndex_ = kPeriod_ + dPeriod_ - 1;
}

std::size_t StochasticReversal::warmupPeriod() const {
    return startIndex_ + 1;
}

Signal StochasticReversal::evaluate(const StockInfo& /* data */, std::size_t index) {
    if (stoch_.k.empty() || stoch_.d.empty()) {
        return Signal::HOLD;
    }
    if (index <= startIndex_) {
        return Signal::HOLD;
    }

    const std::size_t idx = index - startIndex_;
    if (idx >= stoch_.k.size()) {
        return Signal::HOLD;
    }

    const double prevK = stoch_.k[idx - 1];
    const double prevD = stoch_.d[idx - 1];
    const double currK = stoch_.k[idx];
    const double currD = stoch_.d[idx];

    // Bullish reversal: %K crosses above %D while still in the oversold zone.
    if (prevK <= prevD && currK > currD && prevK < oversold_) {
        return Signal::BUY;
    }

    // Bearish reversal: %K crosses below %D while still in the overbought zone.
    if (prevK >= prevD && currK < currD && prevK > overbought_) {
        return Signal::SELL;
    }

    return Signal::HOLD;
}
