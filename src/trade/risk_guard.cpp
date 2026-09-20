#include "trade/risk_guard.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#include "common/util.hpp"

namespace trade {

namespace {

/** KST calendar date, "YYYY-MM-DD" (UTC+9 shift then gmtime — no TZ database needed). */
std::string kstToday() {
    const std::time_t  kst = std::time(nullptr) + 9 * 3600;
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&kst), "%Y-%m-%d");
    return oss.str();
}

}  // namespace

RiskGuard::RiskGuard(const RiskLimits& limits, const std::string& path)
    : limits_(limits)
    , path_(path.empty() ? util::resolveFromExe("data/risk_state.json") : path) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path(), ec);
    load();
}

void RiskGuard::load() {
    date_ = kstToday();

    std::ifstream in(path_);
    if (!in.is_open()) {
        return;  // no state yet — today starts fresh
    }
    try {
        nlohmann::json j;
        in >> j;
        // The counts and the intraday baseline are per-day and must not carry over;
        // the last equity must, or a once-a-day process has nothing to measure against.
        // Whatever the stored day was, its last equity becomes tomorrow's reference.
        previousEquity_ = j.value("last_equity", 0.0);
        lastEquity_     = previousEquity_;
        if (j.value("date", "") != date_) {
            return;  // a prior day: keep only the carried-over equity
        }
        openingEquity_  = j.value("opening_equity", 0.0);
        ordersToday_    = j.value("orders", 0);
        previousEquity_ = j.value("previous_equity", 0.0);
    } catch (const std::exception&) {
        // A corrupt state file must not be read as "no limits" — but it also must not
        // abort trading, so the day simply re-baselines from the next balance.
    }
}

void RiskGuard::save() const {
    nlohmann::json j;
    j["date"]            = date_;
    j["opening_equity"]  = openingEquity_;
    j["previous_equity"] = previousEquity_;
    j["last_equity"]     = lastEquity_;
    j["orders"]          = ordersToday_;

    std::ofstream out(path_, std::ios::trunc);
    if (out.is_open()) {
        out << j.dump(2) << "\n";
    }
}

double RiskGuard::referenceEquity() const {
    return std::max(openingEquity_, previousEquity_);
}

void RiskGuard::observe(const AccountBalance& balance) {
    bool dirty = false;

    // A day boundary crossed while the process was running still rolls over, and
    // today's last equity becomes tomorrow's reference.
    if (const std::string today = kstToday(); today != date_) {
        date_           = today;
        previousEquity_ = lastEquity_;
        openingEquity_  = 0.0;
        ordersToday_    = 0;
        dirty           = true;
    }

    if (balance.success && balance.totalEvalAmount > 0.0) {
        if (openingEquity_ <= 0.0) {
            openingEquity_ = balance.totalEvalAmount;
            dirty          = true;
        }
        if (lastEquity_ != balance.totalEvalAmount) {
            lastEquity_ = balance.totalEvalAmount;
            dirty       = true;
        }
    }

    if (dirty) {
        save();
    }
}

RiskVerdict RiskGuard::check(OrderSide side, const AccountBalance& balance) {
    observe(balance);

    // Closing a position is always permitted — see the class comment.
    if (side == OrderSide::Sell) {
        return {};
    }

    if (limits_.maxOrdersPerDay > 0 && ordersToday_ >= limits_.maxOrdersPerDay) {
        std::ostringstream oss;
        oss << "daily order cap reached (" << ordersToday_ << "/" << limits_.maxOrdersPerDay << ")";
        return {false, oss.str()};
    }

    const double reference = referenceEquity();
    if (limits_.dailyLossLimitPct > 0.0 && reference > 0.0 && balance.success) {
        const double lossPct = (reference - balance.totalEvalAmount) / reference * 100.0;
        if (lossPct >= limits_.dailyLossLimitPct) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << "daily loss limit hit (-" << lossPct << "% vs limit -"
                << limits_.dailyLossLimitPct << "%)";
            return {false, oss.str()};
        }
    }

    return {};
}

void RiskGuard::recordOrder() {
    ++ordersToday_;
    save();
}

}  // namespace trade
