#pragma once

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "strategy/istrategy.hpp"
#include "trade/risk_guard.hpp"

struct StrategyProfile {
    int         id = 0;
    std::string name;
    std::string ticker;
    std::string market;  // "KRX" or "US"
    std::string type;    // "rsi", "macd", "bollinger", "sma_crossover", ...
    // "swing" (mean-reversion, days~weeks), "trend" (trend-following, weeks~months),
    // or "position" (volatility breakout / volume-confirmed, weeks~months).
    std::string    category;
    nlohmann::json params;
    double         positionPct = 1.0;  ///< fraction of cash committed to a full position
    double         stopLossPct = 0.0;  ///< exit when price falls this far below average price; 0 = off

    /* ----- Exit rules beyond the plain stop (all 0 = off) ----- */
    double takeProfitPct   = 0.0;  ///< exit when price rises this far above average price
    double trailingStopPct = 0.0;  ///< exit when price falls this far below the peak seen since entry
    int    cooldownMinutes = 0;    ///< refuse to re-enter this ticker for this long after an exit

    /* ----- Order splitting (1 = all at once) ----- */
    int entryTranches = 1;  ///< buy the position over this many orders
    int exitTranches  = 1;  ///< sell it over this many orders

    /* ----- Intraday trading window, HHMM KST; empty = the whole session ----- */
    std::string tradeStart;  ///< e.g. "0930" to sit out the opening auction noise
    std::string tradeEnd;    ///< e.g. "1520" to stop before the closing auction
    std::string description;

    /**
     * @brief Whether this profile joins the bulk runs (--all, --all-scalp).
     *
     * A disabled profile is still backtestable and still runnable by explicit
     * --id; it simply does not trade by default. This is how a profile kept for
     * research avoids competing with a live one over the same ticker — two
     * strategies on one holding would each act on the other's position.
     */
    bool enabled = true;

    /**
     * @param unused When given, receives every key in `params` that the strategy
     *        never read. nlohmann's value(key, fallback) returns the fallback for a
     *        missing key, so a misspelled parameter is silently ignored and the
     *        strategy runs on defaults — which looks like the parameter having no
     *        effect rather than like an error. That cost a whole parameter sweep
     *        before it was noticed. The warning is always printed; this lets a
     *        test see it.
     */
    [[nodiscard]] std::unique_ptr<IStrategy> createStrategy(std::vector<std::string>* unused = nullptr) const;
};

/**
 * @brief Reads strategy parameters and remembers which keys were asked for.
 *
 * Replaces a hand-maintained list of known keys per strategy type, which covered
 * three of twenty-four types when it was checked and would have kept drifting as
 * parameters were added. A reader that records what was read cannot fall out of
 * step with the code that reads.
 */
class ParamReader {
   public:
    explicit ParamReader(const nlohmann::json& params)
        : params_(params) {}

    template<class T>
    [[nodiscard]] T value(const std::string& key, T fallback) const {
        seen_.insert(key);
        return params_.is_object() ? params_.value(key, fallback) : fallback;
    }

    [[nodiscard]] bool contains(const std::string& key) const {
        seen_.insert(key);
        return params_.is_object() && params_.contains(key);
    }

    /** @brief Keys present in the params that nothing read. */
    [[nodiscard]] std::vector<std::string> unusedKeys() const;

    /** @brief Whether any key was read at all — false when no strategy type matched. */
    [[nodiscard]] bool touched() const { return !seen_.empty(); }

   private:
    const nlohmann::json&         params_;
    mutable std::set<std::string> seen_;
};

class PortfolioConfig {
   public:
    /**
     * @brief Load live positions, resolving strategy references against a catalog.
     *
     * A position may name a strategy by id — the normal case, so a backtest and
     * the trader reference one definition instead of each carrying a copy that
     * can drift — or carry `type` and `params` inline, which keeps a
     * self-contained file usable.
     *
     * A reference that does not resolve is an error, not a default: a mistyped id
     * silently falling back to some other strategy is the kind of failure that
     * would only be noticed from the trades it produced.
     *
     * @param configPath  Live positions file.
     * @param catalogPath Strategy catalog. Empty resolves to config/strategies.json.
     */
    static PortfolioConfig loadFromFile(const std::string& configPath  = "config/live.json",
                                        const std::string& catalogPath = "");

    /** @brief Strategy ids referenced by a position but missing from the catalog. */
    [[nodiscard]] const std::vector<std::string>& unresolvedStrategies() const { return unresolved_; }

    [[nodiscard]] const std::vector<StrategyProfile>& getProfiles() const { return profiles_; }

    [[nodiscard]] const StrategyProfile* findById(int id) const {
        for (const auto& p : profiles_) {
            if (p.id == id)
                return &p;
        }
        return nullptr;
    }

    /**
     * @brief Profile responsible for a holding of this ticker, best effort.
     *
     * Enabled profiles win. Several profiles can share a ticker — a retired one
     * kept for backtesting, a scalping variant, the live one — and returning
     * whichever appears first in the file credited a holding to a strategy that
     * is not trading. A position bought outside any profile matches nothing.
     */
    [[nodiscard]] const StrategyProfile* findByTicker(const std::string& ticker) const {
        const StrategyProfile* fallback = nullptr;
        for (const auto& p : profiles_) {
            if (p.ticker != ticker) {
                continue;
            }
            if (p.enabled) {
                return &p;
            }
            if (fallback == nullptr) {
                fallback = &p;  // nothing live holds this ticker; name something rather than nothing
            }
        }
        return fallback;
    }

    [[nodiscard]] double getInitialCapitalKrw() const { return initialCapitalKrw_; }

    /** @brief Account-wide per-day trading limits from the config's "risk" object. */
    [[nodiscard]] const trade::RiskLimits& getRiskLimits() const { return riskLimits_; }

    /**
     * @brief Last-modified time of the file this config was loaded from, 0 if unknown.
     *
     * Long-running loops compare this against the file on disk so an edit can be
     * picked up without a restart.
     */
    [[nodiscard]] std::int64_t       getSourceMtime() const { return sourceMtime_; }
    [[nodiscard]] const std::string& getSourcePath() const { return sourcePath_; }

    /** @brief True when the source file has changed since it was loaded. */
    [[nodiscard]] bool sourceChanged() const;

   private:
    std::vector<StrategyProfile> profiles_;
    double                       initialCapitalKrw_ = 10000000.0;
    trade::RiskLimits            riskLimits_;
    std::vector<std::string>     unresolved_;
    std::string                  sourcePath_;
    std::int64_t                 sourceMtime_ = 0;
};
