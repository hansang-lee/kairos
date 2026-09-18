#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "strategy/istrategy.hpp"

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
    double         positionPct = 1.0;
    double         stopLossPct = 0.0;
    std::string    description;

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

   private:
    std::vector<StrategyProfile> profiles_;
    double                       initialCapitalKrw_ = 10000000.0;
};
