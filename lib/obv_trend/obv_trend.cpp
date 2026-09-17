#include "obv_trend.hpp"

#include "indicator.hpp"

ObvTrendConfirm::ObvTrendConfirm(std::size_t smaPeriod)
    : smaPeriod_(smaPeriod) {}

std::string ObvTrendConfirm::name() const {
    return "OBV Trend Confirm (SMA " + std::to_string(smaPeriod_) + ")";
}

void ObvTrendConfirm::init(const StockInfo& data) {
    smaVals_       = indicator::sma(data.close, smaPeriod_);
    obv_           = indicator::obv(data.close, data.volume);
    smaStartIndex_ = smaPeriod_;
}

std::size_t ObvTrendConfirm::warmupPeriod() const {
    return smaStartIndex_ + 1;
}

Signal ObvTrendConfirm::evaluate(const StockInfo& /* data */, std::size_t index) {
    if (smaVals_.empty() || obv_.size() < 2) {
        return Signal::HOLD;
    }
    if (index <= smaStartIndex_ || index > obv_.size()) {
        return Signal::HOLD;
    }

    const std::size_t idx = index - smaStartIndex_;
    if (idx == 0 || idx >= smaVals_.size()) {
        return Signal::HOLD;
    }

    const double prevSma = smaVals_[idx - 1];
    const double currSma = smaVals_[idx];
    const double prevObv = obv_[index - 2];
    const double currObv = obv_[index - 1];

    if (currSma > prevSma && currObv > prevObv) {
        return Signal::BUY;
    }
    if (currSma < prevSma && currObv < prevObv) {
        return Signal::SELL;
    }

    return Signal::HOLD;
}
