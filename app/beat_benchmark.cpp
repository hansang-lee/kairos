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

#include "common/util.hpp"
#include "portfolio/price_cache.hpp"
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

double cagrOf(const std::vector<double>& eq, const std::vector<int64_t>& ts, std::size_t a, std::size_t b) {
    if (b <= a + 1 || eq[a] <= 0.0) {
        return 0.0;
    }
    const double years = static_cast<double>(ts[b - 1] - ts[a]) / (365.25 * 86400.0);
    return years > 0.05 ? (std::pow(eq[b - 1] / eq[a], 1.0 / years) - 1.0) * 100.0 : 0.0;
}

double mddOf(const std::vector<double>& eq, std::size_t a, std::size_t b) {
    double peak = eq[a], worst = 0.0;
    for (std::size_t i = a; i < b; ++i) {
        peak  = std::max(peak, eq[i]);
        worst = std::min(worst, (eq[i] - peak) / peak * 100.0);
    }
    return worst;
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

    std::vector<std::vector<double>> curves;
    std::vector<std::size_t>         firstUsable(candidates.size(), 0);
    curves.reserve(candidates.size());
    for (std::size_t c = 0; c < candidates.size(); ++c) {
        curves.push_back(walk(candidates[c].pick, &firstUsable[c]));
    }
    const std::vector<int64_t> eqTs(ts.begin() + static_cast<std::ptrdiff_t>(warm), ts.end());

    // Rolling windows, the question as the user actually lives it.
    const int64_t                                   wl = static_cast<int64_t>(windowYears * 365.25 * 86400.0);
    const int64_t                                   sp = static_cast<int64_t>(stepMonths * 30.44 * 86400.0);
    std::vector<std::pair<std::size_t, std::size_t>> windows;
    for (int64_t t = eqTs.front(); t + wl <= eqTs.back(); t += sp) {
        const auto a = static_cast<std::size_t>(std::lower_bound(eqTs.begin(), eqTs.end(), t) - eqTs.begin());
        const auto b = static_cast<std::size_t>(std::lower_bound(eqTs.begin(), eqTs.end(), t + wl) - eqTs.begin());
        if (b > a + 100) {
            windows.emplace_back(a, b);
        }
    }

    std::cout << "==========================================================================================\n"
              << " Beating a named benchmark   " << dayOf(eqTs.front()) << " ~ " << dayOf(eqTs.back()) << "\n"
              << " " << windows.size() << " rolling " << std::fixed << std::setprecision(0) << windowYears
              << "-year holding periods, stepped " << stepMonths << " months. Switching costs "
              << std::setprecision(2) << switchCost * 100.0 << "% a change.\n"
              << "==========================================================================================\n\n"
              << std::left << std::setw(34) << "" << std::right << std::setw(24) << "period" << std::setw(7)
              << "years" << std::setw(11) << "total%" << std::setw(8) << "CAGR%" << std::setw(8) << "MDD%"
              << std::setw(8) << "sharpe" << std::setw(8) << "beat%" << std::setw(10) << "med exc" << "\n"
              << std::string(118, '-') << "\n";

    constexpr std::size_t kBench = 1;  // QQQ, held
    std::vector<double>   benchWin;
    for (const auto& [a, b] : windows) {
        benchWin.push_back(cagrOf(curves[kBench], eqTs, a, b));
    }

    for (std::size_t c = 0; c < candidates.size(); ++c) {
        const std::size_t   from = firstUsable[c];
        std::vector<double> excess;
        int                 wins = 0;
        for (std::size_t w = 0; w < windows.size(); ++w) {
            if (windows[w].first < from) {
                continue;  // the funds this needs did not exist yet
            }
            const double e = cagrOf(curves[c], eqTs, windows[w].first, windows[w].second) - benchWin[w];
            excess.push_back(e);
            if (e > 0.0) {
                ++wins;
            }
        }
        const std::vector<double> lived(curves[c].begin() + static_cast<std::ptrdiff_t>(from), curves[c].end());
        const double totalPct = lived.front() > 0.0 ? (lived.back() / lived.front() - 1.0) * 100.0 : 0.0;
        const double years    = static_cast<double>(eqTs.back() - eqTs[from]) / (365.25 * 86400.0);

        std::cout << std::left << std::setw(34) << candidates[c].name.substr(0, 33) << std::right
                  << std::setw(13) << dayOf(eqTs[from]) << " ~ " << std::setw(8) << dayOf(eqTs.back())
                  << std::fixed << std::setprecision(1) << std::setw(7) << years << std::setw(11) << totalPct
                  << std::setw(8) << cagrOf(curves[c], eqTs, from, curves[c].size()) << std::setw(8)
                  << mddOf(curves[c], from, curves[c].size()) << std::setprecision(2) << std::setw(8)
                  << sharpeOf(lived) << std::setprecision(1) << std::setw(8)
                  << (excess.empty() ? 0.0 : wins * 100.0 / static_cast<double>(excess.size())) << std::setw(10)
                  << median(excess) << "\n";
    }

    std::cout << "\n period is each row's own, starting when every fund it needs had listed: a rule using\n"
              << " TQQQ cannot be measured before February 2010 whatever the requested window says, and\n"
              << " pretending otherwise credits it for a crash it was not there for. Rows with different\n"
              << " periods are not directly comparable on total% — read CAGR and beat% for those.\n"
              << " total% is the whole period's growth, CAGR the same thing compounded annually.\n"
              << " beat% is the share of rolling " << std::setprecision(0) << windowYears
              << "-year holding periods whose annualised return came out\n"
              << " above QQQ's over the same stretch; med exc is that difference in points a year.\n"
              << " Windows overlap heavily, so those counts are not independent trials.\n"
              << " No tax and no currency effect; all figures are in USD and include distributions.\n";
    return 0;
}
