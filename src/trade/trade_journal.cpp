#include "trade/trade_journal.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#include "common/util.hpp"

namespace trade {

namespace {

/** KST wall-clock "YYYY-MM-DD HH:MM:SS" (UTC+9 shift then gmtime — no TZ database needed). */
std::string kstTimestamp(std::time_t utc) {
    const std::time_t  kst = utc + 9 * 3600;
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&kst), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

}  // namespace

TradeJournal::TradeJournal(const std::string& path)
    : path_(path.empty() ? util::resolveFromExe("data/trades.jsonl") : path) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path(), ec);
}

bool TradeJournal::append(const JournalEntry& entry) const {
    const std::time_t now = std::time(nullptr);

    nlohmann::json j;
    j["ts"]          = static_cast<int64_t>(now);
    j["time"]        = kstTimestamp(now);
    j["event"]       = entry.event;
    j["mode"]        = entry.mode;
    j["dry_run"]     = entry.dryRun;
    j["strategy_id"] = entry.strategyId;
    j["strategy"]    = entry.strategy;
    j["category"]    = entry.category;
    j["ticker"]      = entry.ticker;
    j["side"]        = entry.side;
    j["qty"]         = entry.quantity;
    j["price"]       = entry.price;
    j["reason"]      = entry.reason;
    j["order_no"]    = entry.orderNo;
    j["success"]     = entry.success;
    j["message"]     = entry.message;

    std::ofstream out(path_, std::ios::app);
    if (!out.is_open()) {
        return false;
    }
    out << j.dump() << "\n";
    return out.good();
}

std::set<std::string> TradeJournal::recordedFillKeys() const {
    std::set<std::string> keys;

    std::ifstream in(path_);
    if (!in.is_open()) {
        return keys;  // no journal yet — nothing has been recorded
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        try {
            const auto j = nlohmann::json::parse(line);
            if (j.value("event", "") != "fill") {
                continue;
            }
            keys.insert(j.value("order_no", "") + ":" + std::to_string(j.value("qty", static_cast<int64_t>(0))));
        } catch (const std::exception&) {
            continue;  // a torn final line from a killed process must not hide the rest
        }
    }
    return keys;
}

}  // namespace trade
