#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>

#include <nlohmann/json.hpp>

#include "common/util.hpp"
#include "macro_scorer.hpp"
#include "yfinance.hpp"

double MacroScorer::clamp(double value) {
    return std::max(0.0, std::min(100.0, value));
}

double MacroScorer::rateOfChange(const std::shared_ptr<FredSeriesInfo>& series) {
    if (!series || series->values.size() < 2) {
        return 0.0;
    }
    const auto n = series->values.size();
    return series->values[n - 1] - series->values[n - 2];
}

double MacroScorer::latestValue(const std::shared_ptr<FredSeriesInfo>& series) {
    if (!series || series->values.empty()) {
        return 0.0;
    }
    return series->values.back();
}

double MacroScorer::valueAt(const std::shared_ptr<FredSeriesInfo>& series, size_t index) {
    if (!series || index >= series->values.size()) {
        return 0.0;
    }
    return series->values[index];
}

double MacroScorer::rateOfChangeAt(const std::shared_ptr<FredSeriesInfo>& series, size_t index) {
    if (!series || index == 0 || index >= series->values.size()) {
        return 0.0;
    }
    return series->values[index] - series->values[index - 1];
}

/* Scoring helper: generic scoring with value/change getters */
namespace {

struct DataAccessor {
    using GetValue  = std::function<double(const std::shared_ptr<FredSeriesInfo>&)>;
    using GetChange = std::function<double(const std::shared_ptr<FredSeriesInfo>&)>;
    GetValue  getValue;
    GetChange getChange;
};

double scoreGrowth(const std::map<std::string, std::shared_ptr<FredSeriesInfo>>& data, const DataAccessor& acc) {
    double s = 50.0;

    if (data.count("UNRATE")) {
        double roc = acc.getChange(data.at("UNRATE"));
        double lvl = acc.getValue(data.at("UNRATE"));
        s += (-roc) * 100.0;
        s += (5.0 - lvl) * 5.0;
    }

    if (data.count("PAYEMS")) {
        double roc = acc.getChange(data.at("PAYEMS"));
        s += (roc / 200.0) * 10.0;
    }

    if (data.count("INDPRO")) {
        double roc = acc.getChange(data.at("INDPRO"));
        s += roc * 5.0;
    }

    return MacroScorer::clamp(s);
}

double scoreInflation(const std::map<std::string, std::shared_ptr<FredSeriesInfo>>& data, const DataAccessor& acc) {
    double s = 50.0;

    if (data.count("CPIAUCSL")) {
        double roc    = acc.getChange(data.at("CPIAUCSL"));
        double val    = acc.getValue(data.at("CPIAUCSL"));
        double pctChg = (val > 0) ? (roc / val) * 100.0 * 12.0 : 0.0;
        s += (pctChg - 2.0) * 10.0;
    }

    if (data.count("CPILFESL")) {
        double roc    = acc.getChange(data.at("CPILFESL"));
        double val    = acc.getValue(data.at("CPILFESL"));
        double pctChg = (val > 0) ? (roc / val) * 100.0 * 12.0 : 0.0;
        s += (pctChg - 2.0) * 8.0;
    }

    if (data.count("PCEPI")) {
        double roc    = acc.getChange(data.at("PCEPI"));
        double val    = acc.getValue(data.at("PCEPI"));
        double pctChg = (val > 0) ? (roc / val) * 100.0 * 12.0 : 0.0;
        s += (pctChg - 2.0) * 7.0;
    }

    return MacroScorer::clamp(s);
}

double scoreLiquidity(const std::map<std::string, std::shared_ptr<FredSeriesInfo>>& data, const DataAccessor& acc) {
    double s = 50.0;

    if (data.count("M2REAL")) {
        double roc    = acc.getChange(data.at("M2REAL"));
        double val    = acc.getValue(data.at("M2REAL"));
        double pctChg = (val > 0) ? (roc / val) * 100.0 : 0.0;
        s += pctChg * 30.0;
    }

    if (data.count("WM2NS")) {
        double roc    = acc.getChange(data.at("WM2NS"));
        double val    = acc.getValue(data.at("WM2NS"));
        double pctChg = (val > 0) ? (roc / val) * 100.0 : 0.0;
        s += pctChg * 20.0;
    }

    if (data.count("FEDFUNDS")) {
        double rate = acc.getValue(data.at("FEDFUNDS"));
        s += (3.0 - rate) * 5.0;
    }

    return MacroScorer::clamp(s);
}

double scoreSentiment(const std::map<std::string, std::shared_ptr<FredSeriesInfo>>& data, const DataAccessor& acc,
                      const std::shared_ptr<FearAndGreedInfo>& fng) {
    double s = 50.0;
    int    n = 0;

    if (fng) {
        s = fng->score;
        n++;
    }

    if (data.count("UMCSENT")) {
        double val   = acc.getValue(data.at("UMCSENT"));
        double normd = MacroScorer::clamp((val - 50.0) * (100.0 / 60.0));
        if (n > 0) {
            s = (s + normd) / 2.0;
        } else {
            s = normd;
        }
    }

    return MacroScorer::clamp(s);
}

double scoreRisk(const std::map<std::string, std::shared_ptr<FredSeriesInfo>>& data, const DataAccessor& acc) {
    double s = 50.0;

    if (data.count("T10Y2Y")) {
        double spread = acc.getValue(data.at("T10Y2Y"));
        s += (-spread) * 10.0;
    }

    if (data.count("BAMLH0A0HYM2")) {
        double spread = acc.getValue(data.at("BAMLH0A0HYM2"));
        s += (spread - 4.0) * 8.0;
    }

    return MacroScorer::clamp(s);
}

struct MacroInputs {
    std::map<std::string, std::shared_ptr<FredSeriesInfo>> fredData;
    std::shared_ptr<FearAndGreedInfo>                      fngData;
};

/* Fetch the fixed set of FRED series plus the CNN Fear & Greed index used by macro analysis. */
MacroInputs fetchMacroData(const std::string& apiKey) {
    // clang-format off
    const std::vector<std::string> seriesIds = {
        "UNRATE", "PAYEMS", "INDPRO",
        "CPIAUCSL", "CPILFESL", "PCEPI",
        "M2REAL", "WM2NS", "FEDFUNDS",
        "UMCSENT",
        "T10Y2Y", "BAMLH0A0HYM2"
    };
    // clang-format on

    std::cerr << "Fetching FRED data..." << std::endl;

    MacroInputs inputs;
    for (const auto& id : seriesIds) {
        auto result = yFinance::getFredSeries(id, apiKey, "", "", "m");
        if (result && !result->values.empty()) {
            inputs.fredData[id] = result;
            std::cerr << "  [OK] " << id << " (" << result->values.size() << " observations)" << std::endl;
        } else {
            std::cerr << "  [WARN] " << id << " - no data" << std::endl;
        }
    }

    std::cerr << "Fetching Fear & Greed Index..." << std::endl;
    inputs.fngData = yFinance::getFearAndGreedIndex();
    if (inputs.fngData) {
        std::cerr << "  [OK] FNG score: " << inputs.fngData->score << " (" << inputs.fngData->rating << ")"
                  << std::endl;
    } else {
        std::cerr << "  [WARN] FNG - no data" << std::endl;
    }

    return inputs;
}

}  // namespace

MacroScores MacroScorer::computeScores(const std::map<std::string, std::shared_ptr<FredSeriesInfo>>& data,
                                       const std::shared_ptr<FearAndGreedInfo>&                      fng) {
    DataAccessor acc;
    acc.getValue = [](const std::shared_ptr<FredSeriesInfo>& s) {
        return MacroScorer::latestValue(s);
    };
    acc.getChange = [](const std::shared_ptr<FredSeriesInfo>& s) {
        return MacroScorer::rateOfChange(s);
    };

    MacroScores scores;
    scores.growth    = scoreGrowth(data, acc);
    scores.inflation = scoreInflation(data, acc);
    scores.liquidity = scoreLiquidity(data, acc);
    scores.sentiment = scoreSentiment(data, acc, fng);
    scores.risk      = scoreRisk(data, acc);
    return scores;
}

MacroScores MacroScorer::computeScoresAt(const std::map<std::string, std::shared_ptr<FredSeriesInfo>>& data,
                                         size_t                                                        index) {
    DataAccessor acc;
    acc.getValue = [index](const std::shared_ptr<FredSeriesInfo>& s) {
        return MacroScorer::valueAt(s, index);
    };
    acc.getChange = [index](const std::shared_ptr<FredSeriesInfo>& s) {
        return MacroScorer::rateOfChangeAt(s, index);
    };

    MacroScores scores;
    scores.growth    = scoreGrowth(data, acc);
    scores.inflation = scoreInflation(data, acc);
    scores.liquidity = scoreLiquidity(data, acc);
    scores.sentiment = scoreSentiment(data, acc, nullptr);  // no FNG for historical
    scores.risk      = scoreRisk(data, acc);
    return scores;
}

double MacroScorer::computeComposite(const MacroScores& scores, const nlohmann::json& config) {
    if (!config.contains("scoring_weights")) {
        return 50.0;
    }
    const auto& w = config["scoring_weights"];
    return scores.growth * w.value("growth", 0.25) + (100.0 - scores.inflation) * w.value("inflation", 0.20)
         + scores.liquidity * w.value("liquidity", 0.20) + scores.sentiment * w.value("sentiment", 0.15)
         + (100.0 - scores.risk) * w.value("risk", 0.20);
}

Regime MacroScorer::detectRegime(const MacroScores& scores, const nlohmann::json& config) {
    const auto& thresholds = config["regime_thresholds"];

    // 1. Overheating: High Composite + High Inflation
    if (scores.composite >= thresholds["overheating"].value("composite_min", 45.0)
        && scores.inflation >= thresholds["overheating"].value("inflation_min", 65.0)) {
        return Regime::Overheating;
    }

    // 2. Expansion: High Composite + Low/Moderate Inflation
    if (scores.composite >= thresholds["expansion"].value("composite_min", 60.0)
        && scores.inflation < thresholds["expansion"].value("inflation_max", 65.0)) {
        return Regime::Expansion;
    }

    // 3. Recession: Very Low Composite or Extreme Risk
    if (scores.composite < thresholds["slowdown"].value("composite_min", 25.0) || scores.risk >= 70.0) {
        return Regime::Recession;
    }

    // 4. Slowdown: Moderate Composite without high risk/inflation
    return Regime::Slowdown;
}

Allocation MacroScorer::getAllocation(Regime regime, const nlohmann::json& config) {
    std::string regimeKey;
    switch (regime) {
    case Regime::Expansion:
        regimeKey = "expansion";
        break;
    case Regime::Overheating:
        regimeKey = "overheating";
        break;
    case Regime::Slowdown:
        regimeKey = "slowdown";
        break;
    case Regime::Recession:
        regimeKey = "recession";
        break;
    }

    Allocation alloc;
    if (config.contains("allocation") && config["allocation"].contains(regimeKey)) {
        const auto& a = config["allocation"][regimeKey];
        alloc.stocks  = a.value("stocks", 0.0);
        alloc.gold    = a.value("gold", 0.0);
        alloc.metals  = a.value("metals", 0.0);
        alloc.bonds   = a.value("bonds", 0.0);
        alloc.cash    = a.value("cash", 0.0);
    }

    return alloc;
}

std::string MacroScorer::regimeToString(Regime regime) {
    switch (regime) {
    case Regime::Expansion:
        return "EXPANSION";
    case Regime::Overheating:
        return "OVERHEATING";
    case Regime::Slowdown:
        return "SLOWDOWN";
    case Regime::Recession:
        return "RECESSION";
    }
    return "UNKNOWN";
}

bool MacroScorer::analyze(const std::string& apiKey, const std::string& configPath) {
    const auto config = util::loadJsonConfig(configPath);
    if (!config) {
        return false;
    }

    const auto inputs = fetchMacroData(apiKey);

    /* Compute scores */
    auto scores = computeScores(inputs.fredData, inputs.fngData);

    /* Apply weights */
    scores.composite = computeComposite(scores, *config);

    /* Detect regime */
    auto regime = detectRegime(scores, *config);

    /* Get allocation */
    auto alloc = getAllocation(regime, *config);

    /* Print results */
    // clang-format off
    std::clog << std::endl;
    std::clog << "=== Macro Regime Analysis ===" << std::endl;
    std::clog << std::endl;
    std::clog << std::left << std::setw(20) << "Category"
              << std::right << std::setw(10) << "Score" << std::endl;
    std::clog << std::string(30, '-') << std::endl;
    std::clog << std::left << std::setw(20) << "Growth"
              << std::right << std::fixed << std::setprecision(1) << std::setw(10) << scores.growth << std::endl;
    std::clog << std::left << std::setw(20) << "Inflation"
              << std::right << std::setw(10) << scores.inflation << std::endl;
    std::clog << std::left << std::setw(20) << "Liquidity"
              << std::right << std::setw(10) << scores.liquidity << std::endl;
    std::clog << std::left << std::setw(20) << "Sentiment"
              << std::right << std::setw(10) << scores.sentiment << std::endl;
    std::clog << std::left << std::setw(20) << "Risk"
              << std::right << std::setw(10) << scores.risk << std::endl;
    std::clog << std::string(30, '-') << std::endl;
    std::clog << std::left << std::setw(20) << "Composite"
              << std::right << std::setw(10) << scores.composite << std::endl;
    std::clog << std::endl;
    std::clog << "Current Regime: " << regimeToString(regime) << std::endl;

    std::clog << std::endl;
    std::clog << "=== Recommended Allocation ===" << std::endl;
    std::clog << std::endl;
    std::clog << std::left << std::setw(20) << "Asset"
              << std::right << std::setw(10) << "Weight" << std::endl;
    std::clog << std::string(30, '-') << std::endl;
    std::clog << std::left << std::setw(20) << "Stocks"
              << std::right << std::setw(9) << static_cast<int>(alloc.stocks) << "%" << std::endl;
    std::clog << std::left << std::setw(20) << "Gold"
              << std::right << std::setw(9) << static_cast<int>(alloc.gold) << "%" << std::endl;
    std::clog << std::left << std::setw(20) << "Metals (Ag/Cu)"
              << std::right << std::setw(9) << static_cast<int>(alloc.metals) << "%" << std::endl;
    std::clog << std::left << std::setw(20) << "Bonds (US Treasury)"
              << std::right << std::setw(9) << static_cast<int>(alloc.bonds) << "%" << std::endl;
    std::clog << std::left << std::setw(20) << "Cash"
              << std::right << std::setw(9) << static_cast<int>(alloc.cash) << "%" << std::endl;
    // clang-format on

    return true;
}

nlohmann::json MacroScorer::analyzeJson(const std::string& apiKey, const std::string& configPath) {
    const auto config = util::loadJsonConfig(configPath);
    if (!config) {
        return {};
    }

    const auto inputs = fetchMacroData(apiKey);

    /* Compute scores */
    auto scores      = computeScores(inputs.fredData, inputs.fngData);
    scores.composite = computeComposite(scores, *config);

    /* Detect regime */
    auto regime = detectRegime(scores, *config);

    /* Get allocation */
    auto alloc = getAllocation(regime, *config);

    /* Build JSON */
    // Timestamp
    auto       now = std::time(nullptr);
    struct tm* utc = gmtime(&now);
    char       timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", utc);

    nlohmann::json result;
    result["timestamp"] = std::string(timeBuf);

    result["scores"] = {{"growth", std::round(scores.growth * 10.0) / 10.0},
                        {"inflation", std::round(scores.inflation * 10.0) / 10.0},
                        {"liquidity", std::round(scores.liquidity * 10.0) / 10.0},
                        {"sentiment", std::round(scores.sentiment * 10.0) / 10.0},
                        {"risk", std::round(scores.risk * 10.0) / 10.0},
                        {"composite", std::round(scores.composite * 10.0) / 10.0}};

    result["regime"] = regimeToString(regime);

    result["allocation"] = {{"stocks", static_cast<int>(alloc.stocks)},
                            {"gold", static_cast<int>(alloc.gold)},
                            {"metals", static_cast<int>(alloc.metals)},
                            {"bonds", static_cast<int>(alloc.bonds)},
                            {"cash", static_cast<int>(alloc.cash)}};

    if (inputs.fngData) {
        result["fng"] = {{"score", inputs.fngData->score}, {"rating", inputs.fngData->rating}};
    } else {
        result["fng"] = nullptr;
    }

    /* Details and Summary */
    nlohmann::json details   = nlohmann::json::object();
    auto           addDetail = [&](const std::string& key, const std::string& id) {
        if (inputs.fredData.count(id)) {
            const auto& series = inputs.fredData.at(id);
            double      val    = latestValue(series);
            double      roc    = rateOfChange(series);
            details[key]       = {{"value", val}, {"change", roc}};
        }
    };

    addDetail("unrate", "UNRATE");
    addDetail("payems", "PAYEMS");
    addDetail("indpro", "INDPRO");
    addDetail("cpi", "CPIAUCSL");
    addDetail("fedfunds", "FEDFUNDS");
    addDetail("yield_curve", "T10Y2Y");
    addDetail("hy_spread", "BAMLH0A0HYM2");

    result["details"] = details;

    std::string summary;
    if (regime == Regime::Expansion) {
        summary =
            "The economy is in an expansionary phase. Growth indicators are generally robust while inflation "
            "remains within target bounds. This environment typically favors risk assets like stocks.";
    } else if (regime == Regime::Overheating) {
        summary =
            "Signs of overheating are emerging. Strong growth is being coupled with rising inflationary pressure. "
            "Monetary tightening or market volatility may follow as the cycle matures.";
    } else if (regime == Regime::Slowdown) {
        summary =
            "Economic momentum is decelerating. While not yet in a recession, growth indicators are weakening "
            "and risk factors are rising. A more cautious asset allocation is recommended.";
    } else if (regime == Regime::Recession) {
        summary =
            "The analysis suggests recessionary conditions. Growth is contracting, and financial stress "
            "indicators are likely high. Capital preservation and defensive positioning are paramount.";
    }
    result["summary"] = summary;

    return result;
}
