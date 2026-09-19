#include "macd_strategy.hpp"

#include <iostream>

MacdStrategy::MacdStrategy(std::size_t fastPeriod, std::size_t slowPeriod, std::size_t signalPeriod)
    : fastPeriod_(fastPeriod)
    , slowPeriod_(slowPeriod)
    , signalPeriod_(signalPeriod) {}

std::string MacdStrategy::name() const {
    return "MACD (" + std::to_string(fastPeriod_) + "/" + std::to_string(slowPeriod_) + "/"
         + std::to_string(signalPeriod_) + ")";
}

void MacdStrategy::init(const StockInfo& data) {
    macdResult_ = indicator::macd(data.close, fastPeriod_, slowPeriod_, signalPeriod_);
    // macdResult_.macd[j] belongs to data index j + slowPeriod_ + signalPeriod_ - 2, so this
    // offset makes evaluate()'s idx resolve to data index (index - 1): the last closed bar.
    // It was one lower, which resolved to `index` itself — the bar being traded.
    startIndex_ = slowPeriod_ + signalPeriod_ - 1;
}

std::size_t MacdStrategy::warmupPeriod() const {
    return slowPeriod_ + signalPeriod_;
}

Signal MacdStrategy::evaluate(const StockInfo& /* data */, std::size_t index) {
    if (macdResult_.macd.empty() || macdResult_.signal.empty()) {
        return Signal::HOLD;
    }

    if (index <= startIndex_) {
        return Signal::HOLD;
    }

    const std::size_t idx = index - startIndex_;
    if (idx >= macdResult_.macd.size()) {
        return Signal::HOLD;
    }

    const double prevMacd = macdResult_.macd[idx - 1];
    const double prevSig  = macdResult_.signal[idx - 1];
    const double currMacd = macdResult_.macd[idx];
    const double currSig  = macdResult_.signal[idx];

    // Bullish crossover: MACD crosses above Signal Line
    if (prevMacd <= prevSig && currMacd > currSig) {
        return Signal::BUY;
    }

    // Bearish crossover: MACD crosses below Signal Line
    if (prevMacd >= prevSig && currMacd < currSig) {
        return Signal::SELL;
    }

    return Signal::HOLD;
}
