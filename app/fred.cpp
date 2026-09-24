#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <vector>

#include "common/util.hpp"
#include "yfinance.hpp"

void printTable(const std::vector<std::shared_ptr<FredSeriesInfo>>& seriesList) {
    /* Collect all unique dates */
    std::set<std::string> dateSet;
    for (const auto& s : seriesList) {
        if (!s)
            continue;
        for (const auto& d : s->dates) {
            dateSet.insert(d);
        }
    }

    /* Build date -> value lookup for each series */
    std::vector<std::map<std::string, double>> lookups(seriesList.size());
    for (std::size_t i = 0; i < seriesList.size(); i++) {
        if (!seriesList[i])
            continue;
        for (std::size_t j = 0; j < seriesList[i]->dates.size(); j++) {
            lookups[i][seriesList[i]->dates[j]] = seriesList[i]->values[j];
        }
    }

    constexpr int colW = 14;

    /* Header */
    // clang-format off
    std::clog << std::left << std::setw(14) << "(Date)";
    for (const auto& s : seriesList) {
        std::clog << std::right << std::setw(colW) << (s ? s->seriesId : "N/A");
    }
    std::clog << std::endl;

    std::clog << std::string(14 + colW * static_cast<int>(seriesList.size()), '-') << std::endl;
    // clang-format on

    /* Rows */
    for (const auto& date : dateSet) {
        std::clog << std::left << std::setw(14) << date;
        for (std::size_t i = 0; i < seriesList.size(); i++) {
            auto it = lookups[i].find(date);
            if (it != lookups[i].end()) {
                std::clog << std::right << std::fixed << std::setprecision(2) << std::setw(colW) << it->second;
            } else {
                std::clog << std::right << std::setw(colW) << "-";
            }
        }
        std::clog << std::endl;
    }
}

int main() {
    const char* apiKey = std::getenv("FRED_API_KEY");
    if (!apiKey || std::string(apiKey).empty()) {
        std::cerr << "Error: FRED_API_KEY environment variable is not set.\n"
                  << "Get your free API key at: https://fred.stlouisfed.org/docs/api/api_key.html\n"
                  << "Usage: export FRED_API_KEY=<your_key>" << std::endl;
        return 1;
    }

    yFinance::init();
    util::Defer _cleanup([] { yFinance::close(); });

    /* Fetch last 12 months of data (monthly frequency) */
    const auto unrate   = yFinance::getFredSeries("UNRATE", apiKey, "2025-01-01", "2026-02-01", "m");
    const auto fedfunds = yFinance::getFredSeries("FEDFUNDS", apiKey, "2025-01-01", "2026-02-01", "m");
    const auto gfdebtn  = yFinance::getFredSeries("GFDEBTN", apiKey, "2025-01-01", "2026-02-01", "m");
    const auto wm2ns    = yFinance::getFredSeries("WM2NS", apiKey, "2025-01-01", "2026-02-01", "m");
    const auto m2real   = yFinance::getFredSeries("M2REAL", apiKey, "2025-01-01", "2026-02-01", "m");
    const auto dexko    = yFinance::getFredSeries("DEXKOUS", apiKey, "2025-01-01", "2026-02-01", "m");

    printTable({unrate, fedfunds, gfdebtn, wm2ns, m2real, dexko});

    return 0;
}
