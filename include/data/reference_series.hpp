#pragma once

#include <map>
#include <string>
#include <vector>

namespace data {

/**
 * @brief A second asset's prices, for strategies that compare one thing to another.
 *
 * IStrategy sees the OHLCV of a single ticker, which is enough for every signal
 * built from that ticker's own history and not enough for anything relative —
 * "hold equities only while they are beating bonds" cannot be expressed in it.
 *
 * Rather than widen the interface for every strategy, a strategy that needs a
 * comparison loads it here during init(). Prices come from the same
 * cache/daily/<ticker>.csv the sweep already fills, so a backtest costs no extra
 * requests; a missing file means the strategy stands down rather than guesses.
 */
class ReferenceSeries {
   public:
    /**
     * @param ticker  Reference asset.
     * @param cacheDir Directory of cached daily bars. Empty resolves to <root>/cache/daily.
     */
    explicit ReferenceSeries(const std::string& ticker, const std::string& cacheDir = "");

    /** @brief True when prices were found and the series is usable. */
    [[nodiscard]] bool loaded() const { return !byTimestamp_.empty(); }

    /**
     * @brief Close at or immediately before a timestamp.
     *
     * "At or before" matters: the reference is a different instrument with its own
     * holidays, and taking the next available price instead would read a value from
     * after the bar being decided.
     *
     * @return The price, or 0 when nothing at or before that time exists.
     */
    [[nodiscard]] double closeAtOrBefore(int64_t ts) const;

   private:
    std::map<int64_t, double> byTimestamp_;
};

}  // namespace data
