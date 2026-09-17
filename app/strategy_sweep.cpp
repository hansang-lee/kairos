#include <algorithm>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "backtest/backtest_engine.hpp"
#include "bollinger_strategy.hpp"
#include "common/util.hpp"
#include "data/kis_provider.hpp"
#include "macd_strategy.hpp"
#include "rsi_strategy.hpp"
#include "sma_crossover.hpp"
#include "stock_info.hpp"
#include "yfinance.hpp"

struct SweepRow {
    std::string strategyName;
    double      totalReturnPct = 0.0;
    double      cagr           = 0.0;
    double      maxDrawdownPct = 0.0;
    double      sharpeRatio    = 0.0;
    double      winRate        = 0.0;
    double      profitFactor   = 0.0;
    std::size_t tradeCount     = 0;
    double      score          = 0.0;
    bool        isBest         = false;
};

void runSweepForStock(const std::string& symbol, const std::string& assetName, const std::shared_ptr<StockInfo>& data,
                      const BacktestConfig& config) {
    if (!data || data->close.empty()) {
        std::cerr << "[-] No data available for " << symbol << " (" << assetName << ")\n" << std::endl;
        return;
    }

    std::cout << "\n========================================================================================\n";
    std::cout << " 📊 Asset: " << symbol << " [" << assetName << "] | Bars: " << data->close.size()
              << " | Initial Capital: 10,000 " << data->currency << "\n";
    std::cout << "========================================================================================\n";

    std::vector<std::unique_ptr<IStrategy>> strategies;
    strategies.push_back(std::make_unique<SmaCrossover>(20, 50));
    strategies.push_back(std::make_unique<RsiStrategy>(14, 30.0, 70.0));
    strategies.push_back(std::make_unique<MacdStrategy>(12, 26, 9));
    strategies.push_back(std::make_unique<BollingerStrategy>(20, 2.0));

    BacktestEngine        engine(10000.0);
    std::vector<SweepRow> rows;
    double                maxScore = -1.0;
    std::size_t           bestIdx  = 0;

    for (std::size_t i = 0; i < strategies.size(); ++i) {
        auto&      strat = strategies[i];
        const auto res   = engine.run(*strat, *data, config);

        SweepRow row;
        row.strategyName   = strat->name();
        row.totalReturnPct = res.totalReturnPct;
        row.cagr           = res.cagr;
        row.maxDrawdownPct = res.maxDrawdownPct;
        row.sharpeRatio    = res.sharpeRatio;
        row.winRate        = res.winRate * 100.0;
        row.profitFactor   = res.profitFactor;
        row.tradeCount     = res.trades.size();
        row.score          = res.score;

        if (row.score > maxScore) {
            maxScore = row.score;
            bestIdx  = i;
        }
        rows.push_back(row);
    }

    if (!rows.empty()) {
        rows[bestIdx].isBest = true;
    }

    // Print formatted table
    std::cout << std::left << std::setw(26) << "Strategy" << std::right << std::setw(11) << "Return(%)" << std::setw(10)
              << "CAGR(%)" << std::setw(10) << "MDD(%)" << std::setw(9) << "Sharpe" << std::setw(10) << "WinRate"
              << std::setw(9) << "PF" << std::setw(8) << "Trades" << std::setw(8) << "Score" << "  Notice" << "\n";
    std::cout << std::string(96, '-') << "\n";

    for (const auto& r : rows) {
        std::cout << std::left << std::setw(26) << r.strategyName << std::right << std::fixed << std::setprecision(2)
                  << std::setw(11) << r.totalReturnPct << std::setw(10) << r.cagr << std::setw(10) << r.maxDrawdownPct
                  << std::setprecision(2) << std::setw(9) << r.sharpeRatio << std::setprecision(1) << std::setw(9)
                  << r.winRate << "%" << std::setprecision(2) << std::setw(9) << r.profitFactor << std::setw(8)
                  << r.tradeCount << std::setprecision(1) << std::setw(8) << r.score;

        if (r.isBest) {
            std::cout << "  ★ BEST";
        }
        std::cout << "\n";
    }
}

int main() {
    std::cout << "########################################################################################\n";
    std::cout << "#                      QUANT STRATEGY MULTI-ASSET SWEEP                                #\n";
    std::cout << "#  Evaluating: SMA Crossover, RSI, MACD, Bollinger Bands on KRX & US Markets           #\n";
    std::cout << "########################################################################################\n";

    yFinance::init();
    util::Defer _cleanup([] { yFinance::close(); });

    // 1. Korea Stocks (KRX) via KIS Provider
    KisProvider    kis;
    BacktestConfig krConfig;
    krConfig.commissionRate = 0.00015;  // 0.015%
    krConfig.slippagePct    = 0.0005;   // 0.05%

    std::cout << "\n>>> [1/2] Fetching & Sweeping Korean Market (KRX)..." << std::endl;
    // 2023-09-01 ~ 2024-09-01 (1 year daily bars)
    auto samsung = kis.getStockInfo("005930", "2023-09-01", "2024-09-01");
    runSweepForStock("005930", "삼성전자", samsung, krConfig);

    auto hynix = kis.getStockInfo("000660", "2023-09-01", "2024-09-01");
    runSweepForStock("000660", "SK하이닉스", hynix, krConfig);

    // 2. US Stocks via Yahoo Finance Provider
    BacktestConfig usConfig;
    usConfig.commissionRate = 0.0005;  // 0.05%
    usConfig.slippagePct    = 0.0005;  // 0.05%

    std::cout << "\n>>> [2/2] Fetching & Sweeping US Market (NASDAQ/NYSE)..." << std::endl;
    auto qqq = yFinance::getStockInfo("QQQ", "1d", "1y");
    runSweepForStock("QQQ", "Invesco QQQ Trust", qqq, usConfig);

    auto aapl = yFinance::getStockInfo("AAPL", "1d", "1y");
    runSweepForStock("AAPL", "Apple Inc.", aapl, usConfig);

    std::cout << "\n========================================================================================\n";
    std::cout << " ✔ Strategy Sweep Complete! High-scoring strategies are ready for Paper Trading.\n";
    std::cout << "========================================================================================\n\n";

    return 0;
}
