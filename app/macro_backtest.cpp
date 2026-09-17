#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <unistd.h>

#include <nlohmann/json.hpp>

#include "common/util.hpp"
#include "macro/macro_backtester.hpp"
#include "macro_scorer.hpp"
#include "yfinance.hpp"

/**
 * @brief Build monthly returns from StockInfo close prices.
 *        Returns vector of monthly percentage returns (0.01 = 1%).
 */
static std::vector<double> buildMonthlyReturns(const std::shared_ptr<StockInfo>& stock) {
    std::vector<double> returns;
    if (!stock || stock->close.size() < 2) {
        return returns;
    }
    returns.reserve(stock->close.size() - 1);
    for (size_t i = 1; i < stock->close.size(); ++i) {
        if (stock->close[i - 1] > 0.0) {
            returns.push_back((stock->close[i] - stock->close[i - 1]) / stock->close[i - 1]);
        } else {
            returns.push_back(0.0);
        }
    }
    return returns;
}

/**
 * @brief Build date strings (YYYY-MM-DD) from StockInfo timestamps.
 *        Skips the first timestamp since returns start from index 1.
 */
static std::vector<std::string> buildDates(const std::shared_ptr<StockInfo>& stock) {
    std::vector<std::string> dates;
    if (!stock || stock->timestamps.size() < 2) {
        return dates;
    }
    dates.reserve(stock->timestamps.size() - 1);
    for (size_t i = 1; i < stock->timestamps.size(); ++i) {
        time_t     t  = static_cast<time_t>(stock->timestamps[i]);
        struct tm* tm = gmtime(&t);
        char       buf[11];
        strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
        dates.emplace_back(buf);
    }
    return dates;
}

int main(int argc, char* argv[]) {
    const char* apiKey = std::getenv("FRED_API_KEY");
    if (!apiKey || std::string(apiKey).empty()) {
        std::cerr << "Error: FRED_API_KEY environment variable is not set." << std::endl;
        return 1;
    }

    std::string configPath = util::resolveFromExe("config/macro_allocation.json");
    if (argc > 1) {
        configPath = argv[1];
    }

    /* Load config */
    const auto configOpt = util::loadJsonConfig(configPath);
    if (!configOpt) {
        return 1;
    }
    const auto& config = *configOpt;

    const auto& bt        = config["backtest"];
    std::string startDate = bt.value("start_date", "2015-01-01");
    std::string endDate   = bt.value("end_date", "2025-12-31");
    double      capital   = bt.value("initial_capital", 10000.0);
    std::string benchmark = bt.value("benchmark", "SPY");

    std::vector<std::string> frequencies;
    for (const auto& f : bt["rebalance_frequencies"]) {
        frequencies.push_back(f.get<std::string>());
    }

    const auto& tickers = config["asset_tickers"];

    // Compute warm-up start date (3 months earlier) for rate-of-change calculations
    // so the output can start exactly from startDate.
    auto computeWarmupDate = [](const std::string& date, int monthsEarlier) -> std::string {
        int y = std::stoi(date.substr(0, 4));
        int m = std::stoi(date.substr(5, 2));
        m -= monthsEarlier;
        while (m <= 0) {
            m += 12;
            y--;
        }
        char buf[11];
        snprintf(buf, sizeof(buf), "%04d-%02d-01", y, m);
        return std::string(buf);
    };
    std::string warmupDate = computeWarmupDate(startDate, 3);

    yFinance::init();
    util::Defer _cleanup([] { yFinance::close(); });

    /* ---- Fetch FRED data ---- */
    // clang-format off
    const std::vector<std::string> fredIds = {
        "UNRATE", "PAYEMS", "INDPRO",
        "CPIAUCSL", "CPILFESL", "PCEPI",
        "M2REAL", "WM2NS", "FEDFUNDS",
        "UMCSENT",
        "T10Y2Y", "BAMLH0A0HYM2"
    };
    // clang-format on

    std::cerr << "Fetching FRED data (" << warmupDate << " ~ " << endDate << ")..." << std::endl;

    std::map<std::string, std::shared_ptr<FredSeriesInfo>> fredData;
    for (const auto& id : fredIds) {
        auto result = yFinance::getFredSeries(id, apiKey, warmupDate, endDate, "m");
        if (result && !result->values.empty()) {
            fredData[id] = result;
            std::cerr << "  [OK] " << id << " (" << result->values.size() << " obs)" << std::endl;
        } else {
            std::cerr << "  [WARN] " << id << " - no data" << std::endl;
        }
    }

    /* ---- Fetch asset prices ---- */
    std::cerr << "Fetching asset prices..." << std::endl;

    // Collect unique tickers
    std::map<std::string, std::string> assetTickerMap;  // asset_key -> ticker
    for (const auto& [key, val] : tickers.items()) {
        assetTickerMap[key] = val.get<std::string>();
    }

    // Fetch monthly data for each unique ticker
    std::map<std::string, std::shared_ptr<StockInfo>> priceData;
    std::set<std::string>                             fetchedTickers;
    for (const auto& [key, ticker] : assetTickerMap) {
        if (fetchedTickers.count(ticker)) {
            continue;
        }
        auto stock = yFinance::getStockInfo(ticker, startDate, endDate, "1mo");
        if (stock && !stock->close.empty()) {
            priceData[ticker] = stock;
            std::cerr << "  [OK] " << ticker << " (" << stock->close.size() << " months)" << std::endl;
        } else {
            std::cerr << "  [WARN] " << ticker << " - no data" << std::endl;
        }
        fetchedTickers.insert(ticker);
    }

    // Always fetch benchmark even if not in asset_tickers
    if (!fetchedTickers.count(benchmark)) {
        auto stock = yFinance::getStockInfo(benchmark, startDate, endDate, "1mo");
        if (stock && !stock->close.empty()) {
            priceData[benchmark] = stock;
            std::cerr << "  [OK] " << benchmark << " (" << stock->close.size() << " months, benchmark)" << std::endl;
        } else {
            std::cerr << "  [WARN] " << benchmark << " - no data (benchmark)" << std::endl;
        }
    }

    /* ---- Build aligned monthly returns ---- */
    // Use benchmark (SPY) as the date reference
    if (priceData.find(benchmark) == priceData.end()) {
        std::cerr << "Error: Benchmark " << benchmark << " data not available." << std::endl;
        return 1;
    }

    auto   benchmarkStock  = priceData[benchmark];
    auto   dates           = buildDates(benchmarkStock);
    auto   benchmarkReturn = buildMonthlyReturns(benchmarkStock);
    size_t months          = dates.size();

    // Build monthly returns for each asset class, aligned to same length
    std::map<std::string, std::vector<double>> assetReturns;
    for (const auto& [key, ticker] : assetTickerMap) {
        if (priceData.find(ticker) != priceData.end()) {
            auto returns = buildMonthlyReturns(priceData[ticker]);
            // Align: use minimum length
            size_t len = std::min(returns.size(), months);
            if (returns.size() > months) {
                returns = std::vector<double>(returns.end() - months, returns.end());
            } else if (returns.size() < months) {
                // Pad front with zeros
                std::vector<double> padded(months - returns.size(), 0.0);
                padded.insert(padded.end(), returns.begin(), returns.end());
                returns = padded;
            }
            assetReturns[key] = returns;
        }
    }

    /* ---- Run backtests ---- */
    std::cerr << "Running backtests..." << std::endl;

    std::vector<MacroBacktestResult> results;
    for (const auto& freq : frequencies) {
        auto r = MacroBacktester::run(config, fredData, assetReturns, dates, freq, capital);
        results.push_back(r);
    }

    auto bench = MacroBacktester::computeBenchmark(benchmarkReturn, dates, benchmark, capital);

    /* ---- Print ---- */
    MacroBacktester::printResults(results, bench);

    return 0;
}
