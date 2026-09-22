#include "portfolio/portfolio_engine.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace portfolio {

double expenseRatioFor(const PortfolioConfigBt& config, std::size_t index) {
    const double annual =
        index < config.expenseRatios.size() ? config.expenseRatios[index] : config.defaultExpenseRatio;
    // A negative ratio is a typo in a config, not a rebate a fund pays out.
    return std::max(annual, 0.0);
}

namespace {

/**
 * Take one bar's management fee out of the holdings.
 *
 * Applied as a haircut on the share count rather than a debit to cash because
 * that is where a fund's fee lands in reality — inside the net asset value,
 * shrinking what each share is worth. Charging cash instead would break whenever
 * the account is fully invested, and would let a strategy escape the fee by
 * holding no cash, which is not an escape available to anyone.
 */
void accrueExpenses(const PortfolioConfigBt& config, const PortfolioData& data, std::size_t bar,
                    std::vector<double>& shares, double& totalFees) {
    if (config.barsPerYear <= 0.0) {
        return;
    }
    for (std::size_t a = 0; a < shares.size(); ++a) {
        if (shares[a] <= 0.0) {
            continue;
        }
        // Clamped because a mistyped ratio should cost at most the whole position
        // for the bar, never turn the holding negative.
        const double drag = std::clamp(expenseRatioFor(config, a) / config.barsPerYear, 0.0, 1.0);
        totalFees += shares[a] * data.close[a][bar] * drag;
        shares[a] *= (1.0 - drag);
    }
}

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
    result.warmupBars        = warmup;

    double              cash = initialCapital_;
    std::vector<double> shares(assets, 0.0);

    result.equityCurve.reserve(data.barCount());

    for (std::size_t bar = 0; bar < data.barCount(); ++bar) {
        // Before the bar is valued, so the curve shows what the holdings are worth
        // after the fee rather than a day before it.
        if (bar >= 1) {
            accrueExpenses(config, data, bar, shares, result.totalFees);
        }

        // Borrowing is charged before the bar is valued, for the same reason the fee
        // is: the interest is owed for holding overnight, not for trading today.
        if (bar >= 1 && cash < 0.0 && config.marginRateAnnual > 0.0 && config.barsPerYear > 0.0) {
            const double interest = -cash * config.marginRateAnnual / config.barsPerYear;
            cash -= interest;
            result.totalInterest += interest;
        }

        double invested = 0.0;
        for (std::size_t a = 0; a < assets; ++a) {
            invested += shares[a] * data.close[a][bar];
        }
        double equity = cash + invested;

        // A levered account can be wiped out. Recording the zero and stopping is the
        // honest end of the run: letting the arithmetic go negative and recover
        // would describe a loan that no broker leaves outstanding.
        //
        // The curve is floored at zero rather than showing the debit balance, so a
        // wipeout reads as -100% and not as some larger number. That is generous to
        // leverage — a real account can end owing money — but a drawdown deeper than
        // everything you had is not a figure the rest of the metrics can use.
        if (equity <= 0.0 && config.maxLeverage > 1.0) {
            result.ruined = true;
            std::fill(shares.begin(), shares.end(), 0.0);
            cash = 0.0;
            result.equityCurve.insert(result.equityCurve.end(), data.barCount() - bar, 0.0);
            break;
        }

        result.equityCurve.push_back(equity);

        if (bar < warmup || equity <= 0.0) {
            continue;
        }

        // A margin call is not a decision, so it happens whatever the rebalancing
        // schedule says — and it sells after the fall, which is where most of
        // leverage's real cost lands rather than in the interest line.
        if (config.marginCallLeverage > 0.0 && invested > equity * config.marginCallLeverage) {
            const double keep = equity * config.maxLeverage / invested;
            for (std::size_t a = 0; a < assets; ++a) {
                const double price = data.close[a][bar];
                if (shares[a] <= 0.0 || price <= 0.0) {
                    continue;
                }
                const double sellShares = shares[a] * (1.0 - keep);
                const double gross      = sellShares * price;
                const double net =
                    gross * (1.0 - config.slippagePct) * (1.0 - config.commissionRate) * (1.0 - config.sellTaxRate);
                cash += net;
                shares[a] -= sellShares;
                result.totalCosts += gross - net;
                ++result.orders;
            }
            ++result.marginCalls;
            equity = cash;
            for (std::size_t a = 0; a < assets; ++a) {
                equity += shares[a] * data.close[a][bar];
            }
        }
        const bool rebalanceBar = config.rebalanceEveryBars <= 0
                               || ((bar - warmup) % static_cast<std::size_t>(config.rebalanceEveryBars) == 0);
        if (!rebalanceBar) {
            continue;
        }

        auto target = strategy.targetWeights(data, bar);
        target.resize(assets, 0.0);

        // A target summing above the exposure ceiling is normalised down to it
        // rather than borrowed against without limit. The relative weights the
        // strategy asked for are preserved; only the total is capped.
        const double sum = std::accumulate(target.begin(), target.end(), 0.0);
        if (sum > config.maxLeverage + 1e-9) {
            const double scale = config.maxLeverage / sum;
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

                const double scale   = std::max(wantVal, heldVal);
                const bool   opening = heldVal <= 0.0 && wantVal > 0.0;
                const bool   closing = wantVal <= 0.0 && heldVal > 0.0;
                // Opening and closing are the allocation itself, not drift around it, so the
                // drift band must not veto them. Either band breaching is enough: one measures
                // the error against the account, the other against the position.
                const bool worthTrading = opening || closing || std::fabs(delta) >= equity * config.minWeightChange
                                       || std::fabs(delta) >= scale * config.minPositionDrift;
                if (!worthTrading) {
                    continue;
                }
                const bool selling = delta < 0.0;
                if ((pass == 0) != selling) {
                    continue;
                }

                if (selling) {
                    // Whole shares, like the account this is meant to describe. With
                    // 24 assets and ten million won a position is a handful of shares,
                    // so rounding is not a rounding error here — it is the difference
                    // between six shares and seven.
                    const double sellShares = std::floor(std::min(shares[a], -delta / price));
                    if (sellShares < 1.0) {
                        continue;
                    }
                    const double gross = sellShares * price;
                    const double net =
                        gross * (1.0 - config.slippagePct) * (1.0 - config.commissionRate) * (1.0 - config.sellTaxRate);
                    cash += net;
                    shares[a] -= sellShares;
                    result.totalCosts += gross - net;
                } else {
                    // Spending may run the cash balance negative, but only as far as
                    // the exposure ceiling allows; unlevered that floor is zero and
                    // this is the same test as before.
                    const double borrowFloor = -(config.maxLeverage - 1.0) * equity;
                    const double spend       = std::min(delta, cash - borrowFloor);
                    if (spend <= 0.0) {
                        continue;
                    }
                    const double effectivePrice = price * (1.0 + config.slippagePct) * (1.0 + config.commissionRate);
                    const double bought         = std::floor(spend / effectivePrice);
                    if (bought < 1.0) {
                        continue;
                    }
                    const double paid = bought * effectivePrice;
                    shares[a] += bought;
                    cash -= paid;
                    result.totalCosts += paid * (1.0 - price / effectivePrice);
                }
                ++result.orders;
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
        const double net =
            gross * (1.0 - config.slippagePct) * (1.0 - config.commissionRate) * (1.0 - config.sellTaxRate);
        cash += net;
        // Counted like any other sale, so totalCosts means the same thing here as
        // it does for the benchmark — otherwise the two columns are not comparable.
        result.totalCosts += gross - net;
        shares[a] = 0.0;
    }

    result.finalCapital = cash;
    finalize(result, data, initialCapital_);
    return result;
}

PortfolioResult buyAndHold(const PortfolioData& data, const PortfolioConfigBt& config, double initialCapital) {
    PortfolioResult r;
    r.strategyName = "Equal-weight buy & hold";
    r.finalCapital = initialCapital;
    if (data.barCount() == 0 || data.assetCount() == 0) {
        finalize(r, data, initialCapital);
        return r;
    }

    const std::size_t   assets = data.assetCount();
    std::vector<double> shares(assets, 0.0);
    const double        per  = initialCapital / static_cast<double>(assets);
    double              cash = 0.0;

    /**
     * Slices set aside for assets that have no price yet, held as cash until the
     * asset lists and then spent on it at its first available bar.
     *
     * This is deliberately not "equal weight at each asset's listing", which would
     * resize every slice as the universe grows. It stays a 1/N-at-the-start
     * allocation in which the late arrivals simply start late: each keeps exactly
     * the slice it was given on day one, no more and no less. Equal-weight-on-entry
     * is a defensible benchmark too, but a different one, and it would make the
     * benchmark's early years depend on how many assets happen to list later.
     */
    std::vector<double> pending(assets, 0.0);

    for (std::size_t a = 0; a < assets; ++a) {
        const double price = data.close[a][0];
        // An asset with no price yet keeps its slice in cash instead of forfeiting
        // it: a benchmark that quietly starts with less capital than the strategies
        // it is compared against is not measuring the same thing. Leaving it as cash
        // forever is the other half of that same error, so it is only parked here.
        if (price <= 0.0 || !data.available[a][0]) {
            pending[a] = per;
            cash += per;
            continue;
        }
        const double effectivePrice = price * (1.0 + config.slippagePct) * (1.0 + config.commissionRate);
        // Whatever the slice cannot buy in whole shares stays as cash, which is what
        // an account holding two dozen funds actually looks like.
        shares[a]         = std::floor(per / effectivePrice);
        const double paid = shares[a] * effectivePrice;
        cash += per - paid;
        r.totalCosts += paid - shares[a] * price;
    }

    r.equityCurve.reserve(data.barCount());
    for (std::size_t bar = 0; bar < data.barCount(); ++bar) {
        if (bar >= 1) {
            accrueExpenses(config, data, bar, shares, r.totalFees);
        }

        // Deploy any slice whose asset has now listed, paying the same entry costs
        // the day-one buys paid. An asset that never lists keeps its cash to the end.
        for (std::size_t a = 0; a < assets; ++a) {
            if (pending[a] <= 0.0 || !data.available[a][bar] || data.close[a][bar] <= 0.0) {
                continue;
            }
            const double effectivePrice =
                data.close[a][bar] * (1.0 + config.slippagePct) * (1.0 + config.commissionRate);
            const double bought = std::floor(pending[a] / effectivePrice);
            if (bought < 1.0) {
                continue;  // the slice still cannot buy a share; wait rather than round up
            }
            const double paid = bought * effectivePrice;
            shares[a] += bought;
            cash -= paid;
            r.totalCosts += paid - bought * data.close[a][bar];
            pending[a] -= paid;
        }

        double equity = cash;
        for (std::size_t a = 0; a < assets; ++a) {
            equity += shares[a] * data.close[a][bar];
        }
        r.equityCurve.push_back(equity);
    }

    // The exit is paid once, after the curve is recorded: drawdown and Sharpe
    // describe what the position was worth along the way, and subtracting a sale
    // that never happened from every bar would understate both.
    const std::size_t last = data.barCount() - 1;
    for (std::size_t a = 0; a < assets; ++a) {
        if (shares[a] <= 0.0) {
            continue;
        }
        const double gross = shares[a] * data.close[a][last];
        const double net =
            gross * (1.0 - config.slippagePct) * (1.0 - config.commissionRate) * (1.0 - config.sellTaxRate);
        cash += net;
        r.totalCosts += gross - net;
        shares[a] = 0.0;
    }

    r.finalCapital = cash;
    finalize(r, data, initialCapital);
    return r;
}

}  // namespace portfolio
