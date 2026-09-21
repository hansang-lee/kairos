#include "relative_momentum.hpp"

#include <memory>
#include <utility>

RelativeMomentum::RelativeMomentum(std::string referenceTicker, std::size_t lookback, double marginPct,
                                   std::string cacheDir)
    : reference_(std::move(referenceTicker))
    , cacheDir_(std::move(cacheDir))
    , lookback_(lookback)
    , marginPct_(marginPct) {}

std::string RelativeMomentum::name() const {
    return "Relative Momentum (vs " + reference_ + ", " + std::to_string(lookback_) + ")";
}

void RelativeMomentum::init(const StockInfo& /* data */) {
    series_ = std::make_unique<data::ReferenceSeries>(reference_, cacheDir_);
}

std::size_t RelativeMomentum::warmupPeriod() const {
    return lookback_ + 1;
}

Signal RelativeMomentum::evaluate(const StockInfo& data, std::size_t index) {
    // Without the reference there is no comparison to make, and guessing one would
    // turn this into a different strategy without saying so.
    if (index == 0 || !series_ || !series_->loaded()) {
        return Signal::HOLD;
    }
    const std::size_t bar = index - 1;  // the last closed bar
    if (bar < lookback_ || bar >= data.timestamps.size()) {
        return Signal::HOLD;
    }

    const double ownPast = data.close[bar - lookback_];
    if (ownPast <= 0.0) {
        return Signal::HOLD;
    }
    const double ownReturn = (data.close[bar] - ownPast) / ownPast * 100.0;

    const double refNow  = series_->closeAtOrBefore(data.timestamps[bar]);
    const double refPast = series_->closeAtOrBefore(data.timestamps[bar - lookback_]);
    if (refNow <= 0.0 || refPast <= 0.0) {
        return Signal::HOLD;
    }
    const double refReturn = (refNow - refPast) / refPast * 100.0;

    return (ownReturn - refReturn > marginPct_) ? Signal::BUY : Signal::SELL;
}
