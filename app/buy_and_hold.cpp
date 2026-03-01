#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <vector>

#include "yfinance.hpp"

struct Defer {
    std::function<void()> f;
    explicit Defer(std::function<void()> f)
        : f(std::move(f)) {}
    ~Defer() {
        if (f) {
            f();
        }
    }
};

inline std::string formatTime(const int64_t timestamp) {
    const std::time_t t = static_cast<std::time_t>(timestamp);
    char              mbstr[100];
    std::strftime(mbstr, sizeof(mbstr), "%Y-%m-%d", std::localtime(&t));
    return mbstr;
}

inline int getYear(const int64_t timestamp) {
    const std::time_t t  = static_cast<std::time_t>(timestamp);
    struct tm*        tm = std::localtime(&t);
    return tm->tm_year + 1900;
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cout << "Usage: " << argv[0] << " [TICKER] [QUANTITY] [STARTDATE] [ENDDATE]\n";
        std::cout << "Example: " << argv[0] << " QLD 100 2021-01-01 2026-01-01\n";
        return 1;
    }

    const std::string TICKER   = argv[1];
    const double      QUANTITY = std::stod(argv[2]);
    const std::string START    = argv[3];
    const std::string END      = argv[4];

    yFinance::init();
    Defer _cleanup([] { yFinance::close(); });

    std::clog << "Step 1: Fetching data for " << TICKER << "..." << std::endl;
    const auto stock = yFinance::getStockInfo(TICKER, START, END, "1d");
    if (!stock || stock->close.empty()) {
        std::cerr << "Failed to fetch stock data." << std::endl;
        return 1;
    }

    const double initialPrice = stock->open.front();
    const double finalPrice   = stock->close.back();
    const double principal    = initialPrice * QUANTITY;
    const double finalValue   = finalPrice * QUANTITY;
    const double totalProfit  = finalValue - principal;
    const double totalRoi     = (principal > 0) ? (totalProfit / principal * 100.0) : 0.0;

    // Annualized Return (CAGR)
    double years = (double)(stock->timestamps.back() - stock->timestamps.front()) / (365.25 * 24 * 3600);
    double cagr  = (years > 0) ? (std::pow(finalValue / principal, 1.0 / years) - 1.0) * 100.0 : 0.0;

    // Yearly breakdown
    struct YearData {
        double open  = 0;
        double close = 0;
        bool   set   = false;
    };
    std::map<int, YearData> yearlyData;
    for (size_t i = 0; i < stock->close.size(); ++i) {
        int year = getYear(stock->timestamps[i]);
        if (!yearlyData[year].set) {
            yearlyData[year].open = stock->open[i];
            yearlyData[year].set  = true;
        }
        yearlyData[year].close = stock->close[i];
    }

    std::clog << "\n" << std::string(50, '=') << "\n";
    std::clog << "  BUY AND HOLD SUMMARY: " << TICKER << "\n";
    std::clog << std::string(50, '=') << "\n";
    std::clog << std::fixed << std::setprecision(2);
    std::clog << "Period:         " << formatTime(stock->timestamps.front()) << " ~ "
              << formatTime(stock->timestamps.back()) << "\n";
    std::clog << "Quantity:       " << QUANTITY << " shares\n";
    std::clog << "Initial Price:  $" << initialPrice << " (Open)\n";
    std::clog << "Final Price:    $" << finalPrice << " (Close)\n";
    std::clog << "-"
              << "\n";
    std::clog << "Principal:      $" << principal << "\n";
    std::clog << "Final Value:    $" << finalValue << "\n";
    std::clog << "Total Profit:   $" << totalProfit << (totalProfit >= 0 ? " (Gain)" : " (Loss)") << "\n";
    std::clog << "Total ROI:      " << totalRoi << "%\n";
    std::clog << "Annualized ROI: " << cagr << "% (CAGR)\n";

    std::clog << "\nYEARLY PERFORMANCE:\n";
    std::clog << std::left << std::setw(10) << "Year" << std::setw(15) << "Start" << std::setw(15) << "End"
              << "Return (%)"
              << "\n";
    std::clog << std::string(50, '-') << "\n";

    for (const auto& [year, data] : yearlyData) {
        double yearReturn = (data.open > 0) ? (data.close - data.open) / data.open * 100.0 : 0.0;
        std::clog << std::left << std::setw(10) << year << "$" << std::setw(14) << data.open << "$" << std::setw(14)
                  << data.close << (yearReturn >= 0 ? "+" : "") << yearReturn << "%\n";
    }
    std::clog << std::string(50, '=') << std::endl;

    return 0;
}
