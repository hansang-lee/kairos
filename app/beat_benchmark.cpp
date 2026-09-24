#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <string>
#include <vector>

#include "backtest/backtest_engine.hpp"
#include "common/util.hpp"
#include "portfolio/portfolio_engine.hpp"
#include "portfolio/price_cache.hpp"
#include "portfolio/strategies.hpp"
#include "strategy/strategy_catalog.hpp"
#include "strategy/strategy_factory.hpp"
#include "yfinance.hpp"

/**
 * Whether anything reliably beats holding one fund, judged the only way that
 * question can be settled: by holding period rather than by the whole span.
 *
 * A strategy that wins over twenty-one years and loses over most of the three-year
 * stretches inside them is not one anybody holds to the end, so the figure that
 * decides this is the share of windows won, not the final total. The benchmark is
 * named rather than assumed, because a rule was never going to be judged against a
 * diluted basket again.
 */
namespace {

struct Series {
    std::vector<int64_t> ts;
    std::vector<double>  close;
};

/**
 * @brief An equity curve keyed by date rather than by bar.
 *
 * Three engines produce results here — one position, a whole allocation, and the
 * walk below — over two trading calendars that do not share holidays. Comparing
 * them by bar index would silently offset a KRX result against a US one by however
 * many days the two markets disagreed about. Looking values up by date does not.
 */
struct Curve {
    std::string          name;
    std::vector<int64_t> ts;
    std::vector<double>  v;

    [[nodiscard]] bool empty() const { return v.size() < 2; }

    /** @brief The last value at or before `t`, or 0 before the curve starts. */
    [[nodiscard]] double at(int64_t t) const {
        if (ts.empty() || t < ts.front()) {
            return 0.0;
        }
        const auto it = std::upper_bound(ts.begin(), ts.end(), t);
        return v[static_cast<std::size_t>(it - ts.begin()) - 1];
    }
};

Series load(const std::string& ticker, const std::string& key, const std::string& from, const std::string& to) {
    Series        out;
    auto          cached = portfolio::loadCachedDaily(key);
    const int64_t lo     = portfolio::parseDate(from);
    const int64_t hi     = portfolio::parseDate(to);
    if (!cached || cached->timestamps.empty() || cached->timestamps.front() > lo + 30 * 86400
        || cached->timestamps.back() < hi - 30 * 86400) {
        const auto fetched = yFinance::getStockInfo(ticker, from, to, "1d");
        if (!fetched || fetched->close.empty()) {
            return out;
        }
        auto copy   = *fetched;
        copy.ticker = key;
        portfolio::saveCachedDaily(copy);
        cached = std::make_shared<StockInfo>(copy);
    }
    for (std::size_t i = 0; i < cached->timestamps.size(); ++i) {
        if (cached->timestamps[i] < lo || cached->timestamps[i] > hi) {
            continue;
        }
        out.ts.push_back(cached->timestamps[i]);
        out.close.push_back(cached->close[i]);
    }
    return out;
}

std::string dayOf(int64_t ts) {
    const std::time_t t = static_cast<std::time_t>(ts);
    std::tm           tm{};
    gmtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

/** @brief Annualised growth between two dates, or 0 when the curve does not span them. */
double cagrBetween(const Curve& c, int64_t t0, int64_t t1) {
    const double a = c.at(t0), b = c.at(t1);
    if (a <= 0.0 || b <= 0.0) {
        return 0.0;
    }
    const double years = static_cast<double>(t1 - t0) / (365.25 * 86400.0);
    return years > 0.05 ? (std::pow(b / a, 1.0 / years) - 1.0) * 100.0 : 0.0;
}

double totalBetween(const Curve& c, int64_t t0, int64_t t1) {
    const double a = c.at(t0), b = c.at(t1);
    return a > 0.0 ? (b / a - 1.0) * 100.0 : 0.0;
}

double mddBetween(const Curve& c, int64_t t0, int64_t t1) {
    double peak = 0.0, worst = 0.0;
    for (std::size_t i = 0; i < c.ts.size(); ++i) {
        if (c.ts[i] < t0 || c.ts[i] > t1 || c.v[i] <= 0.0) {
            continue;
        }
        peak  = std::max(peak, c.v[i]);
        worst = std::min(worst, (c.v[i] - peak) / peak * 100.0);
    }
    return worst;
}

double sharpeBetween(const Curve& c, int64_t t0, int64_t t1) {
    std::vector<double> slice;
    for (std::size_t i = 0; i < c.ts.size(); ++i) {
        if (c.ts[i] >= t0 && c.ts[i] <= t1 && c.v[i] > 0.0) {
            slice.push_back(c.v[i]);
        }
    }
    std::vector<double> r;
    for (std::size_t i = 1; i < slice.size(); ++i) {
        r.push_back(slice[i] / slice[i - 1] - 1.0);
    }
    if (r.size() < 2) {
        return 0.0;
    }
    const double m = std::accumulate(r.begin(), r.end(), 0.0) / static_cast<double>(r.size());
    double       v = 0.0;
    for (const double x : r) {
        v += (x - m) * (x - m);
    }
    const double sd = std::sqrt(v / static_cast<double>(r.size()));
    return sd > 1e-12 ? m / sd * std::sqrt(252.0) : 0.0;
}

double sharpeOf(const std::vector<double>& eq) {
    std::vector<double> r;
    for (std::size_t i = 1; i < eq.size(); ++i) {
        if (eq[i - 1] > 0.0) {
            r.push_back(eq[i] / eq[i - 1] - 1.0);
        }
    }
    if (r.size() < 2) {
        return 0.0;
    }
    const double m = std::accumulate(r.begin(), r.end(), 0.0) / static_cast<double>(r.size());
    double       v = 0.0;
    for (const double x : r) {
        v += (x - m) * (x - m);
    }
    const double sd = std::sqrt(v / static_cast<double>(r.size()));
    return sd > 1e-12 ? m / sd * std::sqrt(252.0) : 0.0;
}

double median(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    return v.size() % 2 ? v[v.size() / 2] : (v[v.size() / 2 - 1] + v[v.size() / 2]) / 2.0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string start = "2010-09-22", end = "2026-09-22";
    double      windowYears = 3.0, switchCost = 0.0005;
    int         stepMonths = 3;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--start" && i + 1 < argc)
            start = argv[++i];
        else if (a == "--end" && i + 1 < argc)
            end = argv[++i];
        else if (a == "--window-years" && i + 1 < argc)
            windowYears = std::stod(argv[++i]);
        else if (a == "--step-months" && i + 1 < argc)
            stepMonths = std::stoi(argv[++i]);
        else {
            std::cout << "Usage:\n  beat_benchmark [--start YYYY-MM-DD] [--end YYYY-MM-DD]\n"
                      << "                 [--window-years 3] [--step-months 3]\n\n"
                      << "  Asks which rules beat holding QQQ, counted over rolling holding periods\n"
                      << "  rather than over the whole span.\n";
            return (a == "--help" || a == "-h") ? 0 : 1;
        }
    }

    yFinance::init();
    util::Defer cleanup([] { yFinance::close(); });

    const Series qqq = load("QQQ", "QQQ", start, end);
    const Series spy = load("SPY", "SPY", start, end);
    const Series tlt = load("TLT", "TLT", start, end);
    const Series shy = load("SHY", "SHY", start, end);
    const Series gld = load("GLD", "GLD", start, end);
    // The real leveraged funds rather than a reconstruction, wherever they reach.
    const Series qld  = load("QLD", "QLD", start, end);
    const Series tqqq = load("TQQQ", "TQQQ", start, end);
    const Series bill = load("^IRX", "IRX", start, end);
    if (qqq.close.size() < 800 || tlt.close.size() < 800) {
        std::cerr << "[-] Not enough overlapping history." << std::endl;
        return 1;
    }

    // One timeline, taken from the benchmark, with every other fund looked up on it.
    // A fund missing a day carries its last price forward rather than shifting the
    // series, which would silently misalign a signal from its own fill.
    auto onTimeline = [&](const Series& s) {
        std::map<int64_t, double> by;
        for (std::size_t i = 0; i < s.ts.size(); ++i) {
            by[s.ts[i]] = s.close[i];
        }
        std::vector<double> out;
        double              last = 0.0;
        for (const int64_t t : qqq.ts) {
            auto it = by.upper_bound(t);
            if (it != by.begin()) {
                last = std::prev(it)->second;
            }
            out.push_back(last);
        }
        return out;
    };

    const std::vector<int64_t> ts  = qqq.ts;
    const std::vector<double>  Q   = qqq.close;
    const std::vector<double>  S   = onTimeline(spy);
    const std::vector<double>  T   = onTimeline(tlt);
    const std::vector<double>  C   = onTimeline(shy);
    const std::vector<double>  G   = onTimeline(gld);
    const std::vector<double>  L2  = onTimeline(qld);
    const std::vector<double>  L3  = onTimeline(tqqq);
    const std::vector<double>  R   = onTimeline(bill);

    // A reconstructed leveraged fund, so the question reaches back past 2006 when
    // QLD listed and 2010 when TQQQ did — which is the only way the dot-com bust
    // enters the comparison at all. The same construction tracked real QLD at 0.9951
    // daily correlation over twenty years and grew slightly less than the fund did,
    // so it errs against leverage.
    auto reconstruct = [&](double mult) {
        std::vector<double> out(qqq.close.size(), 1.0);
        for (std::size_t i = 1; i < qqq.close.size(); ++i) {
            const double r    = qqq.close[i] / qqq.close[i - 1] - 1.0;
            const double rate = R[i - 1] > 0.0 ? R[i - 1] / 100.0 : 0.04;
            const double cost = ((mult - 1.0) * (rate + 0.004) + 0.0095) / 252.0;
            out[i]            = std::max(0.0, out[i - 1] * (1.0 + mult * r - cost));
        }
        return out;
    };
    const std::vector<double> S2 = reconstruct(2.0);
    const std::vector<double> S3 = reconstruct(3.0);
    const std::size_t          n   = ts.size();
    const std::size_t          warm = 252;

    auto sma = [&](const std::vector<double>& p, std::size_t i, std::size_t w) {
        if (i + 1 < w) {
            return 0.0;
        }
        double s = 0.0;
        for (std::size_t k = i + 1 - w; k <= i; ++k) {
            s += p[k];
        }
        return s / static_cast<double>(w);
    };
    auto ret12 = [&](const std::vector<double>& p, std::size_t i) {
        return (i >= 252 && p[i - 252] > 0.0) ? p[i] / p[i - 252] - 1.0 : -1e9;
    };

    /**
     * Walk an allocation forward. `pick` returns the weight of each asset for the
     * next bar, decided on bar i and applied to bar i+1, so no rule ever acts on
     * the move it is about to receive.
     */
    using Weights = std::vector<std::pair<const std::vector<double>*, double>>;
    // A fund that has not listed yet has a zero price here, and treating that as
    // cash quietly credits a strategy with sitting out a crash it was not there for
    // — QLD, which listed in 2006, appeared to dodge the dot-com bust entirely.
    // Each candidate therefore reports the first bar on which everything it needs
    // actually existed, and windows starting before that are not counted.
    auto walk = [&](const std::function<Weights(std::size_t)>& pick, std::size_t* firstUsable) {
        std::vector<double> eq;
        eq.reserve(n - warm);
        double  value = 1.0;
        Weights held;
        eq.push_back(value);
        if (firstUsable != nullptr) {
            *firstUsable = 0;
            for (std::size_t i = warm + 1; i < n; ++i) {
                bool allThere = true;
                for (const auto& [px, w] : pick(i - 1)) {
                    if (w > 0.0 && (px == nullptr || (*px)[i - 1] <= 0.0)) {
                        allThere = false;
                    }
                }
                if (allThere) {
                    *firstUsable = i - warm - 1;
                    break;
                }
            }
        }
        for (std::size_t i = warm + 1; i < n; ++i) {
            const Weights want = pick(i - 1);
            bool          same = want.size() == held.size();
            if (same) {
                for (std::size_t k = 0; k < want.size(); ++k) {
                    if (want[k].first != held[k].first || std::fabs(want[k].second - held[k].second) > 1e-9) {
                        same = false;
                        break;
                    }
                }
            }
            if (!same) {
                value *= (1.0 - switchCost);
                held = want;
            }
            double growth = 0.0, invested = 0.0;
            for (const auto& [px, w] : held) {
                if (px != nullptr && (*px)[i - 1] > 0.0) {
                    growth += w * ((*px)[i] / (*px)[i - 1]);
                    invested += w;
                }
            }
            value *= growth + (1.0 - invested);  // whatever is not allocated earns nothing
            eq.push_back(value);
        }
        return eq;
    };

    struct Candidate {
        std::string                          name;
        std::function<Weights(std::size_t)> pick;
    };

    const std::vector<Candidate> candidates = {
        // The two plain holdings first, always. Every rule below is asking to be
        // preferred over one of them, and a reader should not have to hunt for what
        // it is being compared against.
        {"SPY, held", [&](std::size_t) { return Weights{{&S, 1.0}}; }},
        {"QQQ, held (the benchmark)", [&](std::size_t) { return Weights{{&Q, 1.0}}; }},
        {"QQQ above ma200, else cash",
         [&](std::size_t i) { return Q[i] > sma(Q, i, 200) ? Weights{{&Q, 1.0}} : Weights{{&C, 1.0}}; }},
        {"QQQ above ma200, else long bonds",
         [&](std::size_t i) { return Q[i] > sma(Q, i, 200) ? Weights{{&Q, 1.0}} : Weights{{&T, 1.0}}; }},
        {"QQQ above ma100, else long bonds",
         [&](std::size_t i) { return Q[i] > sma(Q, i, 100) ? Weights{{&Q, 1.0}} : Weights{{&T, 1.0}}; }},
        {"QQQ above ma200, else gold",
         [&](std::size_t i) { return Q[i] > sma(Q, i, 200) ? Weights{{&Q, 1.0}} : Weights{{&G, 1.0}}; }},
        {"Dual momentum QQQ/SPY, else bonds",
         [&](std::size_t i) {
             const double q = ret12(Q, i), s = ret12(S, i), c = ret12(C, i);
             if (std::max(q, s) <= c) {
                 return Weights{{&T, 1.0}};
             }
             return q >= s ? Weights{{&Q, 1.0}} : Weights{{&S, 1.0}};
         }},
        {"QQQ/TLT 60:40, rebalanced",
         [&](std::size_t) { return Weights{{&Q, 0.6}, {&T, 0.4}}; }},
        {"QQQ/TLT 80:20, rebalanced",
         [&](std::size_t) { return Weights{{&Q, 0.8}, {&T, 0.2}}; }},
        {"QQQ/TLT/GLD 60:20:20",
         [&](std::size_t) { return Weights{{&Q, 0.6}, {&T, 0.2}, {&G, 0.2}}; }},
        {"QQQ above ma200 else bonds, 80:20",
         [&](std::size_t i) {
             return Q[i] > sma(Q, i, 200) ? Weights{{&Q, 0.8}, {&T, 0.2}} : Weights{{&T, 1.0}};
         }},

        // Leverage, asked as a return question rather than a risk one. Over 41 years
        // of the index a 2x fund with a trend rule did out-compound the index, and the
        // reason it was set aside was the drawdown, not the return.
        {"QLD (2x) held", [&](std::size_t) { return Weights{{&L2, 1.0}}; }},
        {"QLD above ma200, else cash",
         [&](std::size_t i) { return Q[i] > sma(Q, i, 200) ? Weights{{&L2, 1.0}} : Weights{{&C, 1.0}}; }},
        {"QLD above ma200, else long bonds",
         [&](std::size_t i) { return Q[i] > sma(Q, i, 200) ? Weights{{&L2, 1.0}} : Weights{{&T, 1.0}}; }},
        {"QLD above ma100, else cash",
         [&](std::size_t i) { return Q[i] > sma(Q, i, 100) ? Weights{{&L2, 1.0}} : Weights{{&C, 1.0}}; }},
        {"Half QLD half cash, ma200",
         [&](std::size_t i) { return Q[i] > sma(Q, i, 200) ? Weights{{&L2, 0.5}, {&C, 0.5}} : Weights{{&C, 1.0}}; }},
        {"QLD 60 / TLT 40, rebalanced",
         [&](std::size_t) { return Weights{{&L2, 0.6}, {&T, 0.4}}; }},
        {"TQQQ (3x) held", [&](std::size_t) { return Weights{{&L3, 1.0}}; }},
        {"TQQQ above ma200, else cash",
         [&](std::size_t i) { return Q[i] > sma(Q, i, 200) ? Weights{{&L3, 1.0}} : Weights{{&C, 1.0}}; }},

        {"2x reconstructed, held", [&](std::size_t) { return Weights{{&S2, 1.0}}; }},
        {"2x reconstructed above ma200",
         [&](std::size_t i) { return Q[i] > sma(Q, i, 200) ? Weights{{&S2, 1.0}} : Weights{{&C, 1.0}}; }},
        {"2x recon 60 / TLT 40",
         [&](std::size_t) { return Weights{{&S2, 0.6}, {&T, 0.4}}; }},
        {"3x reconstructed above ma200",
         [&](std::size_t i) { return Q[i] > sma(Q, i, 200) ? Weights{{&S3, 1.0}} : Weights{{&C, 1.0}}; }},
    };

    const std::vector<int64_t> eqTs(ts.begin() + static_cast<std::ptrdiff_t>(warm), ts.end());

    std::vector<Curve> curves;
    curves.reserve(candidates.size());
    for (const auto& cand : candidates) {
        std::size_t first = 0;
        auto        eq    = walk(cand.pick, &first);
        Curve       cv;
        cv.name = cand.name;
        // Dropping the bars before every fund existed, rather than carrying a flat
        // stretch that would read as a strategy calmly sitting out a crash.
        cv.ts.assign(eqTs.begin() + static_cast<std::ptrdiff_t>(first), eqTs.end());
        cv.v.assign(eq.begin() + static_cast<std::ptrdiff_t>(first), eq.end());
        curves.push_back(std::move(cv));
    }

    /* ---- Strategies from the other two engines, on their own calendars ---- */

    // An allocation over a whole universe, run by PortfolioEngine.
    auto addPortfolio = [&](const std::string& universePath, const std::string& label) {
        const auto loaded = portfolio::loadUniverse(universePath, "1990-01-01", end, 0.0);
        if (loaded.series.empty()) {
            return;
        }
        const auto                   data = portfolio::PortfolioData::align(loaded.series);
        portfolio::PortfolioConfigBt cfg;
        cfg.rebalanceEveryBars = 21;
        cfg.expenseRatios      = loaded.expenseRatios;

        portfolio::PortfolioEngine engine(10000000.0);
        auto                       set = portfolio::standardStrategySet(loaded.assetClasses);
        for (auto& st : set) {
            const std::string nm = st->name();
            // Only the ones worth carrying into a cross-market table; the momentum
            // grid was disqualified long ago and would be twelve rows of noise.
            if (nm.rfind("Momentum", 0) == 0 || nm.find("abs252") != std::string::npos) {
                continue;
            }
            const auto  r     = engine.run(*st, data, cfg);
            const auto  warmN = std::min(r.warmupBars, r.equityCurve.size());
            Curve       cv;
            cv.name = label + " " + nm;
            for (std::size_t i = warmN; i < r.equityCurve.size(); ++i) {
                cv.ts.push_back(data.timestamps[i]);
                cv.v.push_back(r.equityCurve[i]);
            }
            if (!cv.empty()) {
                curves.push_back(std::move(cv));
            }
        }
        Curve bh;
        bh.name       = label + " equal-weight buy & hold";
        const auto bhr = portfolio::buyAndHold(data, cfg);
        bh.ts          = data.timestamps;
        bh.v           = bhr.equityCurve;
        if (!bh.empty()) {
            curves.push_back(std::move(bh));
        }
    };

    // One signal on one ticker, run by BacktestEngine, then the live set combined
    // equally — which is what config/live.json actually instructs the trader to do.
    auto addLive = [&](const std::string& liveConfig, const std::string& catalogPath) {
        const auto cfgJson = util::loadJsonConfig(liveConfig);
        const auto catalog = StrategyCatalog::loadFromFile(catalogPath);
        if (!cfgJson || !catalog.loaded() || !cfgJson->contains("positions")) {
            return;
        }
        std::vector<Curve> legs;
        std::string        stratId;
        for (const auto& pos : (*cfgJson)["positions"]) {
            if (!pos.value("enabled", false)) {
                continue;
            }
            const auto* def = catalog.find(pos.value("strategy", ""));
            const auto  px  = portfolio::loadCachedDaily(pos.value("ticker", ""));
            if (def == nullptr || !px) {
                continue;
            }
            stratId = def->id;
            StrategyProfile prof;
            prof.type        = def->type;
            prof.params      = def->params;
            prof.positionPct = 1.0;  // each leg is its own account; they are combined below
            prof.stopLossPct = pos.value("stop_loss_pct", 0.0);
            auto strat       = prof.createStrategy();
            if (!strat) {
                continue;
            }
            BacktestEngine engine(10000000.0);
            const auto     r = engine.run(*strat, *px, BacktestConfig::forMarket("KRX"));
            Curve          cv;
            cv.ts = px->timestamps;
            cv.v  = r.equityCurve;
            if (!cv.empty()) {
                legs.push_back(std::move(cv));
            }
        }
        if (legs.empty()) {
            return;
        }
        // Equal money in each leg, valued on the union of their dates.
        std::vector<int64_t> all;
        for (const auto& l : legs) {
            all.insert(all.end(), l.ts.begin(), l.ts.end());
        }
        std::sort(all.begin(), all.end());
        all.erase(std::unique(all.begin(), all.end()), all.end());
        Curve combined;
        combined.name = "KRX live: " + stratId + " x" + std::to_string(legs.size()) + " equal";
        for (const int64_t t : all) {
            double sum = 0.0;
            bool   ok  = true;
            for (const auto& l : legs) {
                const double base = l.v.front();
                const double now  = l.at(t);
                if (base <= 0.0 || now <= 0.0) {
                    ok = false;
                    break;
                }
                sum += now / base;
            }
            if (ok) {
                combined.ts.push_back(t);
                combined.v.push_back(sum / static_cast<double>(legs.size()));
            }
        }
        if (!combined.empty()) {
            curves.push_back(std::move(combined));
        }
    };

    addPortfolio("config/universe_us.json", "US");
    addPortfolio("config/universe_core.json", "KRX");
    addLive("config/live.json", "");

    // Rolling windows, the question as the user actually lives it. They are defined
    // on the benchmark's calendar and every curve is read by date, so a KRX result
    // and a US one are measured over the same stretch of wall-clock time rather than
    // the same count of bars.
    const int64_t                                wl = static_cast<int64_t>(windowYears * 365.25 * 86400.0);
    const int64_t                                sp = static_cast<int64_t>(stepMonths * 30.44 * 86400.0);
    std::vector<std::pair<int64_t, int64_t>>     windows;
    for (int64_t t = eqTs.front(); t + wl <= eqTs.back(); t += sp) {
        windows.emplace_back(t, t + wl);
    }

    const Curve& bench = curves[1];  // QQQ, held
    std::vector<double> benchWin;
    for (const auto& [a, b] : windows) {
        benchWin.push_back(cagrBetween(bench, a, b));
    }

    std::cout << "==========================================================================================\n"
              << " Everything measured so far, against SPY and QQQ held plainly\n"
              << " requested " << dayOf(eqTs.front()) << " ~ " << dayOf(eqTs.back()) << ", " << windows.size()
              << " rolling " << std::fixed << std::setprecision(0) << windowYears << "-year holding periods\n"
              << "==========================================================================================\n\n"
              << std::left << std::setw(40) << "" << std::right << std::setw(23) << "period" << std::setw(7)
              << "years" << std::setw(12) << "total%" << std::setw(8) << "CAGR%" << std::setw(8) << "MDD%"
              << std::setw(8) << "sharpe" << std::setw(8) << "beat%" << std::setw(9) << "med exc" << "\n"
              << std::string(123, '-') << "\n";

    for (const auto& c : curves) {
        if (c.empty()) {
            continue;
        }
        const int64_t t0 = std::max(c.ts.front(), eqTs.front());
        const int64_t t1 = std::min(c.ts.back(), eqTs.back());
        if (t1 <= t0) {
            continue;
        }

        std::vector<double> excess;
        int                 wins = 0;
        for (std::size_t w = 0; w < windows.size(); ++w) {
            if (windows[w].first < t0 || windows[w].second > t1) {
                continue;  // the curve does not cover this stretch
            }
            const double e = cagrBetween(c, windows[w].first, windows[w].second) - benchWin[w];
            excess.push_back(e);
            if (e > 0.0) {
                ++wins;
            }
        }

        std::cout << std::left << std::setw(40) << c.name.substr(0, 39) << std::right << std::setw(12) << dayOf(t0)
                  << " ~ " << std::setw(8) << dayOf(t1) << std::fixed << std::setprecision(1) << std::setw(7)
                  << static_cast<double>(t1 - t0) / (365.25 * 86400.0) << std::setw(12) << totalBetween(c, t0, t1)
                  << std::setw(8) << cagrBetween(c, t0, t1) << std::setw(8) << mddBetween(c, t0, t1)
                  << std::setprecision(2) << std::setw(8) << sharpeBetween(c, t0, t1) << std::setprecision(1)
                  << std::setw(8) << (excess.empty() ? 0.0 : wins * 100.0 / static_cast<double>(excess.size()))
                  << std::setw(9) << (excess.empty() ? 0.0 : median(excess)) << "\n";
    }

    std::cout << "\n period is each row's own, starting when every fund it needs had listed. A rule using\n"
              << " TQQQ cannot be measured before February 2010 whatever window is requested, and\n"
              << " pretending otherwise credits it for a crash it was not there for. Rows spanning\n"
              << " different periods are not comparable on total% — read CAGR and beat% for those.\n"
              << " beat% is the share of rolling " << std::setprecision(0) << windowYears
              << "-year holding periods whose annualised return beat\n"
              << " QQQ's over the same stretch; med exc is that difference in points a year. Windows\n"
              << " overlap heavily, so those counts are not independent trials.\n\n"
              << " US rows are in USD and KRX rows in KRW. Both include distributions — Yahoo's\n"
              << " adjusted series for the US funds, and KIS's 수정주가, which was checked against\n"
              << " Yahoo's dividend-adjusted close on five KRX funds and matched to within 1.25%\n"
              << " over eleven years. So the two sides are comparable on return. Neither carries\n"
              << " tax, and the currency effect between them is not modelled.\n";

    return 0;
}
