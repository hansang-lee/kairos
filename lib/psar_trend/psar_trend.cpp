#include "psar_trend.hpp"

#include "indicator.hpp"

PsarTrend::PsarTrend(double afStep, double afMax)
    : afStep_(afStep)
    , afMax_(afMax) {}

std::string PsarTrend::name() const {
    return "Parabolic SAR (" + std::to_string(afStep_) + "/" + std::to_string(afMax_) + ")";
}

void PsarTrend::init(const StockInfo& data) {
    sar_ = indicator::parabolicSar(data.high, data.low, afStep_, afMax_);
}

std::size_t PsarTrend::warmupPeriod() const {
    return 3;
}

Signal PsarTrend::evaluate(const StockInfo& data, std::size_t index) {
    // Use data strictly through (index - 1) to avoid look-ahead into the current bar.
    // sar_ is aligned 1:1 with the input, so reading index-1 needs index <= size.
    // The guard was `index >= size`, which rejected the one-past-the-last index that
    // live trading uses before the current bar exists.
    if (index < 2 || index > sar_.size()) {
        return Signal::HOLD;
    }

    const double prevClose     = data.close[index - 1];
    const double prevPrevClose = data.close[index - 2];
    const double prevSar       = sar_[index - 1];
    const double prevPrevSar   = sar_[index - 2];

    if (prevPrevClose <= prevPrevSar && prevClose > prevSar) {
        return Signal::BUY;
    }
    if (prevPrevClose >= prevPrevSar && prevClose < prevSar) {
        return Signal::SELL;
    }

    return Signal::HOLD;
}
