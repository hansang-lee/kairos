#include "regime_rsi.hpp"

#include <algorithm>

RegimeRsi::RegimeRsi(std::size_t regimePeriod, std::size_t rsiPeriod, double oversold, double exitLevel)
    : regimePeriod_(regimePeriod)
    , rsiPeriod_(rsiPeriod)
    , oversold_(oversold)
    , exitLevel_(exitLevel) {}

std::string RegimeRsi::name() const {
    return "Regime-Filtered RSI (MA" + std::to_string(regimePeriod_) + "/RSI" + std::to_string(rsiPeriod_) + ")";
}

void RegimeRsi::init(const StockInfo& data) {
    regimeMa_ = indicator::sma(data.close, regimePeriod_);
    rsi_      = indicator::rsi(data.close, rsiPeriod_);
}

std::size_t RegimeRsi::warmupPeriod() const {
    return std::max(regimePeriod_, rsiPeriod_ + 1);
}

Signal RegimeRsi::evaluate(const StockInfo& data, std::size_t index) {
    if (index == 0 || regimeMa_.empty() || rsi_.empty()) {
        return Signal::HOLD;
    }

    // Everything is read at data index (index - 1): the last fully closed bar.
    const std::size_t bar = index - 1;
    if (bar + 1 < regimePeriod_ || bar < rsiPeriod_) {
        return Signal::HOLD;
    }

    const std::size_t maIdx  = bar - (regimePeriod_ - 1);
    const std::size_t rsiIdx = bar - rsiPeriod_;
    if (maIdx >= regimeMa_.size() || rsiIdx >= rsi_.size()) {
        return Signal::HOLD;
    }

    const double price   = data.close[bar];
    const double rsiNow  = rsi_[rsiIdx];
    const bool   uptrend = price > regimeMa_[maIdx];

    // Exit first, and without the regime gate: a position must always be closable.
    if (rsiNow >= exitLevel_ || !uptrend) {
        return Signal::SELL;
    }
    if (rsiNow <= oversold_) {
        return Signal::BUY;
    }
    return Signal::HOLD;
}
