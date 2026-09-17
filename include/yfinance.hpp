#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "fng_info.hpp"
#include "fred_info.hpp"
#include "stock_info.hpp"

class yFinance {
   public:
    static void init();
    static void close();

    yFinance()  = delete;
    ~yFinance() = delete;

    yFinance(const yFinance& other) = delete;
    yFinance(yFinance&& other)      = delete;

    yFinance& operator=(const yFinance& other) = delete;
    yFinance& operator=(yFinance&& other)      = delete;

    /**
     * @brief Fetch historical stock data.
     * @param ticker Stock ticker (e.g., "AAPL")
     * @param interval Data interval (e.g., "1d", "1wk", "1mo")
     * @param range Data range (e.g., "1y", "5y", "max")
     * @return StockInfo containing historical time-series
     */
    [[nodiscard]] static std::shared_ptr<StockInfo>
    getStockInfo(const std::string& ticker, const std::string& interval = "1d", const std::string& range = "1mo");

    /**
     * @brief Fetch historical stock data with date range.
     * @param ticker Stock ticker
     * @param startDate Start date (YYYY-MM-DD)
     * @param endDate End date (YYYY-MM-DD)
     * @param interval Data interval
     * @return StockInfo
     */
    [[nodiscard]] static std::shared_ptr<StockInfo> getStockInfo(const std::string& ticker,
                                                                 const std::string& startDate,
                                                                 const std::string& endDate,
                                                                 const std::string& interval);

    /**
     * @brief Fetch FRED economic data series (e.g., UNRATE, FEDFUNDS).
     * @param seriesId FRED series ID (e.g., "UNRATE", "FEDFUNDS")
     * @param apiKey FRED API key
     * @param observationStart Optional start date (YYYY-MM-DD)
     * @param observationEnd Optional end date (YYYY-MM-DD)
     * @param frequency Optional frequency: "d"(daily), "w"(weekly), "m"(monthly), "q"(quarterly), "a"(annual).
     *        If higher than the series' native frequency, FRED returns data at the native frequency.
     * @return FredSeriesInfo containing date-value time-series
     */
    [[nodiscard]] static std::shared_ptr<FredSeriesInfo>
    getFredSeries(const std::string& seriesId, const std::string& apiKey, const std::string& observationStart = "",
                  const std::string& observationEnd = "", const std::string& frequency = "");

    /**
     * @brief Fetch CNN Fear and Greed Index.
     * @return FearAndGreedInfo containing current and historical sentiment index
     */
    [[nodiscard]] static std::shared_ptr<FearAndGreedInfo> getFearAndGreedIndex();

   private:
    static constexpr std::string_view url_base_      = "https://query1.finance.yahoo.com/v8/finance/chart/";
    static constexpr std::string_view cnn_url_base_  = "https://production.dataviz.cnn.io/index/fearandgreed/graphdata";
    static constexpr std::string_view fred_url_base_ = "https://api.stlouisfed.org/fred/series/observations";

    [[nodiscard]] static std::string fetch(const std::string& url, bool is_cnn = false);

    static std::size_t write(void* contents, std::size_t size, std::size_t nmemb, void* userp);
};
