#include <fstream>
#include <string>

#include "data/krx_calendar.hpp"
#include "test_framework.hpp"
#include "trade/session.hpp"

/*
 * The decisions a session makes before any strategy is asked. Until these were
 * functions they lived inside main() in app/trader.cpp, where nothing tested
 * them — and each one, wrong, moves money on the wrong day, at the wrong bar, or
 * at the wrong price.
 */
namespace {

StockInfo bars(std::size_t n, int64_t firstTs) {
    StockInfo s;
    for (std::size_t i = 0; i < n; ++i) {
        s.timestamps.push_back(firstTs + static_cast<int64_t>(i) * 86400);
        s.close.push_back(100.0 + static_cast<double>(i));
    }
    return s;
}

// 2026-09-22 09:00:00 UTC, the way KIS stamps that bar. Checked with `date -u`,
// after the first draft was a week off and the tests caught it.
constexpr int64_t kSep22_0900Utc = 1790067600;

std::string tmpCalendar(const std::string& name, const std::string& json) {
    const std::string path = "/tmp/kairos_session_" + name + ".json";
    std::ofstream     out(path, std::ios::trunc);
    out << json;
    return path;
}

}  // namespace

TEST(session, a_bar_is_dated_by_the_utc_day_it_is_stamped_on) {
    CHECK_EQ(trade::barDate(kSep22_0900Utc), std::string("2026-09-22"));
    // Midnight UTC of the next day is still "that" bar's date in UTC terms.
    CHECK_EQ(trade::barDate(kSep22_0900Utc + 15 * 3600), std::string("2026-09-23"));
}

TEST(session, when_todays_bar_is_published_the_strategy_fills_on_it) {
    // Three bars ending on the 22nd, and today is the 22nd: evaluate at the last
    // index, so the strategy reads through the 21st and the order fills today.
    const auto s = bars(3, kSep22_0900Utc - 2 * 86400);
    CHECK_EQ(trade::evaluationIndex(s, "2026-09-22"), std::size_t{2});
}

TEST(session, when_todays_bar_is_missing_the_index_is_one_past_the_end) {
    // Same series, but today is the 23rd and no bar for it has arrived: the
    // strategy reads everything it has and fills at a bar that does not exist
    // yet — today's.
    const auto s = bars(3, kSep22_0900Utc - 2 * 86400);
    CHECK_EQ(trade::evaluationIndex(s, "2026-09-23"), std::size_t{3});
}

TEST(session, an_empty_series_evaluates_at_zero_rather_than_underflowing) {
    CHECK_EQ(trade::evaluationIndex(StockInfo{}, "2026-09-22"), std::size_t{0});
}

TEST(session, the_market_is_open_only_on_a_trading_day_inside_the_session_window) {
    const data::KrxCalendar cal(tmpCalendar("open", R"({"holidays":{"2026-09-24":"Chuseok"}})"));

    CHECK_EQ(trade::krxClosedReason(cal, "2026-09-22", 1515), std::string(""));  // Tue, in session
    CHECK_EQ(trade::krxClosedReason(cal, "2026-09-22", 900), std::string(""));   // the open itself
    CHECK_EQ(trade::krxClosedReason(cal, "2026-09-22", 1530), std::string(""));  // the close itself
    CHECK_EQ(trade::krxClosedReason(cal, "2026-09-22", 859), std::string("outside 09:00-15:30 KST"));
    CHECK_EQ(trade::krxClosedReason(cal, "2026-09-22", 1531), std::string("outside 09:00-15:30 KST"));
    CHECK_EQ(trade::krxClosedReason(cal, "2026-09-26", 1000), std::string("weekend"));
    CHECK_EQ(trade::krxClosedReason(cal, "2026-09-24", 1000), std::string("Chuseok"));
}

TEST(session, a_holiday_outranks_the_time_of_day) {
    // Inside the window but a holiday: the reason must be the holiday, so the
    // log says why rather than implying a wrong clock.
    const data::KrxCalendar cal(tmpCalendar("rank", R"({"holidays":{"2026-09-24":"Chuseok"}})"));
    CHECK_EQ(trade::krxClosedReason(cal, "2026-09-24", 1200), std::string("Chuseok"));
}

TEST(session, a_daily_profile_prefers_a_live_quote_and_falls_back_to_the_close) {
    CHECK_NEAR(trade::referencePrice(113145.0, 111880.0, false), 113145.0, 1e-9);
    // No quote: the close, and never zero — an order sized against zero is infinite.
    CHECK_NEAR(trade::referencePrice(0.0, 111880.0, false), 111880.0, 1e-9);
    CHECK_NEAR(trade::referencePrice(-1.0, 111880.0, false), 111880.0, 1e-9);
}

TEST(session, an_intraday_profile_uses_its_last_bar_whatever_the_quote_says) {
    // The last minute bar is already now; a quote would only disagree by noise.
    CHECK_NEAR(trade::referencePrice(113145.0, 113100.0, true), 113100.0, 1e-9);
}

TEST(session, the_summary_goes_once_per_day_after_the_daily_pass_unless_silenced) {
    CHECK(trade::shouldSendSummary(true, false, "", "2026-09-22"));
    CHECK(trade::shouldSendSummary(true, false, "2026-09-21", "2026-09-22"));
    CHECK(!trade::shouldSendSummary(true, false, "2026-09-22", "2026-09-22"));  // already sent today
    CHECK(!trade::shouldSendSummary(false, false, "", "2026-09-22"));           // daily pass did not run
    CHECK(!trade::shouldSendSummary(true, true, "", "2026-09-22"));             // --quiet
}
