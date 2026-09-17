#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fng_info.hpp"
#include "fred_info.hpp"

struct MacroScores {
    double growth    = 0.0;  // 0-100 (high = strong economy)
    double inflation = 0.0;  // 0-100 (high = high inflation)
    double liquidity = 0.0;  // 0-100 (high = loose monetary policy)
    double sentiment = 0.0;  // 0-100 (high = bullish)
    double risk      = 0.0;  // 0-100 (high = risky environment)
    double composite = 0.0;  // weighted total
};

enum class Regime {
    Expansion,    // strong growth, moderate inflation
    Overheating,  // strong growth, high inflation
    Slowdown,     // weakening growth, elevated inflation
    Recession     // weak growth, elevated risk
};

struct Allocation {
    double stocks = 0.0;
    double gold   = 0.0;
    double metals = 0.0;
    double bonds  = 0.0;
    double cash   = 0.0;
};

class MacroScorer {
   public:
    /**
     * @brief Run full macro analysis: fetch data, score, detect regime, recommend allocation.
     * @param apiKey FRED API key
     * @param configPath Path to macro_allocation.json
     */
    static bool analyze(const std::string& apiKey, const std::string& configPath);

    /**
     * @brief Run full macro analysis and return results as JSON.
     * @param apiKey FRED API key
     * @param configPath Path to macro_allocation.json
     * @return JSON object with scores, regime, allocation, fng; empty object on failure.
     */
    static nlohmann::json analyzeJson(const std::string& apiKey, const std::string& configPath);

    /**
     * @brief Compute macro category scores using latest values of each series.
     */
    static MacroScores computeScores(const std::map<std::string, std::shared_ptr<FredSeriesInfo>>& fredData,
                                     const std::shared_ptr<FearAndGreedInfo>&                      fngData);

    /**
     * @brief Compute macro category scores at a specific index in the time series.
     *        Used for backtesting — evaluates data at data[index] vs data[index-1].
     * @param index The time index to evaluate (must be >= 1)
     */
    static MacroScores computeScoresAt(const std::map<std::string, std::shared_ptr<FredSeriesInfo>>& fredData,
                                       size_t                                                        index);

    /**
     * @brief Compute weighted composite score from category scores.
     */
    static double computeComposite(const MacroScores& scores, const nlohmann::json& config);

    /**
     * @brief Detect the current economic regime from scores.
     */
    static Regime detectRegime(const MacroScores& scores, const nlohmann::json& config);

    /**
     * @brief Look up allocation from config for given regime.
     */
    static Allocation getAllocation(Regime regime, const nlohmann::json& config);

    [[nodiscard]] static std::string regimeToString(Regime regime);

    /**
     * @brief Clamp value to 0-100 range.
     */
    static double clamp(double value);

   private:
    /**
     * @brief Helper: compute rate of change between last two values of a series.
     */
    static double rateOfChange(const std::shared_ptr<FredSeriesInfo>& series);

    /**
     * @brief Helper: get latest value of a series.
     */
    static double latestValue(const std::shared_ptr<FredSeriesInfo>& series);

    /**
     * @brief Helper: get value at specific index.
     */
    static double valueAt(const std::shared_ptr<FredSeriesInfo>& series, size_t index);

    /**
     * @brief Helper: compute rate of change at specific index (value[i] - value[i-1]).
     */
    static double rateOfChangeAt(const std::shared_ptr<FredSeriesInfo>& series, size_t index);
};
