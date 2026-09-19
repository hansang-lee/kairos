#include "ichimoku_trend.hpp"

#include <algorithm>

IchimokuTrend::IchimokuTrend(std::size_t conversion, std::size_t base, std::size_t spanB)
    : conversion_(conversion)
    , base_(base)
    , spanB_(spanB) {}

std::string IchimokuTrend::name() const {
    return "Ichimoku Cloud Trend (" + std::to_string(conversion_) + "/" + std::to_string(base_) + "/"
         + std::to_string(spanB_) + ")";
}

void IchimokuTrend::init(const StockInfo& data) {
    ichimoku_ = indicator::ichimoku(data.high, data.low, data.close, conversion_, base_, spanB_);
}

std::size_t IchimokuTrend::warmupPeriod() const {
    return base_ + spanB_;
}

Signal IchimokuTrend::evaluate(const StockInfo& data, std::size_t index) {
    if (index == 0 || ichimoku_.senkouA.empty()) {
        return Signal::HOLD;
    }

    const std::size_t bar = index - 1;  // last fully closed bar
    if (bar >= ichimoku_.senkouA.size()) {
        return Signal::HOLD;
    }

    const double spanA = ichimoku_.senkouA[bar];
    const double spanB = ichimoku_.senkouB[bar];
    if (spanA <= 0.0 || spanB <= 0.0) {
        return Signal::HOLD;  // cloud not defined this far back yet
    }

    const double price      = data.close[bar];
    const double cloudTop   = std::max(spanA, spanB);
    const double cloudFloor = std::min(spanA, spanB);
    const double tenkan     = ichimoku_.tenkan[bar];
    const double kijun      = ichimoku_.kijun[bar];

    if (price < cloudFloor) {
        return Signal::SELL;
    }
    if (price > cloudTop && tenkan > kijun) {
        return Signal::BUY;
    }
    return Signal::HOLD;
}
