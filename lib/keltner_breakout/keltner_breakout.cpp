#include "keltner_breakout.hpp"

#include <algorithm>

KeltnerBreakout::KeltnerBreakout(std::size_t emaPeriod, std::size_t atrPeriod, double multiplier)
    : emaPeriod_(emaPeriod)
    , atrPeriod_(atrPeriod)
    , multiplier_(multiplier) {}

std::string KeltnerBreakout::name() const {
    return "Keltner Breakout (" + std::to_string(emaPeriod_) + "/" + std::to_string(atrPeriod_) + ")";
}

void KeltnerBreakout::init(const StockInfo& data) {
    result_     = indicator::keltner(data.high, data.low, data.close, emaPeriod_, atrPeriod_, multiplier_);
    startIndex_ = std::max(emaPeriod_ - 1, atrPeriod_ - 1) + 1;
}

std::size_t KeltnerBreakout::warmupPeriod() const {
    return startIndex_ + 1;
}

Signal KeltnerBreakout::evaluate(const StockInfo& data, std::size_t index) {
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

    const double prevClose = data.close[index - 2];
    const double currClose = data.close[index - 1];
    const double prevUpper = result_.upper[idx - 1];
    const double currUpper = result_.upper[idx];
    const double prevLower = result_.lower[idx - 1];
    const double currLower = result_.lower[idx];

    if (prevClose <= prevUpper && currClose > currUpper) {
        return Signal::BUY;
    }
    if (prevClose >= prevLower && currClose < currLower) {
        return Signal::SELL;
    }

    return Signal::HOLD;
}
