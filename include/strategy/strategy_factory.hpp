#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "strategy/istrategy.hpp"

struct StrategyProfile {
    int id = 0;
    std::string name;
    std::string ticker;
    std::string market;  // "KRX" or "US"
    std::string type;    // "rsi", "macd", "bollinger", "sma_crossover"
    nlohmann::json params;
    double positionPct = 1.0;
    double stopLossPct = 0.0;
    std::string description;

    [[nodiscard]] std::unique_ptr<IStrategy> createStrategy() const;
};

class PortfolioConfig {
public:
    static PortfolioConfig loadFromFile(const std::string& configPath = "config/portfolio.json");

    [[nodiscard]] const std::vector<StrategyProfile>& getProfiles() const {
        return profiles_;
    }

    [[nodiscard]] const StrategyProfile* findById(int id) const {
        for (const auto& p : profiles_) {
            if (p.id == id) return &p;
        }
        return nullptr;
    }

private:
    std::vector<StrategyProfile> profiles_;
};
