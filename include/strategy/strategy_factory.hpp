#pragma once

#include <memory>
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

    [[nodiscard]] std::unique_ptr<IStrategy> createStrategy() const;
};

class PortfolioConfig {
   public:
    static PortfolioConfig loadFromFile(const std::string& configPath = "config/portfolio.json");

    [[nodiscard]] const std::vector<StrategyProfile>& getProfiles() const { return profiles_; }

    [[nodiscard]] const StrategyProfile* findById(int id) const {
        for (const auto& p : profiles_) {
            if (p.id == id)
                return &p;
        }
        return nullptr;
    }

    /**
     * @brief First profile whose ticker matches (best-effort strategy attribution
     *        for a live holding — a position bought outside any profile won't match).
     */
    [[nodiscard]] const StrategyProfile* findByTicker(const std::string& ticker) const {
        for (const auto& p : profiles_) {
            if (p.ticker == ticker)
                return &p;
        }
        return nullptr;
    }

    [[nodiscard]] double getInitialCapitalKrw() const { return initialCapitalKrw_; }

    /** @brief Account-wide per-day trading limits from the config's "risk" object. */
    [[nodiscard]] const trade::RiskLimits& getRiskLimits() const { return riskLimits_; }

   private:
    std::vector<StrategyProfile> profiles_;
    double                       initialCapitalKrw_ = 10000000.0;
    trade::RiskLimits            riskLimits_;
};
