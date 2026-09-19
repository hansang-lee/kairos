#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>

#include "backtest/backtest_engine.hpp"
#include "data/bar_recorder.hpp"
#include "strategy/strategy_factory.hpp"

/**
 * Backtests a profile against the intraday bars in data/bars/ — the archive that
 * bar_collect seeds and scalp_trade extends.
 *
 * The point of this app existing separately is the warning it prints: a scalping
 * result from a handful of days is not evidence, and the same numbers presented
 * without that context would be read as if they were.
 */
namespace {

std::string kstDate(int daysAgo) {
    const std::time_t  kst = std::time(nullptr) + 9 * 3600 - static_cast<std::time_t>(daysAgo) * 86400;
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&kst), "%Y-%m-%d");
    return oss.str();
}

void printUsage() {
    std::cout << "Usage:\n"
              << "  scalp_backtest --id <portfolio-id> [--start YYYY-MM-DD] [--end YYYY-MM-DD]\n"
              << "                 [--gross] [--config <path>]\n\n"
              << "  --gross runs with zero commission, tax and slippage, to separate the strategy's\n"
              << "  raw edge from the cost of trading it.\n"
              << "  Runs a profile against the archived intraday bars rather than daily ones.\n"
              << "  Seed the archive with bar_collect; scalp_trade adds to it every session.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string configPath = "config/portfolio.json";
    int         targetId   = -1;
    std::string startDate  = "2000-01-01";
    bool        gross      = false;
    std::string endDate    = kstDate(0);

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--id" && i + 1 < argc) {
            targetId = std::stoi(argv[++i]);
        } else if (arg == "--start" && i + 1 < argc) {
            startDate = argv[++i];
        } else if (arg == "--end" && i + 1 < argc) {
            endDate = argv[++i];
        } else if (arg == "--gross") {
            gross = true;
        } else if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else {
            printUsage();
            return (arg == "--help" || arg == "-h") ? 0 : 1;
        }
    }
    if (targetId == -1) {
        printUsage();
        return 1;
    }

    const auto  config  = PortfolioConfig::loadFromFile(configPath);
    const auto* profile = config.findById(targetId);
    if (!profile) {
        std::cerr << "[-] Strategy with ID " << targetId << " not found in " << configPath << std::endl;
        return 1;
    }

    const data::BarRecorder recorder;
    const auto              dates = recorder.storedDates(profile->ticker);
    if (dates.empty()) {
        std::cerr << "[-] No archived bars for " << profile->ticker << ".\n"
                  << "    Seed them first:  ./build/Release/app/bar_collect --interval 1m --range 5d\n";
        return 1;
    }

    const auto data = recorder.load(profile->ticker, startDate, endDate);
    if (!data || data->close.empty()) {
        std::cerr << "[-] No bars in " << startDate << " ~ " << endDate << " (archive holds " << dates.front() << " ~ "
                  << dates.back() << ")" << std::endl;
        return 1;
    }

    auto strat = profile->createStrategy();
    if (!strat) {
        std::cerr << "[-] Failed to create strategy for " << profile->name << std::endl;
        return 1;
    }
    if (data->close.size() <= strat->warmupPeriod()) {
        std::cerr << "[-] Only " << data->close.size() << " bars, strategy needs more than " << strat->warmupPeriod()
                  << std::endl;
        return 1;
    }

    // --gross separates the strategy's edge from what trading it costs. At scalping
    // frequency the two are the same order of magnitude, so net alone cannot tell
    // you whether a strategy has no edge or merely an edge too small to pay for.
    auto cfg = gross ? BacktestConfig{} : BacktestConfig::forMarket(profile->market);
    if (gross) {
        cfg.commissionRate = 0.0;
        cfg.slippagePct    = 0.0;
        cfg.sellTaxRate    = 0.0;
    }
    cfg.positionPct = profile->positionPct;
    cfg.stopLossPct = profile->stopLossPct;

    BacktestEngine engine(config.getInitialCapitalKrw());
    const auto     result = engine.run(*strat, *data, cfg);

    std::cout << "========================================================================================\n";
    std::cout << " Intraday backtest" << (gross ? " [GROSS — costs zeroed]" : "") << ": #" << profile->id << " "
              << profile->name << " (" << profile->ticker << ")\n";
    std::cout << " archive " << dates.front() << " ~ " << dates.back() << " (" << dates.size() << " day(s)), "
              << data->close.size() << " bars used\n";
    std::cout << " costs: commission " << (cfg.commissionRate * 100) << "%/side, tax " << (cfg.sellTaxRate * 100)
              << "% on sells, slippage " << (cfg.slippagePct * 100) << "%\n";
    std::cout << "========================================================================================\n";

    std::cout << std::fixed << std::setprecision(2);
    std::cout << " Total return : " << result.totalReturnPct << " %\n";
    std::cout << " Max drawdown : " << result.maxDrawdownPct << " %\n";
    std::cout << " Trades       : " << result.trades.size() << "\n";
    std::cout << " Win rate     : " << (result.winRate * 100.0) << " %\n";
    std::cout << " Profit factor: " << result.profitFactor << "\n";

    // Two things make an intraday number easy to over-read, so say both plainly.
    std::cout << "\n";
    if (dates.size() < 60) {
        std::cout << " [!] " << dates.size()
                  << " day(s) of history is far too little to judge a strategy. Intraday results\n"
                  << "     over a short window mostly describe the market that week. Keep scalp_trade\n"
                  << "     running to extend the archive — a month is a starting point, not an answer.\n";
    }
    std::cout << " [!] Sharpe is omitted: the engine annualizes by sqrt(252), which is only correct\n"
              << "     for daily bars and would be badly wrong here.\n";
    return 0;
}
