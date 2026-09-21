#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "stock_info.hpp"

namespace portfolio {

/**
 * @brief Several assets on one timeline.
 *
 * Assets do not share a trading calendar — a Korean ETF, a US-tracking one and a
 * commodity fund all close on different days — so comparing them requires a
 * single timeline with each asset's last known price carried forward. Taking the
 * intersection instead would discard most of the history.
 *
 * `available` marks bars before an asset's first price. A strategy must not
 * weight an asset there: the carried-forward value would be its opening price
 * repeated backwards, which is a number nobody could have traded on.
 */
struct PortfolioData {
    std::vector<std::string>         tickers;
    std::vector<int64_t>             timestamps;
    std::vector<std::vector<double>> close;      ///< close[asset][bar]
    std::vector<std::vector<bool>>   available;  ///< available[asset][bar]

    [[nodiscard]] std::size_t assetCount() const { return tickers.size(); }
    [[nodiscard]] std::size_t barCount() const { return timestamps.size(); }

    /**
     * @brief Build from per-asset series, aligning on the union of their timestamps.
     * @param series One entry per asset; entries with no bars are dropped.
     */
    static PortfolioData align(const std::vector<std::shared_ptr<StockInfo>>& series);
};

}  // namespace portfolio
