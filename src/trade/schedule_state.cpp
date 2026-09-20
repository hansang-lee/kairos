#include "trade/schedule_state.hpp"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "common/util.hpp"

namespace trade {

ScheduleState::ScheduleState(const std::string& path)
    : path_(path.empty() ? util::resolveFromExe("data/schedule.json") : path) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path(), ec);
    load();
}

void ScheduleState::load() {
    std::ifstream in(path_);
    if (!in.is_open()) {
        return;
    }
    try {
        nlohmann::json j;
        in >> j;
        for (const auto& [key, value] : j.items()) {
            lastEvaluated_[std::stoi(key)] = value.get<std::string>();
        }
    } catch (const std::exception&) {
        // A corrupt file costs one duplicate evaluation, which the position check
        // then makes harmless. Refusing to start over it would be worse.
        lastEvaluated_.clear();
    }
}

void ScheduleState::save() const {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [id, date] : lastEvaluated_) {
        j[std::to_string(id)] = date;
    }
    std::ofstream out(path_, std::ios::trunc);
    if (out.is_open()) {
        out << j.dump(2) << "\n";
    }
}

std::string ScheduleState::lastEvaluated(int profileId) const {
    const auto it = lastEvaluated_.find(profileId);
    return it == lastEvaluated_.end() ? std::string() : it->second;
}

void ScheduleState::markEvaluated(int profileId, const std::string& date) {
    if (lastEvaluated_[profileId] == date) {
        return;
    }
    lastEvaluated_[profileId] = date;
    save();
}

bool isDailyProfileDue(const std::string& lastDate, const std::string& todayDate, int nowHhmm, int dueHhmm) {
    if (lastDate == todayDate) {
        return false;  // already ran today
    }
    // Late is fine — a machine that was asleep or a process that restarted at 15:40
    // should still act, which a wall-clock timer firing once would not.
    return nowHhmm >= dueHhmm;
}

}  // namespace trade
