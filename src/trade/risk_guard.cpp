#include "trade/risk_guard.hpp"

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
        // Yesterday's state is discarded rather than carried forward: the limits are
        // per-day, so a new day must start with a clean count and a fresh baseline.
        if (j.value("date", "") != date_) {
            return;
        }
        openingEquity_ = j.value("opening_equity", 0.0);
        ordersToday_   = j.value("orders", 0);
    } catch (const std::exception&) {
        // A corrupt state file must not be read as "no limits" — but it also must not
        // abort trading, so the day simply re-baselines from the next balance.
    }
}

void RiskGuard::save() const {
    nlohmann::json j;
    j["date"]           = date_;
    j["opening_equity"] = openingEquity_;
    j["orders"]         = ordersToday_;

    std::ofstream out(path_, std::ios::trunc);
    if (out.is_open()) {
        out << j.dump(2) << "\n";
    }
}

void RiskGuard::observe(const AccountBalance& balance) {
    // A day boundary crossed while the process was running still rolls over.
    if (const std::string today = kstToday(); today != date_) {
        date_          = today;
        openingEquity_ = 0.0;
        ordersToday_   = 0;
    }

    if (balance.success && openingEquity_ <= 0.0 && balance.totalEvalAmount > 0.0) {
        openingEquity_ = balance.totalEvalAmount;
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

    if (limits_.dailyLossLimitPct > 0.0 && openingEquity_ > 0.0 && balance.success) {
        const double lossPct = (openingEquity_ - balance.totalEvalAmount) / openingEquity_ * 100.0;
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
