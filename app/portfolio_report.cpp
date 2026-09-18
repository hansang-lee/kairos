#include <cmath>
#include <ctime>
#include <iostream>

#include <nlohmann/json.hpp>

#include "broker/kis_trader.hpp"
#include "strategy/strategy_factory.hpp"

/**
 * @brief Snapshot the live KIS paper-trading account (balance + holdings) as JSON,
 *        cross-referenced against config/portfolio.json for strategy attribution.
 *
 * Usage: portfolio_report [config_path] > docs/portfolio.json
 */
int main(int argc, char* argv[]) {
    const std::string configPath = (argc > 1) ? argv[1] : "config/portfolio.json";
    const auto        portfolio  = PortfolioConfig::loadFromFile(configPath);

    const auto balance = KisTrader::getBalance();

    const auto now = std::time(nullptr);
    struct tm* utc = gmtime(&now);
    char       tsBuf[32];
    char       dateBuf[16];
    std::strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%dT%H:%M:%SZ", utc);
    std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", utc);

    nlohmann::json result;
    result["timestamp"] = std::string(tsBuf);
    result["date"]      = std::string(dateBuf);
    result["success"]   = balance.success;

    if (!balance.success) {
        result["message"] = balance.message;
        std::cout << result.dump(2) << std::endl;
        return 1;
    }

    const double initialCapital = portfolio.getInitialCapitalKrw();
    const double totalEval      = balance.totalEvalAmount;
    const double totalReturnPct = (initialCapital > 0.0) ? (totalEval - initialCapital) / initialCapital * 100.0 : 0.0;

    result["initial_capital_krw"] = initialCapital;
    result["cash_balance_krw"]    = balance.cashBalance;
    result["total_eval_krw"]      = totalEval;
    result["total_return_pct"]    = std::round(totalReturnPct * 100.0) / 100.0;

    nlohmann::json holdings = nlohmann::json::array();
    for (const auto& h : balance.holdings) {
        const auto*    profile = portfolio.findByTicker(h.ticker);
        nlohmann::json entry;
        entry["ticker"]          = h.ticker;
        entry["name"]            = h.name;
        entry["quantity"]        = h.quantity;
        entry["avg_price"]       = h.avgPrice;
        entry["current_price"]   = h.currentPrice;
        entry["eval_amount"]     = h.evalAmount;
        entry["profit_loss_pct"] = std::round(h.profitLossRate * 100.0) / 100.0;
        entry["weight_pct"]      = (totalEval > 0.0) ? std::round(h.evalAmount / totalEval * 10000.0) / 100.0 : 0.0;
        if (profile) {
            entry["strategy_name"] = profile->name;
            entry["category"]      = profile->category;
        } else {
            entry["strategy_name"] = nullptr;
            entry["category"]      = nullptr;
        }
        holdings.push_back(entry);
    }
    result["holdings"] = holdings;

    std::cout << result.dump(2) << std::endl;
    return 0;
}
