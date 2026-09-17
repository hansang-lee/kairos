#include "strategy/strategy_factory.hpp"

#include <fstream>
#include <iostream>

#include "bollinger_strategy.hpp"
#include "macd_strategy.hpp"
#include "rsi_strategy.hpp"
#include "sma_crossover.hpp"

std::unique_ptr<IStrategy> StrategyProfile::createStrategy() const {
    if (type == "rsi") {
        const std::size_t period     = params.value("period", 14);
        const double      oversold   = params.value("oversold", 30.0);
        const double      overbought = params.value("overbought", 70.0);
        return std::make_unique<RsiStrategy>(period, oversold, overbought);
    }
    if (type == "macd") {
        const std::size_t fast   = params.value("fast", 12);
        const std::size_t slow   = params.value("slow", 26);
        const std::size_t signal = params.value("signal", 9);
        return std::make_unique<MacdStrategy>(fast, slow, signal);
    }
    if (type == "bollinger") {
        const std::size_t period    = params.value("period", 20);
        const double      numStdDev = params.value("std_dev", 2.0);
        return std::make_unique<BollingerStrategy>(period, numStdDev);
    }
    if (type == "sma_crossover" || type == "sma") {
        const std::size_t shortWin = params.value("short_window", 20);
        const std::size_t longWin  = params.value("long_window", 50);
        return std::make_unique<SmaCrossover>(shortWin, longWin);
    }

    std::cerr << "StrategyProfile: Unknown strategy type '" << type << "'" << std::endl;
    return nullptr;
}

PortfolioConfig PortfolioConfig::loadFromFile(const std::string& configPath) {
    PortfolioConfig cfg;
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "PortfolioConfig: Cannot open file " << configPath << std::endl;
        return cfg;
    }

    try {
        nlohmann::json j;
        file >> j;

        if (j.contains("strategies") && j["strategies"].is_array()) {
            for (const auto& item : j["strategies"]) {
                StrategyProfile p;
                p.id          = item.value("id", 0);
                p.name        = item.value("name", "");
                p.ticker      = item.value("ticker", "");
                p.market      = item.value("market", "KRX");
                p.type        = item.value("type", "");
                p.positionPct = item.value("position_pct", 1.0);
                p.stopLossPct = item.value("stop_loss_pct", 0.0);
                p.description = item.value("description", "");

                if (item.contains("params") && item["params"].is_object()) {
                    p.params = item["params"];
                }
                cfg.profiles_.push_back(p);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "PortfolioConfig: JSON parse error: " << e.what() << std::endl;
    }

    return cfg;
}
