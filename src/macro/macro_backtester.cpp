#include "macro/macro_backtester.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>

bool MacroBacktester::isRebalancePoint(size_t monthIndex, const std::string& frequency) {
    if (frequency == "m") {
        return true;
    }
    if (frequency == "q") {
        return (monthIndex % 3) == 0;
    }
    if (frequency == "a") {
        return (monthIndex % 12) == 0;
    }
    return true;
}

MacroBacktestResult MacroBacktester::run(const nlohmann::json&                                         config,
                                         const std::map<std::string, std::shared_ptr<FredSeriesInfo>>& fredData,
                                         const std::map<std::string, std::vector<double>>&             assetReturns,
                                         const std::vector<std::string>& dates, const std::string& frequency,
                                         double initialCapital) {
    MacroBacktestResult result;
    result.frequency      = frequency;
    result.initialCapital = initialCapital;

    if (dates.empty()) {
        result.finalCapital = initialCapital;
        return result;
    }

    const size_t months = dates.size();

    double     equity = initialCapital;
    Allocation currentAlloc;  // starts at zero, first rebalance sets it

    // Find the minimum data length across FRED series to avoid out-of-bounds
    size_t fredMinLen = std::numeric_limits<size_t>::max();
    for (const auto& [id, series] : fredData) {
        if (series) {
            fredMinLen = std::min(fredMinLen, series->values.size());
        }
    }

    // We use the last `months` data points from FRED, so compute offset
    // FRED data[offset + i] corresponds to dates[i]
    const size_t fredOffset = (fredMinLen > months) ? (fredMinLen - months) : 0;

    std::vector<double> equityCurve;
    equityCurve.reserve(months);

    Regime prevRegime        = Regime::Slowdown;
    bool   hadFirstRebalance = false;

    for (size_t i = 0; i < months; ++i) {
        // Rebalance at rebalancing points
        if (isRebalancePoint(i, frequency)) {
            size_t fredIdx = fredOffset + i;
            if (fredIdx > 0 && fredIdx < fredMinLen) {
                auto scores      = MacroScorer::computeScoresAt(fredData, fredIdx);
                scores.composite = MacroScorer::computeComposite(scores, config);
                auto regime      = MacroScorer::detectRegime(scores, config);
                auto newAlloc    = MacroScorer::getAllocation(regime, config);

                bool changed = !hadFirstRebalance || regime != prevRegime;

                MacroBacktestPeriod period;
                period.date         = dates[i];
                period.scores       = scores;
                period.regime       = regime;
                period.prevRegime   = prevRegime;
                period.alloc        = newAlloc;
                period.allocChanged = changed;
                period.equity       = equity;
                result.periods.push_back(period);

                currentAlloc      = newAlloc;
                prevRegime        = regime;
                hadFirstRebalance = true;
                result.rebalanceCount++;
            }
        }

        // Apply monthly returns with current allocation
        double portfolioReturn = 0.0;

        auto applyAsset = [&](const std::string& key, double weight) {
            if (weight <= 0.0 || assetReturns.find(key) == assetReturns.end()) {
                return;
            }
            const auto& returns = assetReturns.at(key);
            if (i < returns.size()) {
                portfolioReturn += (weight / 100.0) * returns[i];
            }
        };

        applyAsset("stocks", currentAlloc.stocks);
        applyAsset("gold", currentAlloc.gold);
        applyAsset("metals", currentAlloc.metals);
        applyAsset("bonds", currentAlloc.bonds);
        applyAsset("cash", currentAlloc.cash);

        equity *= (1.0 + portfolioReturn);
        equityCurve.push_back(equity);

        // Update the last period's monthReturn
        if (!result.periods.empty() && result.periods.back().date == dates[i]) {
            result.periods.back().monthReturn = portfolioReturn * 100.0;
        }
    }

    result.finalCapital   = equity;
    result.totalReturnPct = (equity - initialCapital) / initialCapital * 100.0;

    // CAGR
    double years = static_cast<double>(months) / 12.0;
    if (years > 0.0 && equity > 0.0) {
        result.cagr = (std::pow(equity / initialCapital, 1.0 / years) - 1.0) * 100.0;
    }

    // Max Drawdown
    {
        double peak  = equityCurve[0];
        double maxDD = 0.0;
        for (const auto& eq : equityCurve) {
            peak            = std::max(peak, eq);
            const double dd = (eq - peak) / peak * 100.0;
            maxDD           = std::min(maxDD, dd);
        }
        result.maxDrawdownPct = maxDD;
    }

    // Sharpe Ratio (annualized from monthly returns)
    {
        std::vector<double> monthlyReturns;
        monthlyReturns.reserve(months);
        monthlyReturns.push_back(0.0);  // first month has no prior
        for (size_t i = 1; i < equityCurve.size(); ++i) {
            if (equityCurve[i - 1] > 0.0) {
                monthlyReturns.push_back((equityCurve[i] - equityCurve[i - 1]) / equityCurve[i - 1]);
            }
        }

        if (monthlyReturns.size() > 1) {
            const double mean = std::accumulate(monthlyReturns.begin(), monthlyReturns.end(), 0.0)
                              / static_cast<double>(monthlyReturns.size());

            double variance = 0.0;
            for (const auto& r : monthlyReturns) {
                variance += (r - mean) * (r - mean);
            }
            variance /= static_cast<double>(monthlyReturns.size());

            const double stdDev = std::sqrt(variance);
            if (stdDev > 1e-12) {
                result.sharpeRatio = (mean / stdDev) * std::sqrt(12.0);  // annualize monthly
            }
        }
    }

    return result;
}

MacroBacktestResult MacroBacktester::computeBenchmark(const std::vector<double>&      monthlyReturns,
                                                      const std::vector<std::string>& dates, const std::string& ticker,
                                                      double initialCapital) {
    MacroBacktestResult result;
    result.frequency      = "b&h";
    result.initialCapital = initialCapital;

    double              equity = initialCapital;
    std::vector<double> equityCurve;
    equityCurve.reserve(monthlyReturns.size());

    for (size_t i = 0; i < monthlyReturns.size(); ++i) {
        equity *= (1.0 + monthlyReturns[i]);
        equityCurve.push_back(equity);
    }

    result.finalCapital   = equity;
    result.totalReturnPct = (equity - initialCapital) / initialCapital * 100.0;

    double years = static_cast<double>(monthlyReturns.size()) / 12.0;
    if (years > 0.0 && equity > 0.0) {
        result.cagr = (std::pow(equity / initialCapital, 1.0 / years) - 1.0) * 100.0;
    }

    // MDD
    {
        double peak  = equityCurve[0];
        double maxDD = 0.0;
        for (const auto& eq : equityCurve) {
            peak            = std::max(peak, eq);
            const double dd = (eq - peak) / peak * 100.0;
            maxDD           = std::min(maxDD, dd);
        }
        result.maxDrawdownPct = maxDD;
    }

    // Sharpe
    {
        if (monthlyReturns.size() > 1) {
            const double mean = std::accumulate(monthlyReturns.begin(), monthlyReturns.end(), 0.0)
                              / static_cast<double>(monthlyReturns.size());
            double variance = 0.0;
            for (const auto& r : monthlyReturns) {
                variance += (r - mean) * (r - mean);
            }
            variance /= static_cast<double>(monthlyReturns.size());
            const double stdDev = std::sqrt(variance);
            if (stdDev > 1e-12) {
                result.sharpeRatio = (mean / stdDev) * std::sqrt(12.0);
            }
        }
    }

    return result;
}

void MacroBacktester::printResults(const std::vector<MacroBacktestResult>& results,
                                   const MacroBacktestResult&              benchmark) {
    // clang-format off
    std::clog << std::endl;
    std::clog << "=== Macro Portfolio Backtest ===" << std::endl;
    std::clog << std::endl;

    std::clog << std::left  << std::setw(12) << "Frequency"
              << std::right << std::setw(12) << "Rebalances"
              << std::setw(10) << "CAGR"
              << std::setw(10) << "Sharpe"
              << std::setw(10) << "MaxDD"
              << std::setw(15) << "Final($10k)"
              << std::endl;
    std::clog << std::string(69, '-') << std::endl;

    auto printRow = [](const MacroBacktestResult& r, const std::string& label) {
        std::clog << std::left  << std::setw(12) << label
                  << std::right << std::setw(12) << r.rebalanceCount
                  << std::fixed << std::setprecision(1)
                  << std::setw(9) << r.cagr << "%"
                  << std::setprecision(2)
                  << std::setw(10) << r.sharpeRatio
                  << std::setprecision(1)
                  << std::setw(9) << r.maxDrawdownPct << "%"
                  << std::setw(5) << " $" << std::setprecision(0) << std::setw(10)
                  << r.finalCapital
                  << std::endl;
    };

    for (const auto& r : results) {
        std::string label;
        if (r.frequency == "m") label = "Monthly";
        else if (r.frequency == "q") label = "Quarterly";
        else if (r.frequency == "a") label = "Annually";
        else label = r.frequency;
        printRow(r, label);
    }

    std::clog << std::string(69, '-') << std::endl;
    printRow(benchmark, "SPY (B&H)");
    // clang-format on

    // Print detailed timeline for monthly
    for (const auto& r : results) {
        if (r.frequency == "m" && !r.periods.empty()) {
            std::clog << std::endl;
            std::clog << "=== Detailed Regime Timeline ===" << std::endl;

            for (const auto& p : r.periods) {
                std::clog << std::endl;
                std::clog << "--- " << p.date << " ---" << std::endl;

                // Scores
                std::clog << "  Indicators:  " << "Growth=" << std::fixed << std::setprecision(1) << p.scores.growth
                          << "  Inflation=" << p.scores.inflation << "  Liquidity=" << p.scores.liquidity
                          << "  Sentiment=" << p.scores.sentiment << "  Risk=" << p.scores.risk
                          << "  (Composite=" << p.scores.composite << ")" << std::endl;

                // Regime
                if (p.allocChanged) {
                    std::clog << "  Regime:      " << MacroScorer::regimeToString(p.prevRegime) << " -> "
                              << MacroScorer::regimeToString(p.regime) << "  ** REBALANCED **" << std::endl;
                } else {
                    std::clog << "  Regime:      " << MacroScorer::regimeToString(p.regime) << "  (maintained)"
                              << std::endl;
                }

                std::clog << "  Allocation:  " << "Stocks=" << static_cast<int>(p.alloc.stocks) << "%"
                          << "  Gold=" << static_cast<int>(p.alloc.gold) << "%"
                          << "  Metals=" << static_cast<int>(p.alloc.metals) << "%"
                          << "  Bonds=" << static_cast<int>(p.alloc.bonds) << "%"
                          << "  Cash=" << static_cast<int>(p.alloc.cash) << "%" << std::endl;

                // Equity
                std::clog << "  Equity:      $" << std::setprecision(0) << p.equity << "  (month: " << std::showpos
                          << std::setprecision(1) << p.monthReturn << "%" << std::noshowpos << ")" << std::endl;
            }

            // Final summary
            std::clog << std::endl;
            std::clog << "=== Summary ===" << std::endl;
            std::clog << "  Period:        " << r.periods.front().date << " ~ " << r.periods.back().date << std::endl;
            std::clog << "  Rebalances:    " << r.rebalanceCount << std::endl;
            std::clog << std::fixed << std::setprecision(1);
            std::clog << "  Total Return:  " << r.totalReturnPct << "%" << std::endl;
            std::clog << "  CAGR:          " << r.cagr << "%" << std::endl;
            std::clog << std::setprecision(2);
            std::clog << "  Sharpe Ratio:  " << r.sharpeRatio << std::endl;
            std::clog << std::setprecision(1);
            std::clog << "  Max Drawdown:  " << r.maxDrawdownPct << "%" << std::endl;
            std::clog << std::setprecision(0);
            std::clog << "  Final Capital: $" << r.finalCapital << std::endl;

            break;
        }
    }
}
