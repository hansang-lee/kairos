#include "bollinger_strategy.hpp"

BollingerStrategy::BollingerStrategy(std::size_t period, double numStdDev)
    : period_(period)
    , numStdDev_(numStdDev) {}

std::string BollingerStrategy::name() const {
    return "Bollinger Bands (" + std::to_string(period_) + ", " + std::to_string(static_cast<int>(numStdDev_)) + "σ)";
}

void BollingerStrategy::init(const StockInfo& data) {
    bands_      = indicator::bollinger(data.close, period_, numStdDev_);
    startIndex_ = period_ - 1;
}

std::size_t BollingerStrategy::warmupPeriod() const {
    return period_ + 1;
}

Signal BollingerStrategy::evaluate(const StockInfo& data, std::size_t index) {
    if (bands_.lower.empty() || bands_.upper.empty()) {
        return Signal::HOLD;
    }

    if (index <= startIndex_) {
        return Signal::HOLD;
    }

    const std::size_t idx = index - startIndex_;
    if (idx >= bands_.lower.size()) {
        return Signal::HOLD;
    }

    const double prevPrice = data.close[index - 1];
    const double currPrice = data.close[index];
    const double prevLower = bands_.lower[idx - 1];
    const double currLower = bands_.lower[idx];
    const double currUpper = bands_.upper[idx];

    // Buy: Price touches or dips below lower band and bounces back above it (or is below lower band)
    if (prevPrice <= prevLower && currPrice > currLower) {
        return Signal::BUY;
    }

    // Sell: Price hits or exceeds upper band (take profit / overbought)
    if (currPrice >= currUpper) {
        return Signal::SELL;
    }

    return Signal::HOLD;
}
