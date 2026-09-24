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
        const int year  = std::stoi(date.substr(0, 4));
        const int month = std::stoi(date.substr(5, 2));
        const int day   = std::stoi(date.substr(8, 2));
        // timegm would happily normalise month 13 into next January, turning a
        // malformed date into a real but wrong one.
        if (month < 1 || month > 12 || day < 1 || day > 31) {
            return -1;
        }
        tm.tm_year = year - 1900;
        tm.tm_mon  = month - 1;
        tm.tm_mday = day;
    } catch (const std::exception&) {
        return -1;
    }
    tm.tm_hour          = 12;  // midday, so no DST/rounding edge can shift the date
    const std::time_t t = timegm(&tm);
    return std::gmtime(&t)->tm_wday;
}

/** The calendar day after a "YYYY-MM-DD", or empty if the input is malformed. */
std::string nextDay(const std::string& date) {
    if (date.size() != 10) {
        return "";
    }
    std::tm tm = {};
    try {
        tm.tm_year = std::stoi(date.substr(0, 4)) - 1900;
        tm.tm_mon  = std::stoi(date.substr(5, 2)) - 1;
        tm.tm_mday = std::stoi(date.substr(8, 2));
    } catch (const std::exception&) {
        return "";
    }
    tm.tm_hour          = 12;
    const std::time_t t = timegm(&tm) + 86400;
    char              buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::gmtime(&t));
    return buf;
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

std::vector<KrxCalendar::Disagreement> KrxCalendar::audit(const std::set<std::string>& observedBarDates,
                                                          const std::string& from, const std::string& to) const {
    std::vector<Disagreement> out;
    // A malformed endpoint is refused outright. "2026-13-01" sorts before a real
    // September date, and timegm would happily normalise it into next January, so
    // the loop would report junk days that never existed.
    if (weekdayOf(from) < 0 || weekdayOf(to) < 0) {
        return out;
    }
    // Bounded so a runaway range cannot spin: two years of days is more than any
    // caller has bars for.
    for (std::string d = from; !d.empty() && d <= to && out.size() < 800; d = nextDay(d)) {
        const std::string reason = closedReason(d);
        const bool        hasBar = observedBarDates.count(d) > 0;
        if (!reason.empty() && hasBar) {
            out.push_back({d, "closed (" + reason + ")", "a bar was published"});
        } else if (reason.empty() && !hasBar) {
            out.push_back({d, "open", "no bar"});
        }
    }
    return out;
}

}  // namespace data
