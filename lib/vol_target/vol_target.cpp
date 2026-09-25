#include "vol_target.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

VolTarget::VolTarget(double targetVol, double cap, std::size_t window, double band)
    : targetVol_(std::max(targetVol, 0.0))
    , cap_(std::max(cap, 0.0))
    , window_(std::max<std::size_t>(window, 2))
    , band_(std::max(band, 0.0)) {}

std::string VolTarget::name() const {
    std::ostringstream os;
    os << "Vol target " << std::fixed << std::setprecision(0) << targetVol_ * 100.0 << "% (cap " << std::setprecision(1)
       << cap_ << "x, " << window_ << "d)";
    return os.str();
}

void VolTarget::init(const StockInfo&) {}

std::size_t VolTarget::warmupPeriod() const {
    // `window` returns need `window + 1` closes, all of them before the bar being
    // decided for.
    return window_ + 1;
}

Signal VolTarget::evaluate(const StockInfo&, std::size_t) {
    return Signal::HOLD;
}

double VolTarget::realisedVol(const StockInfo& data, std::size_t lastBar) const {
    if (lastBar < window_ || lastBar >= data.close.size() || data.close[lastBar - window_] <= 0.0) {
        return 0.0;
    }
    double sum = 0.0, sq = 0.0;
    for (std::size_t k = lastBar - window_ + 1; k <= lastBar; ++k) {
        if (data.close[k - 1] <= 0.0) {
            return 0.0;
        }
        const double r = data.close[k] / data.close[k - 1] - 1.0;
        sum += r;
        sq += r * r;
    }
    const double n = static_cast<double>(window_);
    const double v = sq / n - (sum / n) * (sum / n);
    return v > 0.0 ? std::sqrt(v * 252.0) : 0.0;
}

std::optional<double> VolTarget::targetExposure(const StockInfo& data, std::size_t index) {
    if (index == 0) {
        return 0.0;
    }
    // Decided on the last closed bar, like every other strategy here: the bar at
    // `index` is the one the order fills at, and its close is not known yet.
    const double vol = realisedVol(data, index - 1);
    if (vol <= 0.0) {
        return 0.0;  // not enough history to know is not a reason to be fully in
    }
    const double e = std::min(targetVol_ / vol, cap_);
    // Tenths, so a one-point move in volatility does not become a trade.
    return std::floor(e * 10.0 + 1e-9) / 10.0;
}
