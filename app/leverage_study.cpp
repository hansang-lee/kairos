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
 * What a daily-reset leveraged fund does across a bust, and whether a trend rule
 * makes it survivable.
 *
 * QLD returned 33% a year over 2016-2026 and that is the best number anywhere in
 * this repository, resting entirely on an assumption nothing here has tested: that
 * the decade contains no technology collapse. It does not. QLD did not exist for
 * the last one, so the fund is reconstructed from the index it tracks, back to
 * 1985 — which does contain 2000-2002 and 2008.
 *
 * Reconstruction is only worth anything if it reproduces the fund where the fund
 * exists, so the tool checks itself against twenty years of real QLD before
 * reporting a single number from the years before it listed.
 */
namespace {

struct Series {
    std::vector<int64_t> ts;
    std::vector<double>  close;
};

Series load(const std::string& ticker, const std::string& cacheKey, const std::string& from, const std::string& to) {
    Series        out;
    auto          cached = portfolio::loadCachedDaily(cacheKey);
    const int64_t wanted = portfolio::parseDate(from);
    if (!cached || cached->timestamps.empty() || cached->timestamps.front() > wanted + 30 * 86400
        || cached->timestamps.back() < portfolio::parseDate(to) - 30 * 86400) {
        const auto fetched = yFinance::getStockInfo(ticker, from, to, "1d");
        if (!fetched || fetched->close.empty()) {
            return out;
        }
        auto copy   = *fetched;
        copy.ticker = cacheKey;
        portfolio::saveCachedDaily(copy);
        cached = std::make_shared<StockInfo>(copy);
    }
    // Sliced to the window actually asked for. Returning the whole cached series
    // instead made --start and --end silently do nothing, and two different eras
    // printed identical tables — which reads as a strategy being perfectly stable
    // rather than as a bug.
    const int64_t hi = portfolio::parseDate(to);
    for (std::size_t i = 0; i < cached->timestamps.size(); ++i) {
        if (cached->timestamps[i] < wanted || cached->timestamps[i] > hi) {
            continue;
        }
        out.ts.push_back(cached->timestamps[i]);
        out.close.push_back(cached->close[i]);
    }
    return out;
}

int yearOf(int64_t ts) {
    const std::time_t t = static_cast<std::time_t>(ts);
    std::tm           tm{};
    gmtime_r(&t, &tm);
    return tm.tm_year + 1900;
}

std::string dayOf(int64_t ts) {
    const std::time_t t = static_cast<std::time_t>(ts);
    std::tm           tm{};
    gmtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

struct Stats {
    double cagr    = 0.0;
    double mddPct  = 0.0;
    double sharpe  = 0.0;
    double finalX  = 1.0;   ///< multiple of the starting sum
    double worstYr = 0.0;
    int    switches = 0;
};

Stats measure(const std::vector<double>& equity, const std::vector<int64_t>& ts) {
    Stats s;
    if (equity.size() < 2) {
        return s;
    }
    s.finalX = equity.back() / equity.front();

    const double years = static_cast<double>(ts.back() - ts.front()) / (365.25 * 86400.0);
    if (years > 0.1 && s.finalX > 0.0) {
        s.cagr = (std::pow(s.finalX, 1.0 / years) - 1.0) * 100.0;
    }

    double peak = equity.front();
    for (const double e : equity) {
        peak     = std::max(peak, e);
        s.mddPct = std::min(s.mddPct, (e - peak) / peak * 100.0);
    }

    std::vector<double> r;
    r.reserve(equity.size() - 1);
    for (std::size_t i = 1; i < equity.size(); ++i) {
        if (equity[i - 1] > 0.0) {
            r.push_back(equity[i] / equity[i - 1] - 1.0);
        }
    }
    if (!r.empty()) {
        const double m = std::accumulate(r.begin(), r.end(), 0.0) / static_cast<double>(r.size());
        double       v = 0.0;
        for (const double x : r) {
            v += (x - m) * (x - m);
        }
        const double sd = std::sqrt(v / static_cast<double>(r.size()));
        if (sd > 1e-12) {
            s.sharpe = m / sd * std::sqrt(252.0);
        }
    }

    // Worst calendar year, which is how a drawdown is actually lived through.
    std::map<int, std::pair<double, double>> byYear;
    for (std::size_t i = 0; i < equity.size(); ++i) {
        const int y = yearOf(ts[i]);
        auto      it = byYear.find(y);
        if (it == byYear.end()) {
            byYear[y] = {i > 0 ? equity[i - 1] : equity[i], equity[i]};
        } else {
            it->second.second = equity[i];
        }
    }
    for (const auto& [y, se] : byYear) {
        (void)y;
        if (se.first > 0.0) {
            s.worstYr = std::min(s.worstYr, (se.second / se.first - 1.0) * 100.0);
        }
    }
    return s;
}

void printRow(const std::string& name, const Stats& s) {
    std::cout << std::left << std::setw(36) << name.substr(0, 35) << std::right << std::fixed << std::setprecision(1)
              << std::setw(9) << s.cagr << std::setw(10) << s.mddPct << std::setprecision(2) << std::setw(9) << s.sharpe
              << std::setprecision(1) << std::setw(11) << s.worstYr << std::setprecision(0);
    if (s.finalX >= 1000.0) {
        std::cout << std::setw(13) << s.finalX;
    } else {
        std::cout << std::setw(13) << std::setprecision(1) << s.finalX;
    }
    std::cout << std::setw(9) << s.switches << "\n";
}

void printHeader() {
    std::cout << std::left << std::setw(36) << "" << std::right << std::setw(9) << "CAGR%" << std::setw(10) << "MDD%"
              << std::setw(9) << "sharpe" << std::setw(11) << "worst yr" << std::setw(13) << "x money"
              << std::setw(9) << "trades" << "\n"
              << std::string(97, '-') << "\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string index = "^NDX", indexKey = "NDX", start = "1985-10-01", end = "2026-09-22";
    double      leverage = 2.0, expenseAnnual = 0.0095, financingSpread = 0.004, switchCost = 0.0005;
    int         maWindow = 200;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--index" && i + 1 < argc) {
            index = argv[++i];
            indexKey = index;
            indexKey.erase(std::remove(indexKey.begin(), indexKey.end(), '^'), indexKey.end());
        } else if (a == "--start" && i + 1 < argc)
            start = argv[++i];
        else if (a == "--end" && i + 1 < argc)
            end = argv[++i];
        else if (a == "--leverage" && i + 1 < argc)
            leverage = std::stod(argv[++i]);
        else if (a == "--ma" && i + 1 < argc)
            maWindow = std::stoi(argv[++i]);
        else if (a == "--expense" && i + 1 < argc)
            expenseAnnual = std::stod(argv[++i]);
        else if (a == "--spread" && i + 1 < argc)
            financingSpread = std::stod(argv[++i]);
        else {
            std::cout << "Usage:\n  leverage_study [--index ^NDX] [--start YYYY-MM-DD] [--end YYYY-MM-DD]\n"
                      << "                 [--leverage 2] [--ma 200] [--expense 0.0095] [--spread 0.004]\n\n"
                      << "  Reconstructs a daily-reset leveraged fund from its index, checks the\n"
                      << "  reconstruction against real QLD, and asks whether a moving-average rule\n"
                      << "  makes leverage survivable across the busts the fund's own history misses.\n";
            return (a == "--help" || a == "-h") ? 0 : 1;
        }
    }

    yFinance::init();
    util::Defer cleanup([] { yFinance::close(); });

    const Series idx  = load(index, indexKey, start, end);
    const Series bill = load("^IRX", "IRX", start, end);
    const Series qld  = load("QLD", "QLD", "2006-01-01", end);
    if (idx.close.size() < 500 || bill.close.empty()) {
        std::cerr << "[-] Not enough history." << std::endl;
        return 1;
    }

    // The short rate on each day, carried forward: a leveraged fund borrows at
    // roughly the bill rate plus a spread, and over this span that rate ran from
    // 8% to zero. Holding it fixed would make the 1980s look like the 2010s.
    std::map<int64_t, double> rateByDay;
    for (std::size_t i = 0; i < bill.ts.size(); ++i) {
        rateByDay[bill.ts[i]] = bill.close[i] / 100.0;
    }
    auto rateOn = [&](int64_t ts) {
        auto it = rateByDay.upper_bound(ts);
        if (it == rateByDay.begin()) {
            return 0.05;
        }
        return std::prev(it)->second;
    };

    // ---- Reconstruct the fund ----
    std::vector<double> synth(idx.close.size(), 1.0);
    for (std::size_t i = 1; i < idx.close.size(); ++i) {
        const double idxRet = idx.close[i] / idx.close[i - 1] - 1.0;
        const double cost   = ((leverage - 1.0) * (rateOn(idx.ts[i - 1]) + financingSpread) + expenseAnnual) / 252.0;
        synth[i]            = synth[i - 1] * (1.0 + leverage * idxRet - cost);
        if (synth[i] < 0.0) {
            synth[i] = 0.0;  // a daily-reset fund cannot go below zero, it is wound up
        }
    }

    std::cout << "==================================================================================\n"
              << " Leverage across the busts   " << dayOf(idx.ts.front()) << " ~ " << dayOf(idx.ts.back()) << "   "
              << index << ", " << idx.close.size() << " days\n"
              << " reconstruction: " << std::fixed << std::setprecision(1) << leverage
              << "x daily reset, borrowing at the 13-week bill + " << std::setprecision(2) << financingSpread * 100.0
              << "%, fund fee " << expenseAnnual * 100.0 << "%/yr\n"
              << "==================================================================================\n\n";

    // ---- Check the reconstruction against the real thing ----
    if (!qld.close.empty() && std::fabs(leverage - 2.0) < 1e-9 && index == "^NDX") {
        std::map<int64_t, double> synthByDay;
        for (std::size_t i = 0; i < idx.ts.size(); ++i) {
            synthByDay[idx.ts[i]] = synth[i];
        }
        std::vector<double> realR, synthR;
        double              firstReal = 0.0, firstSynth = 0.0, lastReal = 0.0, lastSynth = 0.0;
        for (std::size_t i = 1; i < qld.ts.size(); ++i) {
            const auto a = synthByDay.find(qld.ts[i - 1]);
            const auto b = synthByDay.find(qld.ts[i]);
            if (a == synthByDay.end() || b == synthByDay.end() || a->second <= 0.0) {
                continue;
            }
            realR.push_back(qld.close[i] / qld.close[i - 1] - 1.0);
            synthR.push_back(b->second / a->second - 1.0);
            if (firstReal == 0.0) {
                firstReal  = qld.close[i - 1];
                firstSynth = a->second;
            }
            lastReal  = qld.close[i];
            lastSynth = b->second;
        }
        if (realR.size() > 100) {
            const std::size_t n  = realR.size();
            const double      mr = std::accumulate(realR.begin(), realR.end(), 0.0) / static_cast<double>(n);
            const double      ms = std::accumulate(synthR.begin(), synthR.end(), 0.0) / static_cast<double>(n);
            double            cov = 0.0, vr = 0.0, vs = 0.0, te = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                cov += (realR[i] - mr) * (synthR[i] - ms);
                vr += (realR[i] - mr) * (realR[i] - mr);
                vs += (synthR[i] - ms) * (synthR[i] - ms);
                te += (realR[i] - synthR[i]) * (realR[i] - synthR[i]);
            }
            std::cout << " Reconstruction against real QLD, " << n << " overlapping days since "
                      << dayOf(qld.ts.front()) << ":\n"
                      << "   daily return correlation   " << std::setprecision(4) << cov / std::sqrt(vr * vs) << "\n"
                      << "   tracking error             " << std::setprecision(2)
                      << std::sqrt(te / static_cast<double>(n)) * std::sqrt(252.0) * 100.0 << "%/yr\n"
                      << "   real QLD grew              " << std::setprecision(1) << lastReal / firstReal << "x\n"
                      << "   reconstruction grew        " << lastSynth / firstSynth << "x\n\n";
        }
    }

    // ---- Strategies ----
    const std::size_t warm = static_cast<std::size_t>(maWindow);
    if (idx.close.size() <= warm + 10) {
        std::cerr << "[-] Not enough history for a " << maWindow << "-day average." << std::endl;
        return 1;
    }

    /** Equity curve for holding `asset` whenever `inMarket(i)` says so, else cash at the bill rate. */
    auto walk = [&](const std::vector<double>& asset, const std::function<bool(std::size_t)>& inMarket, int* switches) {
        std::vector<double> eq;
        eq.reserve(idx.close.size() - warm);
        double value = 1.0;
        bool   held  = false;
        eq.push_back(value);
        for (std::size_t i = warm + 1; i < idx.close.size(); ++i) {
            // Decided on the previous close and applied to this day's move, so the
            // rule never acts on the return it is about to receive.
            const bool want = inMarket(i - 1);
            if (want != held) {
                value *= (1.0 - switchCost);
                held = want;
                if (switches != nullptr) {
                    ++*switches;
                }
            }
            if (held && asset[i - 1] > 0.0) {
                value *= asset[i] / asset[i - 1];
            } else if (!held) {
                value *= 1.0 + rateOn(idx.ts[i - 1]) / 252.0;
            }
            eq.push_back(value);
        }
        return eq;
    };

    std::vector<int64_t> ts(idx.ts.begin() + static_cast<std::ptrdiff_t>(warm), idx.ts.end());

    // The trend test itself: is the index above its own moving average.
    std::vector<double> ma(idx.close.size(), 0.0);
    {
        double running = 0.0;
        for (std::size_t i = 0; i < idx.close.size(); ++i) {
            running += idx.close[i];
            if (i >= warm) {
                running -= idx.close[i - warm];
            }
            if (i >= warm - 1) {
                ma[i] = running / static_cast<double>(warm);
            }
        }
    }
    auto above  = [&](std::size_t i) { return ma[i] > 0.0 && idx.close[i] > ma[i]; };
    auto always = [](std::size_t) { return true; };

    const std::string levName = std::to_string(static_cast<int>(leverage)) + "x";

    // Each curve is built once and then sliced, rather than rebuilt per window: the
    // whole point of the trend rule is what it was holding when a window began, and
    // restarting it at each window start would hand it a flat position it did not have.
    int                              swIdx = 0, swLev = 0;
    std::vector<std::pair<std::string, std::vector<double>>> curves;
    curves.emplace_back("Index, held throughout (1x)", walk(idx.close, always, nullptr));
    curves.emplace_back(levName + " fund, held throughout", walk(synth, always, nullptr));
    curves.emplace_back("Index, only above the " + std::to_string(maWindow) + "d average",
                        walk(idx.close, above, &swIdx));
    curves.emplace_back(levName + ", only above the " + std::to_string(maWindow) + "d average",
                        walk(synth, above, &swLev));

    printHeader();
    for (std::size_t c = 0; c < curves.size(); ++c) {
        auto st = measure(curves[c].second, ts);
        if (c == 2) {
            st.switches = swIdx;
        }
        if (c == 3) {
            st.switches = swLev;
        }
        printRow(curves[c].first, st);
    }

    // ---- The periods that matter ----
    struct Window {
        const char* name;
        const char* from;
        const char* to;
    };
    const Window windows[] = {
        {"1987 crash", "1987-08-01", "1988-06-30"},   {"dot-com bust", "2000-03-01", "2002-12-31"},
        {"2008 crisis", "2007-10-01", "2009-06-30"},  {"2022 rate shock", "2021-11-01", "2023-01-31"},
        {"the tested decade", "2016-01-01", "2026-01-01"},
    };

    std::cout << "\n What each one did through the falls (CAGR% / MDD%)\n"
              << std::left << std::setw(22) << "" << std::right << std::setw(18) << "index 1x" << std::setw(18)
              << (std::to_string(static_cast<int>(leverage)) + "x held") << std::setw(18) << "index + trend"
              << std::setw(18) << (std::to_string(static_cast<int>(leverage)) + "x + trend") << "\n"
              << std::string(94, '-') << "\n";

    for (const auto& w : windows) {
        const int64_t lo = portfolio::parseDate(w.from), hi = portfolio::parseDate(w.to);
        std::size_t   a = 0, b = ts.size();
        while (a < ts.size() && ts[a] < lo) {
            ++a;
        }
        b = a;
        while (b < ts.size() && ts[b] <= hi) {
            ++b;
        }
        if (b <= a + 20) {
            std::cout << std::left << std::setw(22) << w.name << "  (outside the data)\n";
            continue;
        }
        std::cout << std::left << std::setw(22) << w.name << std::right << std::fixed << std::setprecision(1);
        const std::vector<int64_t> tslice(ts.begin() + static_cast<std::ptrdiff_t>(a),
                                          ts.begin() + static_cast<std::ptrdiff_t>(b));
        for (const auto& [name, curve] : curves) {
            (void)name;
            const std::vector<double> slice(curve.begin() + static_cast<std::ptrdiff_t>(a),
                                            curve.begin() + static_cast<std::ptrdiff_t>(b));
            const auto st = measure(slice, tslice);
            std::cout << std::setw(10) << st.cagr << std::setw(8) << st.mddPct;
        }
        std::cout << "\n";
    }

    std::cout << "\n The trend rule pays " << std::setprecision(2) << switchCost * 100.0
              << "% each time it switches, and holds the 13-week bill while it is out.\n"
              << " No tax is modelled; in Korea a gain on a US fund is taxed differently from a\n"
              << " domestic ETF, and the trend rule realises gains far more often than holding does.\n";
    return 0;
}
