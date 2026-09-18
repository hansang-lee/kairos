#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

#include "backtest/backtest_engine.hpp"
#include "common/util.hpp"
#include "data/kis_provider.hpp"
#include "strategy/strategy_factory.hpp"
#include "yfinance.hpp"

void printList(const PortfolioConfig& config) {
    std::cout << "\n========================================================================================\n";
    std::cout << " 📋 REGISTERED PORTFOLIO STRATEGIES (from config/portfolio.json)\n";
    std::cout << "========================================================================================\n";
    std::cout << std::left << std::setw(6) << "ID" << std::setw(10) << "Market" << std::setw(10) << "Ticker"
              << std::setw(10) << "Category" << std::setw(21) << "Type" << std::setw(30) << "Name" << "Description\n";
    std::cout << std::string(96, '-') << "\n";

    for (const auto& p : config.getProfiles()) {
        std::cout << std::left << std::setw(6) << ("[" + std::to_string(p.id) + "]") << std::setw(10) << p.market
                  << std::setw(10) << p.ticker << std::setw(10) << p.category << std::setw(21) << p.type
                  << std::setw(30) << p.name << p.description << "\n";
    }
    std::cout << "\nUsage:\n";
    std::cout << "  ./run_strategy --id <number>   Run specific strategy by ID (e.g., --id 1)\n";
    std::cout << "  ./run_strategy --all           Run all strategies in portfolio\n";
    std::cout << "  ./run_strategy --list          Show this list\n\n";
}

void executeProfile(const StrategyProfile& p, KisProvider& kis) {
    std::cout << "\n========================================================================================\n";
    std::cout << " 🚀 Executing Strategy #" << p.id << " : " << p.name << " (" << p.ticker << " [" << p.market
              << "])\n";
    std::cout << " Description: " << p.description << "\n";
    std::cout << " Params: " << p.params.dump() << " | Position: " << (p.positionPct * 100.0) << "%\n";
    std::cout << "========================================================================================\n";

    auto strat = p.createStrategy();
    if (!strat) {
        std::cerr << "[-] Failed to create strategy instance for " << p.name << std::endl;
        return;
    }

    std::shared_ptr<StockInfo> stock;
    BacktestConfig             bConfig;
    bConfig.stopLossPct = p.stopLossPct;

    if (p.market == "KRX") {
        bConfig.commissionRate = 0.00015;
        bConfig.slippagePct    = 0.0005;
        bConfig.positionPct    = p.positionPct;
        std::cout << "[*] Fetching Korean stock data via KisProvider..." << std::endl;
        stock = kis.getStockInfo(p.ticker, "2023-09-01", "2024-09-01");
    } else {
        bConfig.commissionRate = 0.0005;
        bConfig.slippagePct    = 0.0005;
        bConfig.positionPct    = p.positionPct;
        std::cout << "[*] Fetching US stock data via Yahoo Finance..." << std::endl;
        stock = yFinance::getStockInfo(p.ticker, "1d", "1y");
    }

    if (!stock || stock->close.empty()) {
        std::cerr << "[-] No stock data fetched for ticker: " << p.ticker << std::endl;
        return;
    }

    std::cout << "[*] Data points: " << stock->close.size() << " bars (" << stock->currency << ")" << std::endl;

    BacktestEngine engine(10000.0);
    const auto     result = engine.run(*strat, *stock, bConfig);

    std::cout << "\n--- [ Backtest Performance Metrics ] ---\n";
    std::cout << " • Total Return    : " << std::fixed << std::setprecision(2) << result.totalReturnPct << " %\n";
    std::cout << " • CAGR            : " << result.cagr << " %\n";
    std::cout << " • Max Drawdown    : " << result.maxDrawdownPct << " %\n";
    std::cout << " • Sharpe Ratio    : " << result.sharpeRatio << "\n";
    std::cout << " • Win Rate        : " << std::setprecision(1) << (result.winRate * 100.0) << " %\n";
    std::cout << " • Profit Factor   : " << std::setprecision(2) << result.profitFactor << "\n";
    std::cout << " • Total Trades    : " << result.trades.size() << " orders\n";
    std::cout << " • Composite Score : " << std::setprecision(1) << result.score << " / 100\n";
    std::cout << " • Final Capital   : " << std::fixed << std::setprecision(2) << result.finalCapital << " "
              << stock->currency << "\n";
    std::cout << " • Peak Capital    : " << result.peakCapital << " (" << util::formatTime(result.peakTimestamp)
              << ")\n";
    std::cout << " • Lowest Capital  : " << result.lowestCapital << " (" << util::formatTime(result.lowestTimestamp)
              << ")\n";

    if (!result.trades.empty()) {
        std::cout << "\nRecent Trades (up to 3):\n";
        const std::size_t start = result.trades.size() > 3 ? result.trades.size() - 3 : 0;
        for (std::size_t i = start; i < result.trades.size(); ++i) {
            const auto& t = result.trades[i];
            std::cout << "  [" << (i + 1) << "] Buy: " << t.buyPrice << " -> Sell: " << t.sellPrice << " ("
                      << (t.returnPct >= 0 ? "+" : "") << t.returnPct << "%)" << (t.stoppedOut ? " [STOP-LOSS]" : "")
                      << "\n";
        }
    }
}

int main(int argc, char* argv[]) {
    std::string configPath = "config/portfolio.json";
    int         targetId   = -1;
    bool        runAll     = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list" || arg == "-l") {
            auto cfg = PortfolioConfig::loadFromFile(configPath);
            printList(cfg);
            return 0;
        } else if (arg == "--id" && i + 1 < argc) {
            targetId = std::stoi(argv[++i]);
        } else if (arg == "--all" || arg == "-a") {
            runAll = true;
        } else if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            auto cfg = PortfolioConfig::loadFromFile(configPath);
            printList(cfg);
            return 0;
        }
    }

    auto config = PortfolioConfig::loadFromFile(configPath);
    if (config.getProfiles().empty()) {
        std::cerr << "[-] No strategy profiles found in " << configPath << std::endl;
        return 1;
    }

    if (targetId == -1 && !runAll) {
        printList(config);
        std::cout << "Tip: Run with '--id <number>' to execute a specific strategy from the list above.\n\n";
        return 0;
    }

    yFinance::init();
    util::Defer _cleanup([] { yFinance::close(); });
    KisProvider kis;

    if (runAll) {
        for (const auto& p : config.getProfiles()) {
            executeProfile(p, kis);
        }
    } else {
        const auto* profile = config.findById(targetId);
        if (!profile) {
            std::cerr << "[-] Strategy with ID " << targetId << " not found!" << std::endl;
            printList(config);
            return 1;
        }
        executeProfile(*profile, kis);
    }

    return 0;
}
