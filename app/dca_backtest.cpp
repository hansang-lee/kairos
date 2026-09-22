#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "common/util.hpp"
#include "portfolio/price_cache.hpp"
#include "yfinance.hpp"

/**
 * Accumulation rules: buy a fixed number of shares every day, and more when the
 * market looks cheap or frightened.
 *
 * This is a different question from the rest of the backtests here, and the
 * numbers are not comparable to theirs. Those start with a fixed sum and ask what
 * it became; this one puts new money in every single day, so its "return" depends
 * mostly on when the money went in. A rule buying one share a day commits thirty
 * times more capital per purchase after a fund has risen thirtyfold, which means
 * most of the money arrives late and the headline profit understates what the
 * early purchases did. Both figures are reported for that reason.
 */
namespace {

struct Series {
    std::vector<int64_t> ts;
    std::vector<double>  close;
};

/** Daily closes for a ticker, from cache when it covers the range and Yahoo otherwise. */
Series load(const std::string& ticker, const std::string& cacheKey, const std::string& from, const std::string& to) {
    Series out;
    auto   cached = portfolio::loadCachedDaily(cacheKey);
    const int64_t wanted = portfolio::parseDate(from);
    if (!cached || cached->timestamps.empty() || cached->timestamps.front() > wanted + 14 * 86400) {
        const auto fetched = yFinance::getStockInfo(ticker, from, to, "1d");
        if (!fetched || fetched->close.empty()) {
            return out;
        }
        auto copy   = *fetched;
        copy.ticker = cacheKey;
        portfolio::saveCachedDaily(copy);
        cached = std::make_shared<StockInfo>(copy);
    }
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

std::string dayOf(int64_t ts) {
    const std::time_t t = static_cast<std::time_t>(ts);
    std::tm           tm{};
    gmtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

struct Result {
    std::string name;
    double      invested     = 0.0;
    double      finalValue   = 0.0;
    int64_t     shares       = 0;
    double      irrPct       = 0.0;
    double      worstUnreal  = 0.0;  ///< deepest the holding sat below what was paid in
    double      mddPct       = 0.0;  ///< peak-to-trough of the holding's market value
    int64_t     purchases    = 0;
    double      peakInvested = 0.0;
};

/**
 * Annualised money-weighted return.
 *
 * Total profit over invested is meaningless when the money arrives on 2,500
 * different days: a won that worked for nine years and one that worked for nine
 * days would count the same. This discounts each purchase by how long it was
 * actually at work.
 */
double irr(const std::vector<std::pair<int64_t, double>>& flows, int64_t endTs, double finalValue) {
    auto npv = [&](double rate) {
        double v = -finalValue;
        for (const auto& [ts, amount] : flows) {
            const double years = static_cast<double>(endTs - ts) / (365.25 * 86400.0);
            v += amount * std::pow(1.0 + rate, years);
        }
        return v;
    };
    double lo = -0.95, hi = 5.0;
    if (npv(lo) * npv(hi) > 0.0) {
        return 0.0;
    }
    for (int i = 0; i < 200; ++i) {
        const double mid = (lo + hi) / 2.0;
        (npv(lo) * npv(mid) <= 0.0 ? hi : lo) = mid;
    }
    return (lo + hi) / 2.0 * 100.0;
}

/**
 * @param sharesFor Shares to buy on a bar, given the index. Returning 0 skips the day.
 */
Result runDca(const std::string& name, const Series& px, const std::function<int(std::size_t)>& sharesFor) {
    Result r;
    r.name = name;

    std::vector<std::pair<int64_t, double>> flows;
    int64_t                                 held = 0;
    double                                  paid = 0.0;
    double                                  peakValue = 0.0;

    for (std::size_t i = 0; i < px.close.size(); ++i) {
        const int qty = sharesFor(i);
        if (qty > 0) {
            const double cost = qty * px.close[i];
            held += qty;
            paid += cost;
            flows.emplace_back(px.ts[i], cost);
            ++r.purchases;
        }
        const double value = static_cast<double>(held) * px.close[i];
        peakValue          = std::max(peakValue, value);
        if (peakValue > 0.0) {
            r.mddPct = std::min(r.mddPct, (value - peakValue) / peakValue * 100.0);
        }
        if (paid > 0.0) {
            r.worstUnreal = std::min(r.worstUnreal, (value - paid) / paid * 100.0);
        }
    }

    r.invested     = paid;
    r.peakInvested = paid;
    r.shares       = held;
    r.finalValue   = static_cast<double>(held) * px.close.back();
    r.irrPct       = irr(flows, px.ts.back(), r.finalValue);
    return r;
}

void printRow(const Result& r) {
    std::cout << std::left << std::setw(34) << r.name.substr(0, 33) << std::right << std::fixed << std::setprecision(0)
              << std::setw(12) << r.invested << std::setw(13) << r.finalValue << std::setprecision(1) << std::setw(10)
              << (r.invested > 0.0 ? (r.finalValue / r.invested - 1.0) * 100.0 : 0.0) << std::setw(9) << r.irrPct
              << std::setw(10) << r.mddPct << std::setw(11) << r.worstUnreal << std::setprecision(0) << std::setw(9)
              << r.shares << std::setw(9) << r.purchases << "\n";
}

void printUsage() {
    std::cout << "Usage:\n"
              << "  dca_backtest [--ticker QLD] [--base QQQ] [--start YYYY-MM-DD] [--end YYYY-MM-DD]\n"
              << "               [--vix-threshold 22]\n\n"
              << "  Buys shares every day, and more when price is below its 20- or 120-day average\n"
              << "  or the market is frightened. Compares that against buying the same one share a\n"
              << "  day with no rules, against the unleveraged fund, and against one lump sum.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string ticker = "QLD", base = "QQQ", start = "2016-01-01", end = "2026-09-22";
    double      vixThreshold = 22.0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--ticker" && i + 1 < argc)
            ticker = argv[++i];
        else if (arg == "--base" && i + 1 < argc)
            base = argv[++i];
        else if (arg == "--start" && i + 1 < argc)
            start = argv[++i];
        else if (arg == "--end" && i + 1 < argc)
            end = argv[++i];
        else if (arg == "--vix-threshold" && i + 1 < argc)
            vixThreshold = std::stod(argv[++i]);
        else {
            printUsage();
            return (arg == "--help" || arg == "-h") ? 0 : 1;
        }
    }

    yFinance::init();
    util::Defer cleanup([] { yFinance::close(); });

    // Fetched from well before the start so the 120-day average is warm on day one
    // rather than absent for the first six months of the run.
    const std::string fetchFrom = std::to_string(std::stoi(start.substr(0, 4)) - 1) + start.substr(4);

    const Series lev  = load(ticker, ticker, fetchFrom, end);
    const Series spot = load(base, base, fetchFrom, end);
    const Series vix  = load("^VIX", "VIX", fetchFrom, end);
    if (lev.close.empty() || spot.close.empty()) {
        std::cerr << "[-] Could not load prices." << std::endl;
        return 1;
    }

    std::map<int64_t, double> vixByDay;
    for (std::size_t i = 0; i < vix.ts.size(); ++i) {
        vixByDay[vix.ts[i]] = vix.close[i];
    }

    const int64_t from = portfolio::parseDate(start);

    // Rules read the previous bar and fill on this one, the convention every other
    // backtest here follows, so a signal is never taken from the price it trades at.
    auto rules = [&](const Series& s, std::size_t i) -> int {
        if (i == 0 || s.ts[i] < from) {
            return 0;
        }
        const std::size_t b = i - 1;
        int               qty = 1;

        double sum20 = 0.0;
        if (b >= 19) {
            for (std::size_t k = b - 19; k <= b; ++k) {
                sum20 += s.close[k];
            }
            if (s.close[b] < sum20 / 20.0) {
                ++qty;
            }
        }
        double sum120 = 0.0;
        if (b >= 119) {
            for (std::size_t k = b - 119; k <= b; ++k) {
                sum120 += s.close[k];
            }
            if (s.close[b] < sum120 / 120.0) {
                ++qty;
            }
        }
        const auto it = vixByDay.find(s.ts[b]);
        if (it != vixByDay.end() && it->second >= vixThreshold) {
            ++qty;
        }
        return qty;
    };

    auto plain = [&](const Series& s, std::size_t i) -> int { return (i > 0 && s.ts[i] >= from) ? 1 : 0; };

    std::cout << "==========================================================================================\n"
              << " Daily accumulation  " << dayOf(std::max(from, lev.ts.front())) << " ~ " << dayOf(lev.ts.back())
              << "\n rules: 1 share a day, +1 below the 20-day average, +1 below the 120-day average,\n"
              << "        +1 when VIX >= " << std::fixed << std::setprecision(0) << vixThreshold
              << " (standing in for CNN Extreme Fear, which only\n"
              << "        publishes one year of history: on that year the proxy is right 87% of the\n"
              << "        times it fires and catches 63% of the real ones)\n"
              << "==========================================================================================\n\n"
              << std::left << std::setw(34) << "" << std::right << std::setw(12) << "invested" << std::setw(13)
              << "final" << std::setw(10) << "profit%" << std::setw(9) << "IRR%" << std::setw(10) << "MDD%"
              << std::setw(11) << "worst P/L" << std::setw(9) << "shares" << std::setw(9) << "buys" << "\n"
              << std::string(107, '-') << "\n";

    const auto fourRule = runDca(ticker + ", 4 rules", lev, [&](std::size_t i) { return rules(lev, i); });
    printRow(fourRule);
    printRow(runDca(ticker + ", 1 share a day", lev, [&](std::size_t i) { return plain(lev, i); }));
    printRow(runDca(base + ", 4 rules", spot, [&](std::size_t i) { return rules(spot, i); }));
    printRow(runDca(base + ", 1 share a day", spot, [&](std::size_t i) { return plain(spot, i); }));

    // The same total money, all of it on day one: what the accumulation schedule
    // itself cost or earned, separately from which fund was chosen.
    for (const auto& [name, s] : {std::pair{ticker, &lev}, std::pair{base, &spot}}) {
        std::size_t firstBar = 0;
        while (firstBar < s->ts.size() && s->ts[firstBar] < from) {
            ++firstBar;
        }
        if (firstBar >= s->close.size()) {
            continue;
        }
        const int64_t qty = static_cast<int64_t>(fourRule.invested / s->close[firstBar]);
        Result        r;
        r.name       = name + ", lump sum on day one";
        r.invested   = static_cast<double>(qty) * s->close[firstBar];
        r.shares     = qty;
        r.finalValue = static_cast<double>(qty) * s->close.back();
        r.purchases  = 1;
        const double years = static_cast<double>(s->ts.back() - s->ts[firstBar]) / (365.25 * 86400.0);
        r.irrPct = years > 0.0 ? (std::pow(r.finalValue / r.invested, 1.0 / years) - 1.0) * 100.0 : 0.0;
        double peak = 0.0;
        for (std::size_t i = firstBar; i < s->close.size(); ++i) {
            const double v = static_cast<double>(qty) * s->close[i];
            peak           = std::max(peak, v);
            r.mddPct       = std::min(r.mddPct, (v - peak) / peak * 100.0);
            r.worstUnreal  = std::min(r.worstUnreal, (v - r.invested) / r.invested * 100.0);
        }
        printRow(r);
    }

    std::cout << "\n invested is what was actually paid in; profit% is against that. IRR is the annual\n"
              << " rate each won earned for the time it was in, which is the only figure comparable to\n"
              << " the CAGR columns elsewhere. worst P/L is the deepest the holding ever sat below what\n"
              << " had been paid in by then — the number that decides whether the plan gets abandoned.\n"
              << " No commission, tax or dividends are modelled here.\n";
    return 0;
}
