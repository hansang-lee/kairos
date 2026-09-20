#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

/**
 * @brief A strategy definition with no ticker attached.
 *
 * "Bollinger, 40 bars, 2.0 sigma" is a complete idea on its own; which ticker it
 * is pointed at is a separate decision. Keeping them apart means a backtest and
 * the live trader can reference the same definition by id rather than each
 * carrying their own copy of the parameters, which is how the two silently
 * drifted apart before.
 */
struct StrategyDef {
    std::string    id;    ///< referenced by live positions and sweep reports
    std::string    name;  ///< human label; falls back to the id
    std::string    type;  ///< registered strategy type (see StrategyProfile::createStrategy)
    std::string    category;
    nlohmann::json params;
    std::string    description;
};

/**
 * @brief The set of strategies available to backtest or to trade.
 *
 * Loaded from config/strategies.json, which holds both concrete definitions and
 * parameter grids. A grid expands to one definition per combination, so a sweep
 * over parameters needs no code — only a wider grid.
 */
class StrategyCatalog {
   public:
    /** @param path Catalog file. Empty (default) resolves to <project-root>/config/strategies.json. */
    static StrategyCatalog loadFromFile(const std::string& path = "");

    /** @brief Every definition, concrete ones first, then grid expansions. */
    [[nodiscard]] const std::vector<StrategyDef>& all() const { return defs_; }

    /** @brief Definitions not produced by a grid — the named, hand-written ones. */
    [[nodiscard]] std::vector<StrategyDef> concrete() const;

    /** @brief Expansions of one grid, by the type it sweeps. Empty if no such grid. */
    [[nodiscard]] std::vector<StrategyDef> gridFor(const std::string& type) const;

    /** @brief Definition with this id, or nullptr. */
    [[nodiscard]] const StrategyDef* find(const std::string& id) const;

    [[nodiscard]] bool               loaded() const { return loaded_; }
    [[nodiscard]] const std::string& path() const { return path_; }

   private:
    std::vector<StrategyDef> defs_;
    std::vector<bool>        fromGrid_;   ///< parallel to defs_
    std::vector<std::string> gridTypes_;  ///< parallel to defs_, empty for concrete ones
    std::string              path_;
    bool                     loaded_ = false;
};
