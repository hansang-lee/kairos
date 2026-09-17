#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include <unistd.h>

#include <nlohmann/json.hpp>

#include "common/util.hpp"
#include "macro/macro_backtester.hpp"
#include "macro_scorer.hpp"
#include "yfinance.hpp"

static std::string computeWarmupDate(const std::string& date, int monthsEarlier) {
    int y = std::stoi(date.substr(0, 4));
    int m = std::stoi(date.substr(5, 2));
    m -= monthsEarlier;
    while (m <= 0) {
        m += 12;
        y--;
    }
    char buf[11];
    snprintf(buf, sizeof(buf), "%04d-%02d-01", y, m);
    return std::string(buf);
}

static std::vector<double> buildMonthlyReturns(const std::shared_ptr<StockInfo>& stock) {
    std::vector<double> returns;
    if (!stock || stock->close.size() < 2)
        return returns;
    returns.reserve(stock->close.size() - 1);
    for (size_t i = 1; i < stock->close.size(); ++i) {
        if (stock->close[i - 1] > 0.0)
            returns.push_back((stock->close[i] - stock->close[i - 1]) / stock->close[i - 1]);
        else
            returns.push_back(0.0);
    }
    return returns;
}

static std::vector<std::string> buildDates(const std::shared_ptr<StockInfo>& stock) {
    std::vector<std::string> dates;
    if (!stock || stock->timestamps.size() < 2)
        return dates;
    dates.reserve(stock->timestamps.size() - 1);
    for (size_t i = 1; i < stock->timestamps.size(); ++i) {
        time_t     t  = static_cast<time_t>(stock->timestamps[i]);
        struct tm* tm = gmtime(&t);
        char       buf[11];
        strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
        dates.emplace_back(buf);
    }
    return dates;
}

static std::shared_ptr<StockInfo> trimStock(const std::shared_ptr<StockInfo>& full, const std::string& start,
                                            const std::string& end) {
    if (!full)
        return nullptr;
    auto result    = std::make_shared<StockInfo>();
    result->ticker = full->ticker;
    for (size_t i = 0; i < full->timestamps.size(); ++i) {
        time_t     t  = static_cast<time_t>(full->timestamps[i]);
        struct tm* tm = gmtime(&t);
        char       buf[11];
        strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
        std::string d(buf);
        if (d >= start && d <= end) {
            result->timestamps.push_back(full->timestamps[i]);
            result->close.push_back(full->close[i]);
        }
    }
    return result;
}

/* ---- Data types ---- */

struct SweepCell {
    double cagr   = 0.0;
    double sharpe = 0.0;
    double mdd    = 0.0;
};

struct PortfolioRank {
    std::string name;
    std::string bestStrategy;
    double      bestScore = 0.0;
    double      avgCagr   = 0.0;
    double      avgSharpe = 0.0;
    double      worstMdd  = 0.0;
};

int main(int argc, char* argv[]) {
    const char* apiKey = std::getenv("FRED_API_KEY");
    if (!apiKey || std::string(apiKey).empty()) {
        std::cerr << "Error: FRED_API_KEY environment variable is not set." << std::endl;
        return 1;
    }

    std::string sweepPath = util::resolveFromExe("config/macro_sweep.json");
    if (argc > 1)
        sweepPath = argv[1];

    /* ---- Load sweep config ---- */
    const auto sweepCfgOpt = util::loadJsonConfig(sweepPath);
    if (!sweepCfgOpt) {
        return 1;
    }
    const auto& sweepCfg = *sweepCfgOpt;

    // Parse portfolios
    struct PortfolioEntry {
        std::string                        name;
        std::map<std::string, std::string> tickers;  // asset_key -> ticker
    };
    std::vector<PortfolioEntry> portfolios;
    for (const auto& p : sweepCfg["portfolios"]) {
        PortfolioEntry entry;
        entry.name = p["name"].get<std::string>();
        for (const auto& [k, v] : p["tickers"].items()) {
            entry.tickers[k] = v.get<std::string>();
        }
        portfolios.push_back(entry);
    }

    // Parse strategies
    struct StrategyEntry {
        std::string    name;
        nlohmann::json config;
    };
    std::vector<StrategyEntry> strategies;
    for (const auto& s : sweepCfg["strategies"]) {
        StrategyEntry entry;
        entry.name             = s["name"].get<std::string>();
        std::string cfgPath    = util::resolveFromExe(s["config"].get<std::string>());
        const auto  strategyCfg = util::loadJsonConfig(cfgPath);
        if (!strategyCfg) {
            return 1;
        }
        entry.config = *strategyCfg;
        strategies.push_back(entry);
    }

    // Parse periods
    struct PeriodEntry {
        std::string name, start, end;
    };
    std::vector<PeriodEntry> periods;
    for (const auto& p : sweepCfg["periods"]) {
        periods.push_back({p["name"], p["start"], p["end"]});
    }

    std::string frequency = sweepCfg.value("rebalance_frequency", "m");
    double      capital   = sweepCfg.value("initial_capital", 10000.0);
    std::string benchmark = sweepCfg.value("benchmark", "SPY");
    double      cagrW     = sweepCfg.value("/ranking/cagr_weight"_json_pointer, 0.35);
    double      sharpeW   = sweepCfg.value("/ranking/sharpe_weight"_json_pointer, 0.35);
    double      mddW      = sweepCfg.value("/ranking/mdd_weight"_json_pointer, 0.30);

    /* ---- Find widest date range ---- */
    std::string globalStart = periods[0].start;
    std::string globalEnd   = periods[0].end;
    for (const auto& p : periods) {
        if (p.start < globalStart)
            globalStart = p.start;
        if (p.end > globalEnd)
            globalEnd = p.end;
    }
    std::string warmupDate = computeWarmupDate(globalStart, 3);

    yFinance::init();
    util::Defer _cleanup([] { yFinance::close(); });

    /* ---- Fetch FRED data (once) ---- */
    const std::vector<std::string> fredIds = {"UNRATE", "PAYEMS", "INDPRO",   "CPIAUCSL", "CPILFESL", "PCEPI",
                                              "M2REAL", "WM2NS",  "FEDFUNDS", "UMCSENT",  "T10Y2Y",   "BAMLH0A0HYM2"};

    std::cerr << "Fetching FRED data (" << warmupDate << " ~ " << globalEnd << ")..." << std::endl;
    std::map<std::string, std::shared_ptr<FredSeriesInfo>> fredDataFull;
    for (const auto& id : fredIds) {
        auto result = yFinance::getFredSeries(id, apiKey, warmupDate, globalEnd, "m");
        if (result && !result->values.empty()) {
            fredDataFull[id] = result;
            std::cerr << "  [OK] " << id << " (" << result->values.size() << " obs)" << std::endl;
        } else {
            std::cerr << "  [WARN] " << id << " - no data" << std::endl;
        }
    }

    /* ---- Fetch all unique tickers across portfolios (once) ---- */
    std::cerr << "Fetching asset prices..." << std::endl;
    std::set<std::string> allTickers;
    for (const auto& pf : portfolios)
        for (const auto& [k, t] : pf.tickers)
            allTickers.insert(t);
    allTickers.insert(benchmark);

    std::map<std::string, std::shared_ptr<StockInfo>> priceCache;
    for (const auto& ticker : allTickers) {
        auto stock = yFinance::getStockInfo(ticker, globalStart, globalEnd, "1mo");
        if (stock && !stock->close.empty()) {
            priceCache[ticker] = stock;
            std::cerr << "  [OK] " << ticker << " (" << stock->close.size() << " months)" << std::endl;
        } else {
            std::cerr << "  [WARN] " << ticker << " - no data" << std::endl;
        }
    }

    if (priceCache.find(benchmark) == priceCache.end()) {
        std::cerr << "Error: Benchmark " << benchmark << " not available." << std::endl;
        return 1;
    }

    /* ---- Run 3D sweep: Portfolio × Strategy × Period ---- */
    std::cerr << "Running 3D sweep (" << portfolios.size() << " portfolios × " << strategies.size() << " strategies × "
              << periods.size() << " periods)..." << std::endl;

    // results[portfolioIdx][strategyIdx][periodIdx]
    using Grid3D = std::vector<std::vector<std::vector<SweepCell>>>;
    Grid3D results(portfolios.size(),
                   std::vector<std::vector<SweepCell>>(strategies.size(), std::vector<SweepCell>(periods.size())));

    // benchmarkResults[periodIdx] — same across all portfolios
    std::vector<SweepCell> benchResults(periods.size());
    bool                   benchComputed = false;

    for (size_t pfi = 0; pfi < portfolios.size(); ++pfi) {
        const auto& pf = portfolios[pfi];
        std::cerr << "  Portfolio: " << pf.name << std::endl;

        for (size_t pi = 0; pi < periods.size(); ++pi) {
            const auto& period = periods[pi];

            // Trim prices for this period
            auto   benchStock   = trimStock(priceCache[benchmark], period.start, period.end);
            auto   dates        = buildDates(benchStock);
            auto   benchReturns = buildMonthlyReturns(benchStock);
            size_t months       = dates.size();
            if (months < 2)
                continue;

            // Benchmark (compute once)
            if (!benchComputed) {
                auto br                 = MacroBacktester::computeBenchmark(benchReturns, dates, benchmark, capital);
                benchResults[pi].cagr   = br.cagr;
                benchResults[pi].sharpe = br.sharpeRatio;
                benchResults[pi].mdd    = br.maxDrawdownPct;
            }

            // Build asset returns for this portfolio + period
            std::map<std::string, std::vector<double>> assetReturns;
            for (const auto& [key, ticker] : pf.tickers) {
                if (priceCache.find(ticker) == priceCache.end())
                    continue;
                auto trimmed = trimStock(priceCache[ticker], period.start, period.end);
                auto returns = buildMonthlyReturns(trimmed);
                if (returns.size() > months) {
                    returns = std::vector<double>(returns.end() - months, returns.end());
                } else if (returns.size() < months) {
                    std::vector<double> padded(months - returns.size(), 0.0);
                    padded.insert(padded.end(), returns.begin(), returns.end());
                    returns = padded;
                }
                assetReturns[key] = returns;
            }

            // Run each strategy
            for (size_t si = 0; si < strategies.size(); ++si) {
                auto r =
                    MacroBacktester::run(strategies[si].config, fredDataFull, assetReturns, dates, frequency, capital);
                results[pfi][si][pi].cagr   = r.cagr;
                results[pfi][si][pi].sharpe = r.sharpeRatio;
                results[pfi][si][pi].mdd    = r.maxDrawdownPct;
            }
        }
        benchComputed = true;
    }

    /* =============== OUTPUT =============== */

    const int nameW   = 14;
    const int metricW = 8;
    const int periodW = metricW * 3 + 2;

    // Helper to print one strategy × period matrix
    auto printMatrix = [&](const std::vector<std::vector<SweepCell>>& grid) {
        // Header: period names
        std::clog << std::left << std::setw(nameW) << "Strategy";
        for (const auto& p : periods) {
            int pad = (periodW - static_cast<int>(p.name.size())) / 2;
            std::clog << std::string(std::max(pad, 1), ' ') << p.name
                      << std::string(std::max(periodW - pad - static_cast<int>(p.name.size()), 0), ' ');
        }
        std::clog << std::endl;

        // Sub-header
        std::clog << std::left << std::setw(nameW) << "";
        for (size_t pi = 0; pi < periods.size(); ++pi) {
            std::clog << std::right << std::setw(metricW) << "CAGR" << std::setw(metricW) << "Sharpe"
                      << std::setw(metricW) << "MaxDD" << "  ";
        }
        std::clog << std::endl;

        int totalW = nameW + static_cast<int>(periods.size()) * (periodW + 2);
        std::clog << std::string(totalW, '-') << std::endl;

        auto printRow = [&](const std::string& label, const std::vector<SweepCell>& cells) {
            std::clog << std::left << std::setw(nameW) << label;
            for (const auto& c : cells) {
                std::clog << std::right << std::fixed << std::setprecision(1) << std::setw(metricW - 1) << c.cagr << "%"
                          << std::setprecision(2) << std::setw(metricW) << c.sharpe << std::setprecision(1)
                          << std::setw(metricW - 1) << c.mdd << "%" << "  ";
            }
            std::clog << std::endl;
        };

        for (size_t si = 0; si < strategies.size(); ++si) {
            printRow(strategies[si].name, grid[si]);
        }
        std::clog << std::string(totalW, '-') << std::endl;
        printRow(benchmark + " (B&H)", benchResults);
    };

    // Print per-portfolio detail tables
    for (size_t pfi = 0; pfi < portfolios.size(); ++pfi) {
        std::clog << std::endl;
        std::clog << "=== Portfolio: " << portfolios[pfi].name << " ===" << std::endl;

        // Show tickers
        std::clog << "    Tickers:";
        for (const auto& [k, t] : portfolios[pfi].tickers) {
            std::clog << " " << k << "=" << t;
        }
        std::clog << std::endl << std::endl;

        printMatrix(results[pfi]);
    }

    /* ---- Compute per-portfolio ranking ---- */
    // For each portfolio: for each strategy, compute aggregated score across all periods.
    // Portfolio's best = best strategy score. Also track avg stats.

    std::vector<PortfolioRank> ranks(portfolios.size());

    for (size_t pfi = 0; pfi < portfolios.size(); ++pfi) {
        ranks[pfi].name     = portfolios[pfi].name;
        ranks[pfi].worstMdd = 0.0;

        double      bestStrategyScore = -1e18;
        std::string bestStratName;

        for (size_t si = 0; si < strategies.size(); ++si) {
            // Compute this strategy's score across all periods for this portfolio
            double stratScore   = 0.0;
            double sumCagr      = 0.0;
            double sumSharpe    = 0.0;
            double stratWorstDD = 0.0;

            for (size_t pi = 0; pi < periods.size(); ++pi) {
                const auto& cell = results[pfi][si][pi];
                sumCagr += cell.cagr;
                sumSharpe += cell.sharpe;
                stratWorstDD = std::min(stratWorstDD, cell.mdd);
            }

            // Normalize across periods (simple: use raw averages for scoring)
            double avgC = sumCagr / static_cast<double>(periods.size());
            double avgS = sumSharpe / static_cast<double>(periods.size());

            // Score: higher cagr & sharpe is better, less negative mdd is better
            stratScore = avgC * cagrW + avgS * 100.0 * sharpeW + (100.0 + stratWorstDD) * mddW;

            if (stratScore > bestStrategyScore) {
                bestStrategyScore    = stratScore;
                bestStratName        = strategies[si].name;
                ranks[pfi].avgCagr   = avgC;
                ranks[pfi].avgSharpe = avgS;
                ranks[pfi].worstMdd  = stratWorstDD;
            }
        }

        ranks[pfi].bestStrategy = bestStratName;
        ranks[pfi].bestScore    = bestStrategyScore;
    }

    // Sort by score descending
    std::sort(ranks.begin(), ranks.end(),
              [](const PortfolioRank& a, const PortfolioRank& b) { return a.bestScore > b.bestScore; });

    /* ---- Print ranking ---- */
    std::clog << std::endl;
    std::clog << "=== Portfolio Ranking ===" << std::endl;
    std::clog << std::endl;

    std::clog << std::left << std::setw(6) << "Rank" << std::setw(nameW) << "Portfolio" << std::setw(nameW)
              << "Best Strategy" << std::right << std::setw(12) << "Avg CAGR" << std::setw(12) << "Avg Sharpe"
              << std::setw(12) << "Worst MDD" << std::setw(10) << "Score" << std::endl;
    std::clog << std::string(80, '-') << std::endl;

    for (size_t i = 0; i < ranks.size(); ++i) {
        const auto& r = ranks[i];
        std::clog << std::left << std::setw(6) << ("#" + std::to_string(i + 1)) << std::setw(nameW) << r.name
                  << std::setw(nameW) << r.bestStrategy << std::right << std::fixed << std::setprecision(1)
                  << std::setw(11) << r.avgCagr << "%" << std::setprecision(2) << std::setw(12) << r.avgSharpe
                  << std::setprecision(1) << std::setw(11) << r.worstMdd << "%" << std::setprecision(1) << std::setw(10)
                  << r.bestScore << std::endl;
    }

    std::clog << std::endl;
    return 0;
}
