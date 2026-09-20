#pragma once

#include <memory>
#include <string>
#include <vector>

#include "stock_info.hpp"

namespace data {

/**
 * @brief Append-only store of intraday bars, built up day by day.
 *
 * Minute-bar history cannot be bought back: KIS serves only today's, and Yahoo
 * keeps roughly five days of 1-minute data. So the only way to ever backtest a
 * scalping strategy is to start saving the bars the live loop already fetches.
 * A month of running produces a month of real data; nothing else will.
 *
 * One CSV per ticker per KST day, under data/bars/<ticker>/<YYYY-MM-DD>.csv.
 * Recording is idempotent — polls overlap heavily, so bars are merged on
 * timestamp rather than appended blindly.
 */
class BarRecorder {
   public:
    /**
     * @param rootDir Directory holding the per-ticker folders. Empty (default)
     *                resolves to <project-root>/data/bars.
     */
    explicit BarRecorder(const std::string& rootDir = "");

    /**
     * @brief Merge bars into their day's file for one interval.
     *
     * The interval is part of the path, and must be: a 5-minute series shares
     * timestamps with every fifth bar of a 1-minute series, so storing both in one
     * file silently replaces those bars with 5-minute aggregates and leaves a
     * mixed-resolution series that looks valid.
     *
     * @param interval Bar size, e.g. "1m", "5m", "1h". Becomes a directory.
     * @return Number of bars that were not already stored, or -1 on write failure.
     */
    int record(const std::string& ticker, const StockInfo& bars, const std::string& interval = "1m");

    /**
     * @brief Merge bars into a single named file instead of per-day files.
     *
     * Daily bars are one per day, so the per-date layout would produce one file
     * holding one row. A named series keeps them together:
     * data/bars/<ticker>/<series>.csv, merged on timestamp like everything else.
     *
     * @return Number of bars that were not already stored, or -1 on write failure.
     */
    int recordSeries(const std::string& ticker, const std::string& series, const StockInfo& bars);

    /** @brief Load a named series written by recordSeries(); nullptr when absent. */
    [[nodiscard]] std::shared_ptr<StockInfo> loadSeries(const std::string& ticker, const std::string& series) const;

    /**
     * @brief Load stored bars for a ticker across a date range, oldest first.
     * @param startDate Inclusive, "YYYY-MM-DD".
     * @param endDate   Inclusive, "YYYY-MM-DD".
     * @return Concatenated series, or nullptr when nothing is stored for the range.
     */
    [[nodiscard]] std::shared_ptr<StockInfo> load(const std::string& ticker, const std::string& startDate,
                                                  const std::string& endDate, const std::string& interval = "1m") const;

    /** @brief Dates that have per-day stored bars for a ticker, oldest first.
     *
     * Named series are excluded — only files whose name is a date are returned.
     */
    [[nodiscard]] std::vector<std::string> storedDates(const std::string& ticker,
                                                       const std::string& interval = "1m") const;

    [[nodiscard]] const std::string& root() const { return root_; }

   private:
    [[nodiscard]] std::string pathFor(const std::string& ticker, const std::string& date) const;
    [[nodiscard]] std::string pathFor(const std::string& ticker, const std::string& interval,
                                      const std::string& date) const;

    std::string root_;
};

}  // namespace data
