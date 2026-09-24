#include "trade/session.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace trade {

std::string barDate(int64_t ts) {
    const std::time_t  t = static_cast<std::time_t>(ts);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&t), "%Y-%m-%d");
    return oss.str();
}

std::size_t evaluationIndex(const StockInfo& data, const std::string& todayKst) {
    if (data.close.empty() || data.timestamps.empty()) {
        return 0;
    }
    const std::size_t lastIdx = data.close.size() - 1;
    return (barDate(data.timestamps[lastIdx]) == todayKst) ? lastIdx : data.close.size();
}

std::string krxClosedReason(const data::KrxCalendar& calendar, const std::string& dateKst, int hhmm) {
    if (const std::string why = calendar.closedReason(dateKst); !why.empty()) {
        return why;
    }
    return (hhmm >= 900 && hhmm <= 1530) ? "" : "outside 09:00-15:30 KST";
}

double referencePrice(double liveQuote, double lastClose, bool intraday) {
    if (intraday) {
        return lastClose;
    }
    return liveQuote > 0.0 ? liveQuote : lastClose;
}

bool shouldSendSummary(bool ranDaily, bool quiet, const std::string& sentForDate, const std::string& todayKst) {
    return ranDaily && !quiet && sentForDate != todayKst;
}

}  // namespace trade
