#pragma once

#include <map>
#include <string>

namespace trade {

/**
 * @brief Remembers which profiles have already acted today.
 *
 * A once-a-day strategy running inside a continuous loop needs to know it has
 * already run, and needs to still know it after a restart — otherwise every
 * restart would re-evaluate and could re-enter a position it just exited.
 *
 * Dates are KST calendar days, matching every other daily boundary here.
 */
class ScheduleState {
   public:
    /**
     * @param path State file. Empty (default) resolves to <project-root>/data/schedule.json.
     */
    explicit ScheduleState(const std::string& path = "");

    /** @brief KST date this profile last evaluated on, empty if never. */
    [[nodiscard]] std::string lastEvaluated(int profileId) const;

    /** @brief Record that a profile evaluated on the given KST date. */
    void markEvaluated(int profileId, const std::string& date);

    [[nodiscard]] const std::string& path() const { return path_; }

   private:
    void load();
    void save() const;

    std::string                path_;
    std::map<int, std::string> lastEvaluated_;
};

/**
 * @brief Whether a once-a-day profile is due now.
 *
 * Kept free of I/O so the decision can be tested directly rather than by
 * waiting for a clock.
 *
 * @param lastDate  KST date the profile last evaluated on, empty if never.
 * @param todayDate Today's KST date.
 * @param nowHhmm   Current KST time as HHMM.
 * @param dueHhmm   Earliest time of day it may run, as HHMM.
 */
[[nodiscard]] bool isDailyProfileDue(const std::string& lastDate, const std::string& todayDate, int nowHhmm,
                                     int dueHhmm);

}  // namespace trade
