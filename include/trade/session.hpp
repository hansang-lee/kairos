#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "data/krx_calendar.hpp"
#include "stock_info.hpp"

namespace trade {

/**
 * @file session.hpp
 *
 * The decisions a trading session makes before a strategy is even asked: which
 * bar to evaluate, whether the market is open, what price to size against, and
 * whether the day's summary is due. They lived inline in app/trader.cpp, inside
 * main(), where nothing could test them — and each one, wrong, moves money.
 * Here they are functions over plain values, and the trader is a caller.
 */

/**
 * @brief Calendar date of a daily bar, "YYYY-MM-DD".
 *
 * KisProvider stamps daily bars at 09:00 UTC on their own date, so the UTC date
 * of the stamp is the bar's date. Not the KST date: a bar stamped that way is
 * 18:00 KST the same day either way, but the convention is the provider's and is
 * written down here rather than assumed.
 */
[[nodiscard]] std::string barDate(int64_t ts);

/**
 * @brief The index a daily strategy is evaluated at, given today's KST date.
 *
 * evaluate(i) may read bars up to i-1 and fills at bar i. If the exchange has
 * already published today's bar it is the last one and the order fills there; if
 * not, the index is one past the end, so the strategy reads everything it has
 * and the order fills at a bar that does not exist yet — which is today's.
 */
[[nodiscard]] std::size_t evaluationIndex(const StockInfo& data, const std::string& todayKst);

/**
 * @brief Why KRX is closed at this KST moment, or empty if it is open.
 *
 * Weekends and listed holidays come from the calendar; the session window is
 * 09:00 to 15:30 inclusive, which is the continuous session plus the closing
 * auction the trader is scheduled to act in.
 */
[[nodiscard]] std::string krxClosedReason(const data::KrxCalendar& calendar, const std::string& dateKst, int hhmm);

/**
 * @brief The price an order is sized and journaled against.
 *
 * A daily series ends at the last published close, which is yesterday's until the
 * exchange posts today's bar, so a daily profile prefers a live quote and falls
 * back to the close only when the quote is missing. An intraday series' last bar
 * is already now, and a quote would say the same thing.
 */
[[nodiscard]] double referencePrice(double liveQuote, double lastClose, bool intraday);

/**
 * @brief Whether the once-a-day summary should go out now.
 *
 * After the daily pass, once per date, unless silenced. Silence must not mean
 * both "held correctly" and "never ran", so the summary goes even when nothing
 * happened; it just must not go twice.
 */
[[nodiscard]] bool shouldSendSummary(bool ranDaily, bool quiet, const std::string& sentForDate,
                                     const std::string& todayKst);

}  // namespace trade
