#include "strategy/strategy_catalog.hpp"

#include <iostream>

#include "common/util.hpp"

namespace {

/**
 * @brief Expand {"period":[14,20],"std_devs":[1.5,2.0]} into every combination.
 *
 * Built iteratively rather than recursively so the order is stable, which keeps
 * generated ids stable between runs — a sweep report is worth little if the same
 * combination is called something different next time.
 */
std::vector<nlohmann::json> expandGrid(const nlohmann::json& axes) {
    std::vector<nlohmann::json> out{nlohmann::json::object()};

    for (const auto& [key, values] : axes.items()) {
        if (!values.is_array() || values.empty()) {
            continue;
        }
        std::vector<nlohmann::json> next;
        next.reserve(out.size() * values.size());
        for (const auto& partial : out) {
            for (const auto& v : values) {
                nlohmann::json combined = partial;
                combined[key]           = v;
                next.push_back(std::move(combined));
            }
        }
        out = std::move(next);
    }
    return out;
}

/** A short, stable label for a parameter combination, e.g. "bollinger(40,2.0)". */
std::string gridId(const std::string& type, const nlohmann::json& params) {
    std::string id    = type + "(";
    bool        first = true;
    for (const auto& [key, value] : params.items()) {
        if (!first) {
            id += ",";
        }
        first = false;
        if (value.is_number_integer()) {
            id += std::to_string(value.get<int64_t>());
        } else if (value.is_number_float()) {
            std::string s = std::to_string(value.get<double>());
            // Trim trailing zeros so 2.000000 reads as 2.0.
            s.erase(s.find_last_not_of('0') + 1);
            if (!s.empty() && s.back() == '.') {
                s += "0";
            }
            id += s;
        } else {
            id += value.dump();
        }
    }
    return id + ")";
}

}  // namespace

StrategyCatalog StrategyCatalog::loadFromFile(const std::string& path) {
    StrategyCatalog cat;
    cat.path_ = path.empty() ? util::resolveFromExe("config/strategies.json") : path;

    const auto j = util::loadJsonConfig(cat.path_);
    if (!j) {
        return cat;
    }

    if (j->contains("strategies") && (*j)["strategies"].is_array()) {
        for (const auto& item : (*j)["strategies"]) {
            StrategyDef d;
            d.id = item.value("id", "");
            if (d.id.empty()) {
                std::cerr << "StrategyCatalog: a strategy without an id was skipped." << std::endl;
                continue;
            }
            d.type = item.value("type", "");
            if (d.type.empty()) {
                std::cerr << "StrategyCatalog: strategy '" << d.id << "' has no type; skipped." << std::endl;
                continue;
            }
            d.name        = item.value("name", d.id);
            d.category    = item.value("category", "");
            d.description = item.value("description", "");
            if (item.contains("params") && item["params"].is_object()) {
                d.params = item["params"];
            }
            cat.defs_.push_back(std::move(d));
            cat.fromGrid_.push_back(false);
            cat.gridTypes_.emplace_back();
        }
    }

    if (j->contains("grids") && (*j)["grids"].is_array()) {
        for (const auto& grid : (*j)["grids"]) {
            const std::string type = grid.value("type", "");
            if (type.empty() || !grid.contains("params") || !grid["params"].is_object()) {
                std::cerr << "StrategyCatalog: a grid without a type or params was skipped." << std::endl;
                continue;
            }
            const std::string category = grid.value("category", "");
            for (auto& params : expandGrid(grid["params"])) {
                StrategyDef d;
                d.id       = gridId(type, params);
                d.name     = d.id;
                d.type     = type;
                d.category = category;
                d.params   = std::move(params);
                cat.defs_.push_back(std::move(d));
                cat.fromGrid_.push_back(true);
                cat.gridTypes_.push_back(type);
            }
        }
    }

    cat.loaded_ = !cat.defs_.empty();
    return cat;
}

std::vector<StrategyDef> StrategyCatalog::concrete() const {
    std::vector<StrategyDef> out;
    for (std::size_t i = 0; i < defs_.size(); ++i) {
        if (!fromGrid_[i]) {
            out.push_back(defs_[i]);
        }
    }
    return out;
}

std::vector<StrategyDef> StrategyCatalog::gridFor(const std::string& type) const {
    std::vector<StrategyDef> out;
    for (std::size_t i = 0; i < defs_.size(); ++i) {
        if (fromGrid_[i] && gridTypes_[i] == type) {
            out.push_back(defs_[i]);
        }
    }
    return out;
}

const StrategyDef* StrategyCatalog::find(const std::string& id) const {
    for (const auto& d : defs_) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}
