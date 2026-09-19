#include "data/krx_calendar.hpp"

#include <ctime>

#include "common/util.hpp"

namespace data {

namespace {

/** Day of week for a "YYYY-MM-DD" date, 0 = Sunday. -1 if unparseable. */
int weekdayOf(const std::string& date) {
    if (date.size() != 10) {
        return -1;
    }
    std::tm tm = {};
    try {
        tm.tm_year = std::stoi(date.substr(0, 4)) - 1900;
        tm.tm_mon  = std::stoi(date.substr(5, 2)) - 1;
        tm.tm_mday = std::stoi(date.substr(8, 2));
    } catch (const std::exception&) {
        return -1;
    }
    tm.tm_hour          = 12;  // midday, so no DST/rounding edge can shift the date
    const std::time_t t = timegm(&tm);
    return std::gmtime(&t)->tm_wday;
}

}  // namespace

KrxCalendar::KrxCalendar(const std::string& configPath) {
    const std::string path = configPath.empty() ? util::resolveFromExe("config/krx_holidays.json") : configPath;

    const auto j = util::loadJsonConfig(path);
    if (!j || !j->contains("holidays") || !(*j)["holidays"].is_object()) {
        return;  // weekend rule only — see the header for why this fails open
    }
    for (const auto& [date, name] : (*j)["holidays"].items()) {
        holidays_[date] = name.is_string() ? name.get<std::string>() : "holiday";
    }
    loaded_ = true;
}

std::string KrxCalendar::closedReason(const std::string& date) const {
    const int wday = weekdayOf(date);
    if (wday == 0 || wday == 6) {
        return "weekend";
    }
    if (const auto it = holidays_.find(date); it != holidays_.end()) {
        return it->second;
    }
    return "";
}

bool KrxCalendar::isTradingDay(const std::string& date) const {
    return closedReason(date).empty();
}

}  // namespace data
