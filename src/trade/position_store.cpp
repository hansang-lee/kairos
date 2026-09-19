#include "trade/position_store.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "common/util.hpp"

namespace trade {

PositionStore::PositionStore(const std::string& path)
    : path_(path.empty() ? util::resolveFromExe("data/positions.json") : path) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path(), ec);
    load();
}

void PositionStore::load() {
    std::ifstream in(path_);
    if (!in.is_open()) {
        return;
    }
    try {
        nlohmann::json j;
        in >> j;
        for (const auto& [ticker, v] : j.items()) {
            PositionState s;
            s.entryTs       = v.value("entry_ts", static_cast<int64_t>(0));
            s.peakPrice     = v.value("peak_price", 0.0);
            s.entryTranches = v.value("entry_tranches", 0);
            s.exitTranches  = v.value("exit_tranches", 0);
            s.lastExitTs    = v.value("last_exit_ts", static_cast<int64_t>(0));
            states_[ticker] = s;
        }
    } catch (const std::exception&) {
        // A corrupt file costs us the peaks and cooldowns, which re-derive from the
        // next cycle onward. Refusing to start over it would be worse.
        states_.clear();
    }
}

void PositionStore::save() const {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [ticker, s] : states_) {
        j[ticker] = {{"entry_ts", s.entryTs},
                     {"peak_price", s.peakPrice},
                     {"entry_tranches", s.entryTranches},
                     {"exit_tranches", s.exitTranches},
                     {"last_exit_ts", s.lastExitTs}};
    }
    std::ofstream out(path_, std::ios::trunc);
    if (out.is_open()) {
        out << j.dump(2) << "\n";
    }
}

const PositionState& PositionStore::sync(const std::string& ticker, int64_t heldQty, double price) {
    const int64_t now = static_cast<int64_t>(std::time(nullptr));

    PositionState& s       = states_[ticker];
    const bool     wasHeld = s.entryTs != 0;
    bool           dirty   = false;

    if (heldQty > 0) {
        if (!wasHeld) {
            // First sight of a position — including one opened outside this system.
            s.entryTs   = now;
            s.peakPrice = price;
            dirty       = true;
        } else if (price > s.peakPrice) {
            s.peakPrice = price;
            dirty       = true;
        }
    } else if (wasHeld) {
        // Closed, by us or by hand. Keep only what outlives the position.
        s.lastExitTs    = now;
        s.entryTs       = 0;
        s.peakPrice     = 0.0;
        s.entryTranches = 0;
        s.exitTranches  = 0;
        dirty           = true;
    }

    if (dirty) {
        save();
    }
    return s;
}

void PositionStore::recordEntryTranche(const std::string& ticker) {
    ++states_[ticker].entryTranches;
    save();
}

void PositionStore::recordExitTranche(const std::string& ticker) {
    ++states_[ticker].exitTranches;
    save();
}

const PositionState& PositionStore::get(const std::string& ticker) const {
    const auto it = states_.find(ticker);
    return it == states_.end() ? empty_ : it->second;
}

}  // namespace trade
