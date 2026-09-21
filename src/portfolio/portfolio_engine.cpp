#include "portfolio/portfolio_engine.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace portfolio {

namespace {

/** Fill in the metrics every result shares, from the equity curve. */
void finalize(PortfolioResult& r, const PortfolioData& data, double initialCapital) {
    r.initialCapital = initialCapital;
    r.totalReturnPct = (r.finalCapital - initialCapital) / initialCapital * 100.0;

    if (data.timestamps.size() >= 2) {
        const double seconds = static_cast<double>(data.timestamps.back() - data.timestamps.front());
        const double years   = seconds / (365.25 * 86400.0);
        if (years > 0.01 && r.finalCapital > 0.0) {
            r.cagr = (std::pow(r.finalCapital / initialCapital, 1.0 / years) - 1.0) * 100.0;
        }
    }

    double peak = r.equityCurve.empty() ? initialCapital : r.equityCurve.front();
    for (const double e : r.equityCurve) {
        peak             = std::max(peak, e);
        r.maxDrawdownPct = std::min(r.maxDrawdownPct, (e - peak) / peak * 100.0);
    }

    // Annualized from daily bars, like BacktestEngine — and wrong for any other bar
    // size, for the same reason.
    if (r.equityCurve.size() > 1) {
        std::vector<double> rets;
        rets.reserve(r.equityCurve.size() - 1);
        for (std::size_t i = 1; i < r.equityCurve.size(); ++i) {
            if (r.equityCurve[i - 1] > 0.0) {
                rets.push_back((r.equityCurve[i] - r.equityCurve[i - 1]) / r.equityCurve[i - 1]);
            }
        }
        if (!rets.empty()) {
            const double mean = std::accumulate(rets.begin(), rets.end(), 0.0) / static_cast<double>(rets.size());
            double       var  = 0.0;
            for (const double x : rets) {
                var += (x - mean) * (x - mean);
            }
            var /= static_cast<double>(rets.size());
            const double sd = std::sqrt(var);
            if (sd > 1e-12) {
                r.sharpeRatio = mean / sd * std::sqrt(252.0);
            }
        }
    }
}

}  // namespace

PortfolioEngine::PortfolioEngine(double initialCapital)
    : initialCapital_(initialCapital) {}

PortfolioResult PortfolioEngine::run(IPortfolioStrategy& strategy, const PortfolioData& data,
                                     const PortfolioConfigBt& config) {
    PortfolioResult result;
    result.strategyName = strategy.name();
    result.finalCapital = initialCapital_;
    if (data.barCount() == 0 || data.assetCount() == 0) {
        finalize(result, data, initialCapital_);
        return result;
    }

    strategy.init(data);
    const std::size_t warmup = strategy.warmupPeriod();
    const std::size_t assets = data.assetCount();

    double              cash = initialCapital_;
    std::vector<double> shares(assets, 0.0);
    std::vector<double> weights(assets, 0.0);

    result.equityCurve.reserve(data.barCount());

    for (std::size_t bar = 0; bar < data.barCount(); ++bar) {
        double equity = cash;
        for (std::size_t a = 0; a < assets; ++a) {
            equity += shares[a] * data.close[a][bar];
        }
        result.equityCurve.push_back(equity);

        if (bar < warmup || equity <= 0.0) {
            continue;
        }
        const bool rebalanceBar = config.rebalanceEveryBars <= 0
                               || ((bar - warmup) % static_cast<std::size_t>(config.rebalanceEveryBars) == 0);
        if (!rebalanceBar) {
            continue;
        }

        auto target = strategy.targetWeights(data, bar);
        target.resize(assets, 0.0);

        // Leverage is not modelled, so an over-allocated target is a bug in the
        // strategy rather than something to silently normalise away.
        const double sum = std::accumulate(target.begin(), target.end(), 0.0);
        if (sum > 1.0 + 1e-9) {
            const double scale = 1.0 / sum;
            for (auto& w : target) {
                w *= scale;
            }
        }

        ++result.rebalances;

        // Sell first, so the proceeds are available to the buys in the same pass.
        for (int pass = 0; pass < 2; ++pass) {
            for (std::size_t a = 0; a < assets; ++a) {
                const double price = data.close[a][bar];
                if (price <= 0.0 || !data.available[a][bar]) {
                    continue;
                }
                const double want    = data.available[a][bar] ? target[a] : 0.0;
                const double heldVal = shares[a] * price;
                const double wantVal = equity * want;
                const double delta   = wantVal - heldVal;

                if (std::fabs(delta) < equity * config.minWeightChange) {
                    continue;
                }
                const bool selling = delta < 0.0;
                if ((pass == 0) != selling) {
                    continue;
                }

                if (selling) {
                    const double sellShares = std::min(shares[a], -delta / price);
                    const double gross      = sellShares * price;
                    const double net =
                        gross * (1.0 - config.slippagePct) * (1.0 - config.commissionRate) * (1.0 - config.sellTaxRate);
                    cash += net;
                    shares[a] -= sellShares;
                    result.totalCosts += gross - net;
                } else {
                    const double spend = std::min(delta, cash);
                    if (spend <= 0.0) {
                        continue;
                    }
                    const double effectivePrice = price * (1.0 + config.slippagePct) * (1.0 + config.commissionRate);
                    shares[a] += spend / effectivePrice;
                    cash -= spend;
                    result.totalCosts += spend * (1.0 - price / effectivePrice);
                }
                ++result.orders;
                weights[a] = want;
            }
        }
    }

    // Close everything at the last price, so the result is cash rather than a
    // position whose value depends on when you stopped looking.
    const std::size_t last = data.barCount() - 1;
    for (std::size_t a = 0; a < assets; ++a) {
        if (shares[a] <= 0.0) {
            continue;
        }
        const double gross = shares[a] * data.close[a][last];
        cash += gross * (1.0 - config.slippagePct) * (1.0 - config.commissionRate) * (1.0 - config.sellTaxRate);
        shares[a] = 0.0;
    }

    result.finalCapital = cash;
    finalize(result, data, initialCapital_);
    return result;
}

PortfolioResult buyAndHold(const PortfolioData& data, double initialCapital) {
    PortfolioResult r;
    r.strategyName = "Equal-weight buy & hold";
    r.finalCapital = initialCapital;
    if (data.barCount() == 0 || data.assetCount() == 0) {
        finalize(r, data, initialCapital);
        return r;
    }

    const std::size_t   assets = data.assetCount();
    std::vector<double> shares(assets, 0.0);
    const double        per = initialCapital / static_cast<double>(assets);
    for (std::size_t a = 0; a < assets; ++a) {
        if (data.close[a][0] > 0.0) {
            shares[a] = per / data.close[a][0];
        }
    }

    r.equityCurve.reserve(data.barCount());
    for (std::size_t bar = 0; bar < data.barCount(); ++bar) {
        double equity = 0.0;
        for (std::size_t a = 0; a < assets; ++a) {
            equity += shares[a] * data.close[a][bar];
        }
        r.equityCurve.push_back(equity);
    }
    r.finalCapital = r.equityCurve.back();
    finalize(r, data, initialCapital);
    return r;
}

}  // namespace portfolio
