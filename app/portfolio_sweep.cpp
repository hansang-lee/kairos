#include <algorithm>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "portfolio/portfolio_engine.hpp"
#include "portfolio/price_cache.hpp"
#include "portfolio/strategies.hpp"

/**
 * Compares strategies that allocate one account across several assets, against
 * equal-weight buy-and-hold of the same assets.
 *
 * Separate from `sweep`, which asks whether a signal works on each ticker
 * independently. The question here is different: whether deciding *how much* of
 * each to hold beats holding all of them.
 */
namespace {

void printRow(const portfolio::PortfolioResult& r) {
    std::cout << std::left << std::setw(34) << r.strategyName << std::right << std::fixed << std::setprecision(1)
              << std::setw(10) << r.totalReturnPct << std::setw(9) << r.cagr << std::setw(10) << r.maxDrawdownPct
              << std::setprecision(2) << std::setw(9)
              << (r.maxDrawdownPct != 0.0 ? r.totalReturnPct / -r.maxDrawdownPct : 0.0) << std::setw(9) << r.sharpeRatio
              << std::setprecision(0) << std::setw(9) << r.orders << std::setw(12) << r.totalCosts << std::setw(12)
              << r.totalFees << "\n";
}

void printHeader() {
    std::cout << std::left << std::setw(34) << "" << std::right << std::setw(10) << "return%" << std::setw(9) << "CAGR"
              << std::setw(10) << "MDD%" << std::setw(9) << "ret/MDD" << std::setw(9) << "sharpe" << std::setw(9)
              << "orders" << std::setw(12) << "costs" << std::setw(12) << "fees" << "\n"
              << std::string(114, '-') << "\n";
}

void printLeverageHeader() {
    std::cout << std::left << std::setw(34) << "" << std::right << std::setw(7) << "lev" << std::setw(10) << "return%"
              << std::setw(9) << "CAGR" << std::setw(10) << "MDD%" << std::setw(9) << "sharpe" << std::setw(12)
              << "interest" << std::setw(8) << "calls" << std::setw(8) << "ruined" << "\n"
              << std::string(107, '-') << "\n";
}

void printLeverageRow(const portfolio::PortfolioResult& r, double leverage) {
    std::cout << std::left << std::setw(34) << r.strategyName.substr(0, 33) << std::right << std::fixed
              << std::setprecision(1) << std::setw(7) << leverage << std::setw(10) << r.totalReturnPct << std::setw(9)
              << r.cagr << std::setw(10) << r.maxDrawdownPct << std::setprecision(2) << std::setw(9) << r.sharpeRatio
              << std::setprecision(0) << std::setw(12) << r.totalInterest << std::setw(8) << r.marginCalls
              << std::setw(8) << (r.ruined ? "YES" : "-") << "\n";
}

void printUsage() {
    std::cout << "Usage:\n"
              << "  portfolio_sweep [--universe <path>] [--start YYYY-MM-DD] [--end YYYY-MM-DD]\n"
              << "                  [--rebalance <bars>] [--rebalance-sweep] [--expense-ratio <fraction>]\n\n"
              << "  Allocates one account across the universe and compares that against holding\n"
              << "  all of it in equal weight. Prices come from cache/daily/, which `sweep` fills.\n"
              << "  --rebalance-sweep repeats the comparison at several rebalancing intervals, to\n"
              << "  separate the strategy's edge from the cost of chasing its target.\n"
              << "  --equity-sweep sets the equity share explicitly, from all-bonds to all-equity,\n"
              << "  using the universe's equity_classes. Redrawing a taxonomy sets this number\n"
              << "  without admitting to setting it; this shows the whole trade-off instead.\n"
              << "  --leverage-sweep borrows against each strategy at several multiples, charging\n"
              << "  --margin-rate for the loan and cutting the position back at --margin-call.\n"
              << "  --expense-ratio is the annual management fee assumed for any asset whose entry\n"
              << "  in the universe does not state one. It defaults to 0.3%, the middle of the\n"
              << "  published range for KRX index ETFs, because assuming zero is not neutral: it\n"
              << "  is a claim that funds are free, and it flatters whatever stays invested.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string universePath   = "config/universe_index.json";
    std::string startDate      = "2015-01-01";
    std::string endDate        = "2026-01-01";
    int         rebalance      = 21;
    bool        rebalanceSweep = false;
    double      defaultExpense = 0.003;
    bool        leverageSweep  = false;
    bool        equitySweep    = false;
    double      marginRate     = 0.06;
    double      marginCall     = 0.0;

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
        else if (arg == "--rebalance-sweep")
            rebalanceSweep = true;
        else if (arg == "--leverage-sweep")
            leverageSweep = true;
        else if (arg == "--equity-sweep")
            equitySweep = true;
        else if (arg == "--margin-rate" && i + 1 < argc)
            marginRate = std::stod(argv[++i]);
        else if (arg == "--margin-call" && i + 1 < argc)
            marginCall = std::stod(argv[++i]);
        else if (arg == "--expense-ratio" && i + 1 < argc)
            defaultExpense = std::stod(argv[++i]);
        else {
            printUsage();
            return (arg == "--help" || arg == "-h") ? 0 : 1;
        }
    }

    const auto loaded = portfolio::loadUniverse(universePath, startDate, endDate, defaultExpense);
    for (const auto& m : loaded.missing) {
        std::cerr << "  " << m << ": not cached or too short; run sweep first to fetch it\n";
    }
    if (loaded.series.empty()) {
        std::cerr << "[-] No usable price data." << std::endl;
        return 1;
    }

    const auto data = portfolio::PortfolioData::align(loaded.series);

    portfolio::PortfolioConfigBt cfg;
    cfg.expenseRatios       = loaded.expenseRatios;
    cfg.defaultExpenseRatio = defaultExpense;

    std::cout << "========================================================================================\n";
    std::cout << " Portfolio sweep  " << startDate << " ~ " << endDate << "  " << data.assetCount() << " assets, "
              << data.barCount() << " bars\n";
    if (!loaded.allFeesKnown) {
        std::cout << " management fee assumed " << std::fixed << std::setprecision(2) << defaultExpense * 100.0
                  << "%/yr where the universe does not state one\n";
    }
    std::cout << "========================================================================================\n";

    // Several intervals rather than one, because a strategy that only wins at a
    // single rebalancing frequency has been fitted to that frequency: the interval
    // is a parameter like any other, and one that costs money to get wrong.
    const std::vector<int> intervals =
        rebalanceSweep ? std::vector<int>{5, 10, 21, 42, 63, 126} : std::vector<int>{rebalance};

    portfolio::PortfolioEngine engine(10000000.0);

    // Borrowing is asked as its own question, because its answer is not a column
    // next to the others: a levered result and its unlevered twin are the same
    // rule, and the only thing worth reading is how the two differ.
    // The equity share, asked directly. Every way of redrawing the class lines was
    // really a way of setting this number, so setting it is the honest version of
    // the same experiment — and it shows the whole trade-off rather than one point
    // on it.
    if (equitySweep) {
        if (loaded.equityClasses.empty()) {
            std::cerr << "[-] " << universePath << " does not say which classes are the equity sleeve.\n";
            return 1;
        }
        cfg.rebalanceEveryBars = rebalance;
        std::vector<std::string> defensive;
        for (const auto& c : loaded.assetClasses) {
            const bool isEquity = std::find(loaded.equityClasses.begin(), loaded.equityClasses.end(), c)
                               != loaded.equityClasses.end();
            if (!isEquity && !c.empty() && std::find(defensive.begin(), defensive.end(), c) == defensive.end()) {
                defensive.push_back(c);
            }
        }

        std::cout << "\n equity sleeve: ";
        for (const auto& c : loaded.equityClasses) {
            std::cout << c << " ";
        }
        std::cout << "  | rest: ";
        for (const auto& c : defensive) {
            std::cout << c << " ";
        }
        std::cout << "\n\n";
        printHeader();
        printRow(portfolio::buyAndHold(data, cfg));

        for (const int pct : {0, 20, 30, 40, 50, 60, 70, 80, 100}) {
            for (const bool volIn : {false, true}) {
                std::vector<std::pair<std::string, double>> cw;
                for (const auto& c : loaded.equityClasses) {
                    cw.emplace_back(c, static_cast<double>(pct) / static_cast<double>(loaded.equityClasses.size()));
                }
                for (const auto& c : defensive) {
                    cw.emplace_back(c, static_cast<double>(100 - pct) / static_cast<double>(defensive.size()));
                }
                portfolio::GroupParity s(loaded.assetClasses, volIn, 63, cw);
                auto                   r = s.name();
                auto                   res = engine.run(s, data, cfg);
                res.strategyName = std::to_string(pct) + "% equity" + (volIn ? " (vol-weighted in)" : " (equal in)");
                printRow(res);
            }
        }
        return 0;
    }

    if (leverageSweep) {
        cfg.rebalanceEveryBars = rebalance;
        cfg.marginRateAnnual   = marginRate;
        cfg.marginCallLeverage = marginCall;

        std::cout << "\n borrowing at " << std::fixed << std::setprecision(1) << marginRate * 100.0 << "%/yr";
        if (marginCall > 0.0) {
            std::cout << ", called back at " << std::setprecision(2) << marginCall << "x";
        } else {
            std::cout << ", never called (optimistic: a real broker sells into the fall)";
        }
        std::cout << "\n\n";
        printLeverageHeader();

        for (const double lev : {1.0, 1.5, 2.0, 2.5, 3.0}) {
            cfg.maxLeverage = lev;
            auto strategies = portfolio::standardStrategySet(loaded.assetClasses);
            for (auto& s : strategies) {
                const std::string base = s->name();
                if (base.rfind("Momentum", 0) == 0) {
                    continue;  // measured already and not worth borrowing against
                }
                portfolio::Levered levered(std::move(s), lev);
                auto               r = engine.run(levered, data, cfg);
                r.strategyName       = base;
                printLeverageRow(r, lev);
            }
            std::cout << "\n";
        }
        return 0;
    }

    for (const int interval : intervals) {
        cfg.rebalanceEveryBars = interval;

        std::cout << "\n rebalance every " << interval << " bars\n";
        printHeader();
        printRow(portfolio::buyAndHold(data, cfg));
        auto strategies = portfolio::standardStrategySet(loaded.assetClasses);
        for (auto& s : strategies) {
            printRow(engine.run(*s, data, cfg));
        }
    }
    return 0;
}
