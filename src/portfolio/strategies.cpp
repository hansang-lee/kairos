#include "portfolio/strategies.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace portfolio {

namespace {

/** Trailing return over `lookback` bars, reading nothing past `bar`. */
double trailingReturn(const PortfolioData& d, std::size_t asset, std::size_t bar, std::size_t lookback) {
    if (bar < lookback || !d.available[asset][bar] || !d.available[asset][bar - lookback]) {
        return -1e9;  // unrankable, not merely weak
    }
    const double past = d.close[asset][bar - lookback];
    return past > 0.0 ? (d.close[asset][bar] - past) / past * 100.0 : -1e9;
}

/** Standard deviation of bar-to-bar returns, as a volatility proxy. */
double volatility(const PortfolioData& d, std::size_t asset, std::size_t bar, std::size_t lookback) {
    if (bar < lookback + 1) {
        return 0.0;
    }
    std::vector<double> rets;
    rets.reserve(lookback);
    for (std::size_t i = bar - lookback + 1; i <= bar; ++i) {
        const double prev = d.close[asset][i - 1];
        if (prev > 0.0 && d.available[asset][i]) {
            rets.push_back((d.close[asset][i] - prev) / prev);
        }
    }
    if (rets.size() < 2) {
        return 0.0;
    }
    const double mean = std::accumulate(rets.begin(), rets.end(), 0.0) / static_cast<double>(rets.size());
    double       var  = 0.0;
    for (const double r : rets) {
        var += (r - mean) * (r - mean);
    }
    return std::sqrt(var / static_cast<double>(rets.size() - 1));
}

}  // namespace

/* --------------------------- MomentumRotation --------------------------- */

MomentumRotation::MomentumRotation(std::size_t topN, std::size_t lookback, double minReturnPct)
    : topN_(topN)
    , lookback_(lookback)
    , minReturnPct_(minReturnPct) {}

std::string MomentumRotation::name() const {
    return "Momentum rotation (top " + std::to_string(topN_) + " of " + std::to_string(lookback_) + ")";
}

void MomentumRotation::init(const PortfolioData&) {}

std::size_t MomentumRotation::warmupPeriod() const {
    return lookback_ + 1;
}

std::vector<double> MomentumRotation::targetWeights(const PortfolioData& data, std::size_t index) {
    std::vector<double> weights(data.assetCount(), 0.0);
    if (index == 0) {
        return weights;
    }
    // Ranked on the last closed bar, so the allocation never uses the price it is
    // about to trade at.
    const std::size_t bar = index - 1;

    std::vector<std::pair<double, std::size_t>> ranked;
    for (std::size_t a = 0; a < data.assetCount(); ++a) {
        const double r = trailingReturn(data, a, bar, lookback_);
        if (r > -1e8 && r > minReturnPct_) {
            ranked.emplace_back(r, a);
        }
    }
    if (ranked.empty()) {
        return weights;  // nothing qualifies: stay in cash
    }

    std::sort(ranked.begin(), ranked.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
    const std::size_t take = std::min(topN_, ranked.size());
    // Divided by topN_ rather than by `take`, so a period where only one asset
    // qualifies holds one-third and cash, not everything in that one asset.
    const double each = 1.0 / static_cast<double>(topN_);
    for (std::size_t i = 0; i < take; ++i) {
        weights[ranked[i].second] = each;
    }
    return weights;
}

/* ------------------------------ RiskParity ------------------------------ */

RiskParity::RiskParity(std::size_t lookback, double maxWeight)
    : lookback_(lookback)
    , maxWeight_(maxWeight) {}

std::string RiskParity::name() const {
    return "Risk parity (" + std::to_string(lookback_) + ")";
}

void RiskParity::init(const PortfolioData&) {}

std::size_t RiskParity::warmupPeriod() const {
    return lookback_ + 2;
}

std::vector<double> RiskParity::targetWeights(const PortfolioData& data, std::size_t index) {
    std::vector<double> weights(data.assetCount(), 0.0);
    if (index == 0) {
        return weights;
    }
    const std::size_t bar = index - 1;

    std::vector<double> inverse(data.assetCount(), 0.0);
    double              total = 0.0;
    for (std::size_t a = 0; a < data.assetCount(); ++a) {
        if (!data.available[a][bar]) {
            continue;
        }
        const double vol = volatility(data, a, bar, lookback_);
        if (vol <= 1e-9) {
            continue;  // a series that has not moved gives no basis to size against
        }
        inverse[a] = 1.0 / vol;
        total += inverse[a];
    }
    if (total <= 0.0) {
        return weights;
    }

    // Normalise, then cap. Whatever the cap frees stays in cash rather than being
    // pushed back onto the other assets: redistributing it would breach the same
    // cap for whoever received it, and holding cash is the honest reading of "no
    // asset may be more than maxWeight of this".
    for (std::size_t a = 0; a < weights.size(); ++a) {
        weights[a] = std::min(inverse[a] / total, maxWeight_);
    }
    return weights;
}

/* ------------------------------ EqualWeight ------------------------------ */

std::vector<double> EqualWeight::targetWeights(const PortfolioData& data, std::size_t index) {
    std::vector<double> weights(data.assetCount(), 0.0);
    if (index == 0) {
        return weights;
    }
    const std::size_t bar = index - 1;

    std::size_t live = 0;
    for (std::size_t a = 0; a < data.assetCount(); ++a) {
        if (data.available[a][bar]) {
            ++live;
        }
    }
    if (live == 0) {
        return weights;
    }
    for (std::size_t a = 0; a < data.assetCount(); ++a) {
        if (data.available[a][bar]) {
            weights[a] = 1.0 / static_cast<double>(live);
        }
    }
    return weights;
}

std::vector<std::unique_ptr<IPortfolioStrategy>> standardStrategySet() {
    std::vector<std::unique_ptr<IPortfolioStrategy>> out;
    out.push_back(std::make_unique<EqualWeight>());
    out.push_back(std::make_unique<RiskParity>(63, 0.4));
    out.push_back(std::make_unique<RiskParity>(126, 0.25));
    for (std::size_t top : {1u, 2u, 3u, 5u}) {
        for (std::size_t look : {63u, 126u, 252u}) {
            out.push_back(std::make_unique<MomentumRotation>(top, look, 0.0));
        }
    }
    return out;
}

}  // namespace portfolio
