#include "portfolio/strategies.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

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

/* --------------------------- MovingAverageFilter --------------------------- */

MovingAverageFilter::MovingAverageFilter(std::unique_ptr<IPortfolioStrategy> inner, std::size_t window,
                                         std::vector<std::string> assetClasses,
                                         std::vector<std::string> classesToFilter)
    : inner_(std::move(inner))
    , window_(std::max<std::size_t>(window, 2))
    , classes_(std::move(assetClasses))
    , filtered_(std::move(classesToFilter)) {}

std::string MovingAverageFilter::name() const {
    return inner_->name() + " + ma" + std::to_string(window_) + (filtered_.empty() ? "" : " (equity only)");
}

void MovingAverageFilter::init(const PortfolioData& data) {
    inner_->init(data);
}

std::size_t MovingAverageFilter::warmupPeriod() const {
    return std::max(inner_->warmupPeriod(), window_ + 1);
}

std::vector<double> MovingAverageFilter::targetWeights(const PortfolioData& data, std::size_t index) {
    auto weights = inner_->targetWeights(data, index);
    if (index == 0) {
        return weights;
    }
    const std::size_t bar = index - 1;

    for (std::size_t a = 0; a < weights.size() && a < data.assetCount(); ++a) {
        if (weights[a] <= 0.0) {
            continue;
        }
        if (!filtered_.empty()) {
            const bool inScope = a < classes_.size()
                              && std::find(filtered_.begin(), filtered_.end(), classes_[a]) != filtered_.end();
            if (!inScope) {
                continue;  // held regardless of its own trend
            }
        }
        // Not enough history to know the trend is not a reason to assume it is up.
        if (bar + 1 < window_ || !data.available[a][bar]) {
            weights[a] = 0.0;
            continue;
        }
        double sum = 0.0;
        for (std::size_t k = bar + 1 - window_; k <= bar; ++k) {
            sum += data.close[a][k];
        }
        const double average = sum / static_cast<double>(window_);
        if (!(average > 0.0) || data.close[a][bar] <= average) {
            weights[a] = 0.0;
        }
    }
    return weights;
}

/* ------------------------------ GroupParity ------------------------------ */

GroupParity::GroupParity(std::vector<std::string> assetClasses, bool inverseVolWithin, std::size_t lookback,
                         std::vector<std::pair<std::string, double>> classWeights)
    : classes_(std::move(assetClasses))
    , inverseVolWithin_(inverseVolWithin)
    , lookback_(lookback)
    , classWeights_(std::move(classWeights)) {}

std::string GroupParity::name() const {
    const char* within = inverseVolWithin_ ? " (vol-weighted in)" : " (equal in)";
    if (classWeights_.empty()) {
        return std::string("Group parity") + within;
    }
    double total = 0.0;
    for (const auto& [label, w] : classWeights_) {
        (void)label;
        total += w;
    }
    std::ostringstream os;
    os << "Tilted parity" << within << " " << std::fixed << std::setprecision(0) << total;
    return os.str();
}

namespace {

/** The share a class is entitled to, before it is spread over what is available. */
double configuredWeight(const std::vector<std::pair<std::string, double>>& weights, const std::string& label) {
    for (const auto& [name, w] : weights) {
        if (name == label) {
            return std::max(w, 0.0);
        }
    }
    return 0.0;
}

}  // namespace

void GroupParity::init(const PortfolioData&) {}

std::size_t GroupParity::warmupPeriod() const {
    // Equal weighting inside a class needs no history at all; the volatility
    // variant needs as much as it measures over.
    return inverseVolWithin_ ? lookback_ + 2 : 1;
}

std::vector<double> GroupParity::targetWeights(const PortfolioData& data, std::size_t index) {
    const std::size_t   assets = data.assetCount();
    std::vector<double> weights(assets, 0.0);
    if (index == 0 || classes_.size() < assets) {
        return weights;  // untagged assets cannot be grouped, and guessing would lie
    }
    const std::size_t bar = index - 1;

    // Group the assets that are actually tradeable at this bar. A class whose
    // every member lists later simply does not exist yet, and its share belongs to
    // the classes that do rather than to cash.
    std::vector<std::string>              labels;
    std::vector<std::vector<std::size_t>> members;
    for (std::size_t a = 0; a < assets; ++a) {
        if (!data.available[a][bar] || classes_[a].empty()) {
            continue;
        }
        const auto it = std::find(labels.begin(), labels.end(), classes_[a]);
        if (it == labels.end()) {
            labels.push_back(classes_[a]);
            members.push_back({a});
        } else {
            members[static_cast<std::size_t>(it - labels.begin())].push_back(a);
        }
    }
    if (labels.empty()) {
        return weights;
    }

    // The configured shares are renormalised over the classes that actually exist
    // at this bar, so a class whose funds all list later does not leave the book
    // holding cash it never chose to hold.
    std::vector<double> share(labels.size(), 0.0);
    double              shareTotal = 0.0;
    for (std::size_t g = 0; g < labels.size(); ++g) {
        share[g] = classWeights_.empty() ? 1.0 : configuredWeight(classWeights_, labels[g]);
        shareTotal += share[g];
    }
    if (shareTotal <= 0.0) {
        return weights;
    }

    for (std::size_t g = 0; g < members.size(); ++g) {
        const auto&  group    = members[g];
        const double perClass = share[g] / shareTotal;
        if (!inverseVolWithin_) {
            const double each = perClass / static_cast<double>(group.size());
            for (const std::size_t a : group) {
                weights[a] = each;
            }
            continue;
        }

        double              total = 0.0;
        std::vector<double> inverse(group.size(), 0.0);
        for (std::size_t i = 0; i < group.size(); ++i) {
            const double vol = volatility(data, group[i], bar, lookback_);
            if (vol > 1e-9) {
                inverse[i] = 1.0 / vol;
                total += inverse[i];
            }
        }
        // A class where nothing has moved yet falls back to equal weight rather
        // than dropping out: the class split is the decision, and it should not
        // depend on whether a volatility estimate happened to be available.
        if (total <= 0.0) {
            const double each = perClass / static_cast<double>(group.size());
            for (const std::size_t a : group) {
                weights[a] = each;
            }
            continue;
        }
        for (std::size_t i = 0; i < group.size(); ++i) {
            weights[group[i]] = perClass * inverse[i] / total;
        }
    }
    return weights;
}

/* ------------------------- AbsoluteMomentumFilter ------------------------- */

AbsoluteMomentumFilter::AbsoluteMomentumFilter(std::unique_ptr<IPortfolioStrategy> inner, std::size_t lookback,
                                               double minReturnPct)
    : inner_(std::move(inner))
    , lookback_(lookback)
    , minReturnPct_(minReturnPct) {}

std::string AbsoluteMomentumFilter::name() const {
    std::ostringstream os;
    os << inner_->name() << " + abs" << lookback_;
    return os.str();
}

void AbsoluteMomentumFilter::init(const PortfolioData& data) {
    inner_->init(data);
}

std::size_t AbsoluteMomentumFilter::warmupPeriod() const {
    return std::max(inner_->warmupPeriod(), lookback_ + 1);
}

std::vector<double> AbsoluteMomentumFilter::targetWeights(const PortfolioData& data, std::size_t index) {
    auto weights = inner_->targetWeights(data, index);
    if (index == 0) {
        return weights;
    }
    const std::size_t bar = index - 1;

    for (std::size_t a = 0; a < weights.size() && a < data.assetCount(); ++a) {
        if (weights[a] <= 0.0) {
            continue;
        }
        const double r = trailingReturn(data, a, bar, lookback_);
        // An unrankable asset fails the test rather than passing it by default:
        // not knowing whether something has been falling is not a reason to hold it.
        if (!(r > -1e8) || r <= minReturnPct_) {
            weights[a] = 0.0;
        }
    }
    return weights;
}

Levered::Levered(std::unique_ptr<IPortfolioStrategy> inner, double multiple)
    : inner_(std::move(inner)),
      multiple_(multiple) {}

std::string Levered::name() const {
    std::ostringstream os;
    os << inner_->name() << " x" << std::fixed << std::setprecision(1) << multiple_;
    return os.str();
}

void Levered::init(const PortfolioData& data) { inner_->init(data); }

std::size_t Levered::warmupPeriod() const { return inner_->warmupPeriod(); }

std::vector<double> Levered::targetWeights(const PortfolioData& data, std::size_t index) {
    auto w = inner_->targetWeights(data, index);
    for (double& x : w) {
        x *= multiple_;
    }
    return w;
}

std::vector<std::unique_ptr<IPortfolioStrategy>> standardStrategySet(const std::vector<std::string>& assetClasses) {
    std::vector<std::unique_ptr<IPortfolioStrategy>> out;
    out.push_back(std::make_unique<EqualWeight>());
    out.push_back(std::make_unique<RiskParity>(63, 0.4));
    out.push_back(std::make_unique<RiskParity>(126, 0.25));

    // Only when the universe tags its assets. A grouping rule fed untagged assets
    // would report a diversification it does not have.
    if (!assetClasses.empty()) {
        out.push_back(std::make_unique<GroupParity>(assetClasses, false));
        out.push_back(std::make_unique<GroupParity>(assetClasses, true, 63));
        out.push_back(std::make_unique<AbsoluteMomentumFilter>(
            std::make_unique<GroupParity>(assetClasses, false), 252, 0.0));
        out.push_back(std::make_unique<AbsoluteMomentumFilter>(
            std::make_unique<GroupParity>(assetClasses, true, 63), 252, 0.0));
    }

    out.push_back(std::make_unique<AbsoluteMomentumFilter>(std::make_unique<EqualWeight>(), 252, 0.0));
    out.push_back(std::make_unique<AbsoluteMomentumFilter>(std::make_unique<RiskParity>(63, 0.4), 252, 0.0));
    out.push_back(std::make_unique<MovingAverageFilter>(std::make_unique<EqualWeight>(), 200));
    if (!assetClasses.empty()) {
        out.push_back(std::make_unique<MovingAverageFilter>(std::make_unique<GroupParity>(assetClasses, false), 200));
        out.push_back(std::make_unique<MovingAverageFilter>(std::make_unique<GroupParity>(assetClasses, true, 63), 200));
    }

    for (std::size_t top : {1u, 2u, 3u, 5u}) {
        for (std::size_t look : {63u, 126u, 252u}) {
            out.push_back(std::make_unique<MomentumRotation>(top, look, 0.0));
        }
    }
    // The same ranking, but standing aside when nothing is actually rising.
    for (std::size_t top : {3u, 5u}) {
        out.push_back(std::make_unique<MomentumRotation>(top, 126, 0.0001));
        out.push_back(std::make_unique<MomentumRotation>(top, 252, 0.0001));
    }
    return out;
}

}  // namespace portfolio
