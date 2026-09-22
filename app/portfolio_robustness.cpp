#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "portfolio/portfolio_engine.hpp"
#include "portfolio/price_cache.hpp"
#include "portfolio/strategies.hpp"

/**
 * Asks whether an allocation strategy's edge survives the period it is measured
 * over, by cutting one long run into many overlapping stretches.
 *
 * A single ten-year number hides everything that matters for actually living
 * with a strategy: it cannot distinguish a steady 8% a year from three flat
 * years, one spectacular one, and six more flat ones. Holding period is the
 * investor's real exposure, so the questions here are the worst stretch, the
 * spread across stretches, and how often the strategy lost to simply holding
 * the same assets.
 */
namespace {

struct WindowStat {
    double returnPct = 0.0;
    double cagr      = 0.0;
    double mddPct     = 0.0;
};

/** Return, CAGR and drawdown over one contiguous stretch of an equity curve. */
WindowStat measure(const std::vector<double>& curve, const std::vector<int64_t>& ts, std::size_t first,
                   std::size_t last) {
    WindowStat w;
    if (first >= last || last > curve.size() || curve[first] <= 0.0) {
        return w;
    }
    w.returnPct = (curve[last - 1] - curve[first]) / curve[first] * 100.0;

    const double years = static_cast<double>(ts[last - 1] - ts[first]) / (365.25 * 86400.0);
    if (years > 0.05) {
        w.cagr = (std::pow(curve[last - 1] / curve[first], 1.0 / years) - 1.0) * 100.0;
    }

    double peak = curve[first];
    for (std::size_t i = first; i < last; ++i) {
        peak      = std::max(peak, curve[i]);
        w.mddPct = std::min(w.mddPct, (curve[i] - peak) / peak * 100.0);
    }
    return w;
}

/**
 * The bar a stretch is measured from, given the first bar that belongs to it.
 *
 * A calendar year has to be measured from the previous year's last close, not
 * from its own first close: the move into the first close happened during this
 * year and belongs to no other one. Measuring from `first` drops it, and the
 * printed years then no longer chain back to the curve they were read out of.
 */
std::size_t basisBar(std::size_t first) {
    return first > 0 ? first - 1 : first;
}

int yearOf(int64_t ts) {
    const std::time_t t = static_cast<std::time_t>(ts);
    std::tm           tm{};
    gmtime_r(&t, &tm);
    return tm.tm_year + 1900;
}

double median(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const std::size_t m = v.size() / 2;
    return v.size() % 2 ? v[m] : (v[m - 1] + v[m]) / 2.0;
}

void printUsage() {
    std::cout << "Usage:\n"
              << "  portfolio_robustness [--universe <path>] [--start YYYY-MM-DD] [--end YYYY-MM-DD]\n"
              << "                       [--rebalance <bars>] [--window-years <n>] [--step-months <n>]\n"
              << "                       [--expense-ratio <fraction>]\n\n"
              << "  Runs each allocation strategy once over the whole period, then reports what\n"
              << "  every calendar year and every rolling holding period looked like inside it.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string universePath   = "config/universe_index.json";
    std::string startDate      = "2016-01-01";
    std::string endDate        = "2026-01-01";
    int         rebalance      = 21;
    double      windowYears    = 3.0;
    int         stepMonths     = 3;
    double      defaultExpense = 0.0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--universe" && i + 1 < argc)
            universePath = argv[++i];
        else if (arg == "--start" && i + 1 < argc)
            startDate = argv[++i];
        else if (arg == "--end" && i + 1 < argc)
            endDate = argv[++i];
        else if (arg == "--rebalance" && i + 1 < argc)
            rebalance = std::stoi(argv[++i]);
        else if (arg == "--window-years" && i + 1 < argc)
            windowYears = std::stod(argv[++i]);
        else if (arg == "--step-months" && i + 1 < argc)
            stepMonths = std::stoi(argv[++i]);
        else if (arg == "--expense-ratio" && i + 1 < argc)
            defaultExpense = std::stod(argv[++i]);
        else {
            printUsage();
            return (arg == "--help" || arg == "-h") ? 0 : 1;
        }
    }

    const auto loaded = portfolio::loadUniverse(universePath, startDate, endDate, defaultExpense);
    if (loaded.series.empty()) {
        std::cerr << "[-] No usable price data in " << universePath << " for that window." << std::endl;
        return 1;
    }
    const auto data = portfolio::PortfolioData::align(loaded.series);

    portfolio::PortfolioConfigBt cfg;
    cfg.rebalanceEveryBars  = rebalance;
    cfg.expenseRatios       = loaded.expenseRatios;
    cfg.defaultExpenseRatio = defaultExpense;

    std::cout << "==========================================================================================\n"
              << " Robustness  " << startDate << " ~ " << endDate << "   " << data.assetCount() << " assets, "
              << data.barCount() << " bars, rebalance every " << rebalance << " bars\n"
              << " Rolling window " << windowYears << "y stepped " << stepMonths << "m";
    if (!loaded.allFeesKnown) {
        std::cout << "   [fees assumed " << defaultExpense * 100.0 << "%/yr where the universe does not state one]";
    }
    std::cout << "\n==========================================================================================\n\n";
    if (!loaded.missing.empty()) {
        std::cout << " skipped (no usable cache): ";
        for (const auto& m : loaded.missing) {
            std::cout << m << " ";
        }
        std::cout << "\n\n";
    }

    // One run per strategy over the whole history, then read the stretches out of
    // its equity curve. Re-running the strategy inside each window instead would
    // charge it a fresh warm-up every time, which is a cost no investor holding it
    // continuously actually pays.
    std::vector<portfolio::PortfolioResult> results;
    results.push_back(portfolio::buyAndHold(data, cfg));

    portfolio::PortfolioEngine engine(10000000.0);
    auto                       strategies = portfolio::standardStrategySet();
    for (auto& s : strategies) {
        results.push_back(engine.run(*s, data, cfg));
    }

    // ---- Calendar years ----
    std::vector<int>                       years;
    std::map<int, std::pair<std::size_t, std::size_t>> yearBars;  // year -> [first, last)
    for (std::size_t b = 0; b < data.barCount(); ++b) {
        const int y = yearOf(data.timestamps[b]);
        if (!yearBars.count(y)) {
            yearBars[y] = {b, b + 1};
            years.push_back(y);
        } else {
            yearBars[y].second = b + 1;
        }
    }

    std::cout << " Calendar-year return %\n";
    std::cout << std::left << std::setw(34) << "";
    for (const int y : years) {
        std::cout << std::right << std::setw(8) << y;
    }
    std::cout << std::right << std::setw(9) << "worst" << "\n"
              << std::string(34 + 8 * static_cast<int>(years.size()) + 9, '-') << "\n";

    for (const auto& r : results) {
        std::cout << std::left << std::setw(34) << r.strategyName << std::right << std::fixed << std::setprecision(1);
        bool   haveWorst = false;
        double worst     = 0.0;
        for (const int y : years) {
            const auto [f, l] = yearBars[y];
            const auto s      = basisBar(f);
            // A stretch that starts inside the warm-up is not a result: the curve is
            // pinned at the initial capital there because nothing is held yet.
            if (s < r.warmupBars) {
                std::cout << std::setw(8) << "-";
                continue;
            }
            const auto w = measure(r.equityCurve, data.timestamps, s, l);
            if (!haveWorst || w.returnPct < worst) {
                worst     = w.returnPct;
                haveWorst = true;
            }
            std::cout << std::setw(8) << w.returnPct;
        }
        if (haveWorst) {
            std::cout << std::setw(9) << worst;
        } else {
            std::cout << std::setw(9) << "-";
        }
        std::cout << "\n";
    }

    // ---- Rolling holding periods ----
    const int64_t windowSecs = static_cast<int64_t>(windowYears * 365.25 * 86400.0);
    const int64_t stepSecs   = static_cast<int64_t>(stepMonths * 30.44 * 86400.0);

    std::vector<std::pair<std::size_t, std::size_t>> windows;
    for (int64_t start = data.timestamps.front(); start + windowSecs <= data.timestamps.back(); start += stepSecs) {
        const auto f = std::lower_bound(data.timestamps.begin(), data.timestamps.end(), start);
        const auto l = std::lower_bound(data.timestamps.begin(), data.timestamps.end(), start + windowSecs);
        const auto fi = static_cast<std::size_t>(f - data.timestamps.begin());
        const auto li = static_cast<std::size_t>(l - data.timestamps.begin());
        if (li > fi + 50) {
            windows.emplace_back(fi, li);
        }
    }

    std::cout << "\n Rolling " << windowYears << "-year holding periods (" << windows.size()
              << " in the period)\n"
              << std::left << std::setw(34) << "" << std::right << std::setw(9) << "windows" << std::setw(10) << "worst"
              << std::setw(10) << "median" << std::setw(10) << "best" << std::setw(11) << "worstMDD" << std::setw(10)
              << "loss%" << std::setw(11) << "beatB&H%" << "\n"
              << std::string(105, '-') << "\n";

    // The benchmark's own windows, so "did this beat holding the basket" is
    // answered stretch by stretch rather than once at the end — a strategy can win
    // over ten years and still have lost over most of the periods inside them.
    const auto&         bench = results.front();
    std::vector<double> benchCagr(windows.size(), 0.0);
    std::vector<bool>   benchMeasured(windows.size(), false);
    for (std::size_t i = 0; i < windows.size(); ++i) {
        if (windows[i].first < bench.warmupBars) {
            continue;
        }
        benchCagr[i]     = measure(bench.equityCurve, data.timestamps, windows[i].first, windows[i].second).cagr;
        benchMeasured[i] = true;
    }

    for (const auto& r : results) {
        std::vector<double> cagrs;
        double              worstMdd   = 0.0;
        int                 losses     = 0;
        int                 beats      = 0;
        int                 comparable = 0;
        for (std::size_t i = 0; i < windows.size(); ++i) {
            // Same exclusion as the calendar table: a window overlapping the warm-up
            // would report a flat curve as a result, and would be counted as neither a
            // loss nor a win because it moved by exactly nothing.
            if (windows[i].first < r.warmupBars) {
                continue;
            }
            const auto w = measure(r.equityCurve, data.timestamps, windows[i].first, windows[i].second);
            cagrs.push_back(w.cagr);
            worstMdd = std::min(worstMdd, w.mddPct);
            if (w.returnPct < 0.0) {
                ++losses;
            }
            // Only where the benchmark is measured too, so the comparison is between
            // two invested strategies rather than against a flat stretch.
            if (benchMeasured[i]) {
                ++comparable;
                if (w.cagr > benchCagr[i]) {
                    ++beats;
                }
            }
        }
        std::cout << std::left << std::setw(34) << r.strategyName << std::right << std::fixed << std::setprecision(1);
        if (cagrs.empty()) {
            std::cout << std::setw(9) << 0 << std::setw(10) << "-" << std::setw(10) << "-" << std::setw(10) << "-"
                      << std::setw(11) << "-" << std::setw(10) << "-" << std::setw(11) << "-" << "\n";
            continue;
        }
        const double pct = 100.0 / static_cast<double>(cagrs.size());
        std::cout << std::setw(9) << cagrs.size() << std::setw(10)
                  << *std::min_element(cagrs.begin(), cagrs.end()) << std::setw(10) << median(cagrs) << std::setw(10)
                  << *std::max_element(cagrs.begin(), cagrs.end()) << std::setw(11) << worstMdd << std::setw(10)
                  << losses * pct;
        if (comparable > 0) {
            std::cout << std::setw(11) << beats * 100.0 / static_cast<double>(comparable);
        } else {
            std::cout << std::setw(11) << "-";
        }
        std::cout << "\n";
    }

    // ---- What the numbers above cost ----
    // Neither table shows what was paid for its returns, and the answer differs by
    // more than an order of magnitude across these strategies: a rotation that
    // re-picks its holdings every month pays for every pick, out of the same equity
    // curve the median column is read from.
    std::cout << "\n Full period, as a share of initial capital\n"
              << std::left << std::setw(34) << "" << std::right << std::setw(10) << "return%" << std::setw(10)
              << "costs%" << std::setw(10) << "fees%" << std::setw(12) << "rebalances" << "\n"
              << std::string(76, '-') << "\n";
    for (const auto& r : results) {
        const double base = r.initialCapital > 0.0 ? r.initialCapital : 1.0;
        // rebalances rather than orders: buyAndHold counts no orders, and a zero
        // there next to a non-zero costs% reads as a contradiction rather than as
        // "it bought once and never touched the basket again".
        std::cout << std::left << std::setw(34) << r.strategyName << std::right << std::fixed << std::setprecision(1)
                  << std::setw(10) << r.totalReturnPct << std::setprecision(2) << std::setw(10)
                  << r.totalCosts / base * 100.0 << std::setw(10) << r.totalFees / base * 100.0 << std::setw(12)
                  << r.rebalances << "\n";
    }

    // How many of these windows are actually independent: the span they cover
    // between them, divided by the length of one. Everything beyond that is the
    // same years counted again under a different label.
    const double windowMonths = windowYears * 12.0;
    const double overlapPct   = windowMonths > 0.0 ? (1.0 - stepMonths / windowMonths) * 100.0 : 0.0;
    const double spanMonths =
        windows.empty() ? 0.0 : static_cast<double>(windows.size() - 1) * stepMonths + windowMonths;
    const long independent = windowMonths > 0.0 ? std::lround(spanMonths / windowMonths) : 0;

    std::cout << "\n worst/median/best are annualised returns over the window; loss% is the share of\n"
              << " windows that ended below where they started; beatB&H% compares against equal-weight\n"
              << " buy-and-hold over the same stretch, counted only over windows where both are measured.\n"
              << " Calendar years are measured from the previous year's last close, so a row's measured\n"
              << " years chain back to the move its equity curve actually made over them.\n"
              << " A '-' means not measured: the stretch starts inside that strategy's warm-up, where it\n"
              << " holds nothing and the curve is flat at the initial capital. The windows column is how\n"
              << " many stretches survived that, which is why it differs between rows.\n"
              << " The windows overlap: consecutive ones share " << std::fixed << std::setprecision(0) << overlapPct
              << "% of their bars, so loss% and beatB&H%\n"
              << " read like independent trials and are not — these " << windows.size()
              << " windows carry roughly " << independent << " windows'\n"
              << " worth of independent information.\n";
    return 0;
}
