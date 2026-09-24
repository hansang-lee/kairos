#pragma once

#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace util {

/**
 * @file kst_time.hpp
 *
 * Every clock read in the trading path is KST, and every one of them was its own
 * copy of the same trick: shift the UTC epoch forward nine hours and read the
 * fields with gmtime, so no timezone database is consulted and a host set to UTC
 * gives the same answer as one set to Seoul. Eight copies agreed today; one that
 * drifted would have been found by a mistimed order.
 */

/** @brief KST wall clock as {weekday (0 = Sunday), HHMM}. */
[[nodiscard]] inline std::pair<int, int> kstNow() {
    const std::time_t kst   = std::time(nullptr) + 9 * 3600;
    const std::tm*    tmPtr = std::gmtime(&kst);
    return {tmPtr->tm_wday, tmPtr->tm_hour * 100 + tmPtr->tm_min};
}

/** @brief KST calendar date of a UTC epoch, "YYYY-MM-DD". */
[[nodiscard]] inline std::string kstDateOf(int64_t utc) {
    const std::time_t  kst = static_cast<std::time_t>(utc) + 9 * 3600;
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&kst), "%Y-%m-%d");
    return oss.str();
}

/** @brief KST calendar date `daysAgo` days back, "YYYY-MM-DD". */
[[nodiscard]] inline std::string kstDate(int daysAgo = 0) {
    return kstDateOf(static_cast<int64_t>(std::time(nullptr)) - static_cast<int64_t>(daysAgo) * 86400);
}

/** @brief Today's KST date, "YYYY-MM-DD". */
[[nodiscard]] inline std::string kstToday() {
    return kstDate(0);
}

/** @brief KST wall-clock timestamp of a UTC epoch, "YYYY-MM-DD HH:MM:SS". */
[[nodiscard]] inline std::string kstTimestamp(std::time_t utc) {
    const std::time_t  kst = utc + 9 * 3600;
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&kst), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

}  // namespace util
