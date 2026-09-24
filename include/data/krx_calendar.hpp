#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace data {

/**
 * @brief Whether KRX trades on a given day.
 *
 * KIS's holiday API (CTCA0903R) is a ledger service rejected on paper accounts
 * ("모의투자 TR 이 아닙니다"), so the closures come from config/krx_holidays.json
 * instead. That file is maintained by hand; see its own notes for what has been
 * verified against real market data and what is still projected.
 *
 * Unknown dates are treated as trading days. Getting that wrong costs one
 * pointless poll — KIS just returns no bars — while wrongly declaring a trading
 * day closed would stop trading with no visible error.
 */
class KrxCalendar {
   public:
    /**
     * @param configPath Holiday file. Empty (default) resolves to
     *        <project-root>/config/krx_holidays.json; a missing file leaves only
     *        the weekend rule in effect.
     */
    explicit KrxCalendar(const std::string& configPath = "");

    /** @param date "YYYY-MM-DD" (KST). */
    [[nodiscard]] bool isTradingDay(const std::string& date) const;

    /** @brief Reason the day is closed ("weekend", or the holiday name); empty if it trades. */
    [[nodiscard]] std::string closedReason(const std::string& date) const;

    /** @brief Whether any holiday list was actually loaded. */
    [[nodiscard]] bool loaded() const { return loaded_; }

    /** @brief One day where this calendar and the exchange disagreed. */
    struct Disagreement {
        std::string date;
        std::string expected;  ///< "closed (<reason>)" or "open"
        std::string observed;  ///< "a bar was published" or "no bar"
    };

    /**
     * @brief Check the calendar's claims against days the exchange actually traded.
     *
     * Every closure in the list is a projection until something confirms it, and
     * both mistakes are silent: a trading day marked closed skips that day's
     * signals with no error, and a holiday marked open sends orders against
     * yesterday's close. The only ground truth available on a paper account is
     * whether KIS published a bar for the date, so that is what this compares to.
     *
     * @param observedBarDates Every "YYYY-MM-DD" with a daily bar, across any
     *        ticker the caller has fetched. One bar on a date is enough to prove
     *        the exchange was open.
     * @param from Inclusive start of the range to judge.
     * @param to   Inclusive end. Pass yesterday, not today: today's bar may simply
     *        not be published yet, which is not a disagreement.
     */
    [[nodiscard]] std::vector<Disagreement> audit(const std::set<std::string>& observedBarDates,
                                                  const std::string& from, const std::string& to) const;

   private:
    bool                                            loaded_ = false;
    std::map<std::string, std::string, std::less<>> holidays_;
};

}  // namespace data
