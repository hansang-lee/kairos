#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "portfolio/portfolio_data.hpp"
#include "stock_info.hpp"

namespace portfolio {

/**
 * @brief Loading a universe's prices out of cache/daily/, shared by the tools.
 *
 * Extracted from portfolio_sweep because a second tool needed the same four
 * steps, and two copies of a CSV parser drift: the moment one of them starts
 * skipping a malformed row that the other keeps, the two tools disagree about
 * history and nobody notices until their numbers differ.
 */

/** @brief One ticker's cached daily bars, or nullptr when the file is missing or empty. */
[[nodiscard]] std::shared_ptr<StockInfo> loadCachedDaily(const std::string& ticker);

/**
 * @brief Write a ticker's bars to cache/daily/, replacing what is there.
 *
 * Refuses a series shorter than `minBars`, because a fetch cut short by a rate
 * limit returns a handful of bars and caching that poisons every later run —
 * which is how a 2,700-bar series once became 101 and silently dropped two
 * tickers from a sweep.
 *
 * @return whether anything was written.
 */
bool saveCachedDaily(const StockInfo& data, std::size_t minBars = 300);

/** @brief Seconds since epoch for a YYYY-MM-DD date, at the KRX open. */
[[nodiscard]] int64_t parseDate(const std::string& date);

/** @brief The bars of `series` inside [from, to), as a new series. */
[[nodiscard]] std::shared_ptr<StockInfo> sliceTo(const std::shared_ptr<StockInfo>& series, int64_t from, int64_t to);

struct UniverseLoad {
    std::vector<std::shared_ptr<StockInfo>> series;
    std::vector<std::string>                missing;  ///< tickers with no usable cache
    std::vector<double>                     expenseRatios;

    /**
     * @brief Each asset's `asset_class`, aligned with `series`.
     *
     * What an allocation rule should group by. A universe that does not tag its
     * assets leaves these empty rather than guessing, because a rule grouping on
     * a guess would report a diversification it does not have.
     */
    std::vector<std::string> assetClasses;

    /**
     * @brief Whether every asset kept carries a published fee.
     *
     * A run where some fees are guessed and some are measured is not a costed
     * run, and the difference has to reach the report rather than being averaged
     * away silently.
     */
    bool allFeesKnown = true;

    /** @brief The universe's `equity_classes`, naming which classes are the equity sleeve. */
    std::vector<std::string> equityClasses;
};

/**
 * @brief Load every ticker of a universe JSON over a date window.
 *
 * `expenseRatios` comes back aligned with `series`, reading each ticker's
 * `expense_ratio` field and falling back to `defaultExpenseRatio` where the
 * config does not state one.
 *
 * @param minBars Shortest history worth keeping; shorter series are dropped
 *                rather than aligned, because a ticker with a handful of bars
 *                contributes nothing but a hole in the timeline.
 */
[[nodiscard]] UniverseLoad loadUniverse(const std::string& universePath, const std::string& startDate,
                                        const std::string& endDate, double defaultExpenseRatio,
                                        std::size_t minBars = 300);

}  // namespace portfolio
