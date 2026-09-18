#include <iomanip>
#include <iostream>
#include <string>

#include "data/kis_provider.hpp"

int main(int argc, char* argv[]) {
    std::string ticker    = "005930";  // Samsung Electronics
    std::string startDate = "2024-01-01";
    std::string endDate   = "2024-03-31";

    if (argc >= 2) {
        ticker = argv[1];
    }
    if (argc >= 3) {
        startDate = argv[2];
    }
    if (argc >= 4) {
        endDate = argv[3];
    }

    std::cout << "=== KIS Korean Stock Data Query ===" << std::endl;
    std::cout << "Ticker: " << ticker << " (" << startDate << " ~ " << endDate << ")" << std::endl;

    KisProvider provider;
    const auto  stock = provider.getStockInfo(ticker, startDate, endDate);

    if (!stock) {
        std::cerr << "Failed to fetch stock info for ticker: " << ticker << std::endl;
        return 1;
    }

    std::cout << "\nCurrency: " << stock->currency << " | Exchange: " << stock->exchangeName << std::endl;
    std::cout << "Data points: " << stock->close.size() << " bars\n" << std::endl;

    std::cout << std::left << std::setw(12) << "Timestamp" << std::setw(10) << "Open" << std::setw(10) << "High"
              << std::setw(10) << "Low" << std::setw(10) << "Close" << std::setw(14) << "Volume" << std::endl;
    std::cout << std::string(66, '-') << std::endl;

    const std::size_t n            = stock->close.size();
    const std::size_t displayCount = std::min<std::size_t>(n, 15);

    // Show last 15 days
    for (std::size_t i = n - displayCount; i < n; ++i) {
        std::time_t ts    = static_cast<std::time_t>(stock->timestamps[i]);
        std::tm*    tmPtr = std::gmtime(&ts);
        char        dateBuf[32];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", tmPtr);

        std::cout << std::left << std::setw(12) << dateBuf << std::setw(10) << static_cast<int64_t>(stock->open[i])
                  << std::setw(10) << static_cast<int64_t>(stock->high[i]) << std::setw(10)
                  << static_cast<int64_t>(stock->low[i]) << std::setw(10) << static_cast<int64_t>(stock->close[i])
                  << std::setw(14) << stock->volume[i] << std::endl;
    }

    return 0;
}
