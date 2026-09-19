#pragma once

#include <map>
#include <string>

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

   private:
    bool                                            loaded_ = false;
    std::map<std::string, std::string, std::less<>> holidays_;
};

}  // namespace data
