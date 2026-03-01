#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <vector>

#include "indicator.hpp"
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

int main(int argc, char* argv[]) {
    yFinance::init();
    Defer _cleanup([] { yFinance::close(); });

    const std::string TICKER     = "QLD";
    const std::string START_DATE = "2021-01-01";
    const std::string END_DATE   = "2026-01-01";

    // Fetch data starting earlier to compute 120-day SMA
    // approx 252 trading days in a year, 120 days is roughly 6 months.
    const std::string FETCH_START = "2020-06-01";

    std::clog << "Step 1: Fetching QLD data (" << FETCH_START << " ~ " << END_DATE << ")..." << std::endl;
    const auto stock = yFinance::getStockInfo(TICKER, FETCH_START, END_DATE, "1d");
    if (!stock || stock->close.empty()) {
        std::cerr << "Failed to fetch stock data for " << TICKER << std::endl;
        return 1;
    }

    std::clog << "Step 2: Fetching Fear & Greed Index historical data..." << std::endl;
    const auto fng = yFinance::getFearAndGreedIndex();
    if (!fng) {
        std::cerr << "Failed to fetch Fear & Greed data." << std::endl;
        return 1;
    }

    // Map F&G timestamps to dates for easier lookup
    std::map<std::string, std::string> fngRatingsByDate;
    for (size_t i = 0; i < fng->timestamps.size(); ++i) {
        fngRatingsByDate[formatTime(fng->timestamps[i])] = fng->ratings[i];
    }

    auto getFngRating = [&](int64_t timestamp) -> std::string {
        std::string date = formatTime(timestamp);
        if (fngRatingsByDate.count(date)) {
            return fngRatingsByDate[date];
        }
        // Fallback: use the closest previous date
        auto it = fngRatingsByDate.lower_bound(date);
        if (it == fngRatingsByDate.end() && !fngRatingsByDate.empty()) {
            --it;
        } else if (it != fngRatingsByDate.begin()) {
            --it;
        }
        if (it != fngRatingsByDate.end()) {
            return it->second;
        }
        return "neutral";
    };

    std::clog << "Step 3: Computing 120-day SMA..." << std::endl;
    const size_t SMA_WINDOW = 120;
    const auto   sma120     = indicator::sma(stock->close, SMA_WINDOW);

    // Simulation variables
    double totalShares   = 0.0;
    double totalInvested = 0.0;

    // Find index for START_DATE
    size_t       startIndex = 0;
    const time_t startTs    = [](const std::string& date) {
        std::tm tm = {};
        strptime(date.c_str(), "%Y-%m-%d", &tm);
        return timegm(&tm);
    }(START_DATE);

    for (size_t i = 0; i < stock->timestamps.size(); ++i) {
        if (stock->timestamps[i] >= (int64_t)startTs) {
            startIndex = i;
            break;
        }
    }

    std::clog << "Step 4: Running DCA Simulation from " << formatTime(stock->timestamps[startIndex]) << "..."
              << std::endl;

    std::clog << std::left << std::setw(12) << "Date" << std::setw(10) << "Price" << std::setw(10) << "SMA120"
              << std::setw(15) << "F&G" << std::setw(8) << "BuyQty" << std::setw(12) << "TotalInv" << std::endl;
    std::clog << std::string(70, '-') << std::endl;

    std::ofstream csvFile("qld_dca_backtest.csv");
    if (csvFile.is_open()) {
        csvFile << "Date,Price,Quantity\n";
    }

    for (size_t i = startIndex; i < stock->close.size(); ++i) {
        // Use 'open' price for daily purchase as requested
        const double      price   = stock->open[i];
        const int64_t     ts      = stock->timestamps[i];
        const std::string dateStr = formatTime(ts);

        // Find SMA value for this day
        // sma[0] corresponds to stock[SMA_WINDOW - 1]
        double currentSma = 0.0;
        if (i >= SMA_WINDOW - 1) {
            currentSma = sma120[i - (SMA_WINDOW - 1)];
        }

        const std::string rating = getFngRating(ts);

        int  buyQty        = 1;  // Basic buy
        bool isExtremeFear = (rating == "extreme fear" || rating == "Extreme Fear");
        bool isBelowSma    = (currentSma > 0 && price < currentSma);

        if (isExtremeFear)
            buyQty += 1;
        if (isBelowSma)
            buyQty += 1;

        totalShares += buyQty;
        totalInvested += buyQty * price;

        if (csvFile.is_open()) {
            csvFile << dateStr << "," << price << "," << buyQty << "\n";
        }

        // Print every month or so to keep logs clean, or just some updates
        if (i % 60 == 0 || i == startIndex || i == stock->close.size() - 1) {
            std::clog << std::left << std::setw(12) << formatTime(ts) << std::fixed << std::setprecision(2) << "$"
                      << std::setw(9) << price << "$" << std::setw(9) << currentSma << std::setw(15) << rating
                      << std::setw(8) << buyQty << "$" << std::setw(11) << totalInvested << std::endl;
        }
    }

    const double finalPrice = stock->close.back();
    const double finalValue = totalShares * finalPrice;
    const double profit     = finalValue - totalInvested;
    const double roi        = (totalInvested > 0) ? (profit / totalInvested * 100.0) : 0.0;

    std::clog << "\n" << std::string(40, '=') << "\n";
    std::clog << "  BACKTEST SUMMARY (" << TICKER << ")\n";
    std::clog << std::string(40, '=') << "\n";
    std::clog << std::fixed << std::setprecision(2);
    std::clog << "Period:         " << formatTime(stock->timestamps[startIndex]) << " ~ "
              << formatTime(stock->timestamps.back()) << "\n";
    std::clog << "Principal:      $" << totalInvested << "\n";
    std::clog << "Final Value:    $" << finalValue << "\n";
    std::clog << "Profit:         $" << profit << (profit >= 0 ? " (Gain)" : " (Loss)") << "\n";
    std::clog << "ROI:            " << roi << "%\n";
    std::clog << "Total Shares:   " << totalShares << "\n";
    std::clog << std::string(40, '=') << std::endl;

    return 0;
}
