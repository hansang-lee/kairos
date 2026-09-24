#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "backtest/backtest_engine.hpp"
#include "common/util.hpp"
#include "rsi_strategy.hpp"
#include "sma_crossover.hpp"
#include "yfinance.hpp"

inline std::string currentDateTimeString() {
    const std::time_t now = std::time(nullptr);
    char              mbstr[100];
    std::strftime(mbstr, sizeof(mbstr), "%Y%m%d_%H%M%S", std::localtime(&now));
    return mbstr;
}

void printSummary(std::ostream& os, const BacktestResult& result, const StockInfo& data) {
    // clang-format off
    os << "\n"
       << "=== Backtest Result: " << result.strategyName << " ===" << "\n"
       << "Ticker:         " << result.ticker << "\n"
       << "Period:         "
           << util::formatTime(data.timestamps.front()) << " ~ "
           << util::formatTime(data.timestamps.back()) << "\n"
       << std::fixed << std::setprecision(2)
       << "Initial Capital: $" << result.initialCapital << "\n"
       << "Final Capital:   $" << result.finalCapital << "\n"
       << "Peak Capital:    $" << result.peakCapital << " (" << util::formatTime(result.peakTimestamp) << ")\n"
       << "Lowest Capital:  $" << result.lowestCapital << " (" << util::formatTime(result.lowestTimestamp) << ")\n"
       << "-" << "\n"
       << "Total Return:   " << result.totalReturnPct << "%" << "\n"
       << "CAGR:           " << result.cagr << "%" << "\n"
       << "Max Drawdown:   " << result.maxDrawdownPct << "%" << "\n"
       << "Win Rate:       " << (result.winRate * 100.0) << "%"
           << " (" << std::count_if(result.trades.begin(), result.trades.end(),
                                    [](const Trade& t) { return t.returnPct > 0; })
           << "/" << result.trades.size() << ")" << "\n"
       << "Profit Factor:  " << result.profitFactor << "\n"
       << "Sharpe Ratio:   " << result.sharpeRatio << "\n"
       << "-" << "\n"
       << "SCORE:          " << result.score << " / 100" << "\n"
       << std::endl;
    // clang-format on
}

void printTrades(std::ostream& os, const BacktestResult& result, const StockInfo& data) {
    if (result.trades.empty()) {
        os << "(No trades executed)" << std::endl;
        return;
    }

    // clang-format off
    os << "=== Trades ===" << "\n"
       << std::left
       << std::setw(16) << "(Buy Date)"
       << std::setw(16) << "(Sell Date)"
       << std::setw(12) << "(Buy)"
       << std::setw(12) << "(Sell)"
       << std::setw(12) << "(Return)"
       << "\n-"
       << std::endl;
    // clang-format on

    for (const auto& trade : result.trades) {
        // clang-format off
        os << std::left
           << std::setw(16) << util::formatTime(data.timestamps[trade.buyIndex])
           << std::setw(16) << util::formatTime(data.timestamps[trade.sellIndex])
           << std::fixed << std::setprecision(2)
           << "$" << std::setw(11) << trade.buyPrice
           << "$" << std::setw(11) << trade.sellPrice
           << (trade.returnPct >= 0.0 ? "+" : "") << trade.returnPct << "%"
           << std::endl;
        // clang-format on
    }
}

void printComparison(std::ostream& os, const std::vector<BacktestResult>& results) {
    const int nameW = 22;
    const int colW  = 22;
    const int lineW = nameW + colW * static_cast<int>(results.size());

    // Header
    // clang-format off
    os << "\n"
       << std::string(lineW, '=') << "\n"
       << "  STRATEGY COMPARISON" << "\n"
       << std::string(lineW, '=') << "\n"
       << std::left << std::setw(nameW) << "";
    // clang-format on
    for (const auto& r : results) {
        os << std::left << std::setw(colW) << r.strategyName;
    }
    os << "\n" << std::string(lineW, '-') << std::endl;

    // Initial Capital
    os << std::left << std::setw(nameW) << "Initial Capital";
    for (const auto& r : results) {
        std::ostringstream oss;
        oss << "$" << std::fixed << std::setprecision(2) << r.initialCapital;
        os << std::setw(colW) << oss.str();
    }
    os << std::endl;

    // Final Capital
    os << std::left << std::setw(nameW) << "Final Capital";
    for (const auto& r : results) {
        std::ostringstream oss;
        oss << "$" << std::fixed << std::setprecision(2) << r.finalCapital;
        os << std::setw(colW) << oss.str();
    }
    os << std::endl;

    // Peak Capital
    os << std::left << std::setw(nameW) << "Peak Capital";
    for (const auto& r : results) {
        std::ostringstream oss;
        oss << "$" << std::fixed << std::setprecision(2) << r.peakCapital;
        os << std::setw(colW) << oss.str();
    }
    os << std::endl;

    // Lowest Capital
    os << std::left << std::setw(nameW) << "Lowest Capital";
    for (const auto& r : results) {
        std::ostringstream oss;
        oss << "$" << std::fixed << std::setprecision(2) << r.lowestCapital;
        os << std::setw(colW) << oss.str();
    }
    os << "\n" << std::string(lineW, '-') << std::endl;

    // Total Return
    os << std::left << std::setw(nameW) << "Total Return";
    for (const auto& r : results) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << r.totalReturnPct << "%";
        os << std::setw(colW) << oss.str();
    }
    os << std::endl;

    // CAGR
    os << std::left << std::setw(nameW) << "CAGR";
    for (const auto& r : results) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << r.cagr << "%";
        os << std::setw(colW) << oss.str();
    }
    os << std::endl;

    // Max Drawdown
    os << std::left << std::setw(nameW) << "Max Drawdown";
    for (const auto& r : results) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << r.maxDrawdownPct << "%";
        os << std::setw(colW) << oss.str();
    }
    os << std::endl;

    // Win Rate
    os << std::left << std::setw(nameW) << "Win Rate";
    for (const auto& r : results) {
        const auto wins =
            std::count_if(r.trades.begin(), r.trades.end(), [](const Trade& t) { return t.returnPct > 0; });
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << (r.winRate * 100.0) << "% (" << wins << "/" << r.trades.size()
            << ")";
        os << std::setw(colW) << oss.str();
    }
    os << std::endl;

    // Profit Factor
    os << std::left << std::setw(nameW) << "Profit Factor";
    for (const auto& r : results) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << r.profitFactor;
        os << std::setw(colW) << oss.str();
    }
    os << std::endl;

    // Sharpe Ratio
    os << std::left << std::setw(nameW) << "Sharpe Ratio";
    for (const auto& r : results) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << r.sharpeRatio;
        os << std::setw(colW) << oss.str();
    }
    os << std::endl;

    // Trades
    os << std::left << std::setw(nameW) << "Trades";
    for (const auto& r : results) {
        os << std::setw(colW) << r.trades.size();
    }
    os << "\n" << std::string(lineW, '-') << std::endl;

    // SCORE
    os << std::left << std::setw(nameW) << ">>> SCORE";
    for (const auto& r : results) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << r.score << " / 100";
        os << std::setw(colW) << oss.str();
    }
    os << "\n" << std::string(lineW, '=') << "\n" << std::endl;
}

int main(int argc, char* argv[]) {
    yFinance::init();
    util::Defer _cleanup([] { yFinance::close(); });

    const auto TICKER   = ((argc > 1) ? argv[1] : "SPY");
    const auto START    = ((argc > 2) ? argv[2] : "2021-01-01");
    const auto END      = ((argc > 3) ? argv[3] : "2026-01-01");
    const auto INTERVAL = ((argc > 4) ? argv[4] : "1d");

    const auto data = yFinance::getStockInfo(TICKER, START, END, INTERVAL);
    if (!data) {
        std::cerr << "Failed to fetch stock data." << std::endl;
        return 1;
    }

    std::clog << "Fetched " << data->close.size() << " data points for " << data->ticker << std::endl;

    BacktestEngine engine(10000.0);

    // Strategy 1: SMA Crossover (20/50)
    SmaCrossover sma(20, 50);
    const auto   smaResult = engine.run(sma, *data);

    // Strategy 2: RSI (14, 30/70)
    RsiStrategy rsi(14, 30.0, 70.0);
    const auto  rsiResult = engine.run(rsi, *data);

    // Generate output stringstream
    std::stringstream reportSS;

    printSummary(reportSS, smaResult, *data);
    printTrades(reportSS, smaResult, *data);
    printSummary(reportSS, rsiResult, *data);
    printTrades(reportSS, rsiResult, *data);
    printComparison(reportSS, {smaResult, rsiResult});

    const std::string reportStr = reportSS.str();

    // 1. Output to console
    std::clog << reportStr;

    // 2. Save report to logs/ directory
    try {
        std::filesystem::create_directories("logs");

        const std::string dtStr         = currentDateTimeString();
        const std::string logPath       = "logs/backtest_" + std::string(TICKER) + "_" + dtStr + ".log";
        const std::string latestLogPath = "logs/backtest_latest.log";

        std::ofstream logFile(logPath);
        if (logFile.is_open()) {
            logFile << reportStr;
            logFile.close();
            std::clog << "[LOG] Report saved to: " << logPath << std::endl;
        }

        std::ofstream latestLogFile(latestLogPath);
        if (latestLogFile.is_open()) {
            latestLogFile << reportStr;
            latestLogFile.close();
            std::clog << "[LOG] Report saved to: " << latestLogPath << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[WARNING] Failed to save log file: " << e.what() << std::endl;
    }

    return 0;
}
