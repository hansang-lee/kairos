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

void printUsage() {
    std::cout << "Usage:\n"
              << "  portfolio_sweep [--universe <path>] [--start YYYY-MM-DD] [--end YYYY-MM-DD]\n"
              << "                  [--rebalance <bars>] [--rebalance-sweep] [--expense-ratio <fraction>]\n\n"
              << "  Allocates one account across the universe and compares that against holding\n"
              << "  all of it in equal weight. Prices come from cache/daily/, which `sweep` fills.\n"
              << "  --rebalance-sweep repeats the comparison at several rebalancing intervals, to\n"
              << "  separate the strategy's edge from the cost of chasing its target.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string universePath   = "config/universe_index.json";
    std::string startDate      = "2015-01-01";
    std::string endDate        = "2026-01-01";
    int         rebalance      = 21;
    bool        rebalanceSweep = false;
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
        else if (arg == "--rebalance-sweep")
            rebalanceSweep = true;
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
    const std::vector<int> intervals = rebalanceSweep ? std::vector<int>{5, 10, 21, 42, 63, 126} : std::vector<int>{rebalance};

    portfolio::PortfolioEngine engine(10000000.0);
    for (const int interval : intervals) {
        cfg.rebalanceEveryBars = interval;

        std::cout << "\n rebalance every " << interval << " bars\n";
        printHeader();
        printRow(portfolio::buyAndHold(data, cfg));
        auto strategies = portfolio::standardStrategySet();
        for (auto& s : strategies) {
            printRow(engine.run(*s, data, cfg));
        }
    }
    return 0;
}
