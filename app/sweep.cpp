#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "backtest/backtest_engine.hpp"
#include "common/util.hpp"
#include "data/kis_provider.hpp"
#include "strategy/strategy_catalog.hpp"
#include "strategy/strategy_factory.hpp"

/**
 * Cross-applies every strategy to every ticker in a universe, then ranks the
 * strategies by whether they *keep* working rather than by how well the best
 * single combination did.
 *
 * That distinction is the whole point. Testing 16 strategies against 30 tickers
 * produces 480 results, and the best of 480 is mostly luck — the same reason the
 * best of 480 coin-flippers looks like a genius. So this selects on an early
 * period and reports an untouched later one, and ranks on the median and hit
 * rate across tickers instead of the maximum.
 */
namespace {

struct Cell {
    std::string ticker;
    std::string name;
    double      isReturn   = 0.0;
    double      oosReturn  = 0.0;
    double      oosBuyHold = 0.0;
    double      maxDD      = 0.0;
    double      bhMaxDD    = 0.0;
    std::size_t trades     = 0;
    bool        valid      = false;
};

struct Candidate {
    std::string    label;
    std::string    type;
    nlohmann::json params;
};

/**
 * @brief Strategies to test, from the catalog rather than from this file.
 *
 * These were hardcoded here, which meant the parameters a sweep reported on and
 * the parameters the trader ran were two separate copies that nothing kept in
 * step. Both now read config/strategies.json.
 *
 * @param gridFor Empty for every concrete definition; otherwise the grid for
 *                that strategy type.
 */
std::vector<Candidate> candidatesFrom(const StrategyCatalog& catalog, const std::string& gridFor) {
    std::vector<Candidate> out;
    for (const auto& d : gridFor.empty() ? catalog.concrete() : catalog.gridFor(gridFor)) {
        out.push_back({d.id, d.type, d.params});
    }
    return out;
}

double median(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const std::size_t mid = v.size() / 2;
    return (v.size() % 2 == 0) ? (v[mid - 1] + v[mid]) / 2.0 : v[mid];
}

/** Slice a series to bars at or after a date, keeping the engine's assumptions intact. */
StockInfo sliceFrom(const StockInfo& src, int64_t fromTs) {
    StockInfo out;
    out.ticker   = src.ticker;
    out.currency = src.currency;
    for (std::size_t i = 0; i < src.timestamps.size(); ++i) {
        if (src.timestamps[i] < fromTs) {
            continue;
        }
        out.timestamps.push_back(src.timestamps[i]);
        out.open.push_back(src.open[i]);
        out.high.push_back(src.high[i]);
        out.low.push_back(src.low[i]);
        out.close.push_back(src.close[i]);
        out.volume.push_back(src.volume[i]);
    }
    return out;
}

StockInfo sliceUntil(const StockInfo& src, int64_t untilTs) {
    StockInfo out;
    out.ticker   = src.ticker;
    out.currency = src.currency;
    for (std::size_t i = 0; i < src.timestamps.size(); ++i) {
        if (src.timestamps[i] >= untilTs) {
            break;
        }
        out.timestamps.push_back(src.timestamps[i]);
        out.open.push_back(src.open[i]);
        out.high.push_back(src.high[i]);
        out.low.push_back(src.low[i]);
        out.close.push_back(src.close[i]);
        out.volume.push_back(src.volume[i]);
    }
    return out;
}

int64_t parseDate(const std::string& yyyymmdd) {
    std::tm tm = {};
    tm.tm_year = std::stoi(yyyymmdd.substr(0, 4)) - 1900;
    tm.tm_mon  = std::stoi(yyyymmdd.substr(5, 2)) - 1;
    tm.tm_mday = std::stoi(yyyymmdd.substr(8, 2));
    tm.tm_hour = 9;
    return timegm(&tm);
}

/* ---- Disk cache: a full sweep refetches ~30 tickers, which is slow and rude ---- */

/**
 * One cache file per ticker, holding the widest range ever fetched.
 *
 * Keying the cache by test window would mean refetching 30 tickers whenever the
 * window changes — which is most of what this tool is for. Fetch wide once,
 * slice in memory.
 */
std::string cachePath(const std::string& ticker) {
    return util::resolveFromExe("cache/daily/" + ticker + ".csv");
}

std::shared_ptr<StockInfo> loadCache(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return nullptr;
    }
    auto        data = std::make_shared<StockInfo>();
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string        cell;
        try {
            std::getline(ss, cell, ',');
            data->timestamps.push_back(std::stoll(cell));
            std::getline(ss, cell, ',');
            data->open.push_back(std::stod(cell));
            std::getline(ss, cell, ',');
            data->high.push_back(std::stod(cell));
            std::getline(ss, cell, ',');
            data->low.push_back(std::stod(cell));
            std::getline(ss, cell, ',');
            data->close.push_back(std::stod(cell));
            std::getline(ss, cell, ',');
            data->volume.push_back(std::stoll(cell));
        } catch (const std::exception&) {
            return nullptr;  // partial write; refetch rather than trust it
        }
    }
    return data->close.empty() ? nullptr : data;
}

void saveCache(const std::string& path, const StockInfo& data) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    out << "timestamp,open,high,low,close,volume\n" << std::fixed << std::setprecision(4);
    for (std::size_t i = 0; i < data.close.size(); ++i) {
        out << data.timestamps[i] << "," << data.open[i] << "," << data.high[i] << "," << data.low[i] << ","
            << data.close[i] << "," << data.volume[i] << "\n";
    }
}

void printUsage() {
    std::cout << "Usage:\n"
              << "  sweep [--universe <path>] [--start YYYY-MM-DD] [--split YYYY-MM-DD] [--end YYYY-MM-DD]\n"
              << "        [--min-trades <n>] [--position <0..1>] [--refetch]\n\n"
              << "  Runs every strategy against every ticker, selects on the period before --split\n"
              << "  and reports the period after it untouched.\n\n"
              << "  --min-trades  ignore combinations with fewer trades than this (default 8) —\n"
              << "                a strategy that barely traded cannot be evaluated either way\n"
              << "  --fetch-start how far back to fetch and cache (default 2019-01-01). The test\n"
              << "                window is sliced from this, so widening it only costs one fetch\n"
              << "  --strategy    sweep one strategy type's parameter grid from the catalog,\n"
              << "                instead of every concrete strategy in it\n"
              << "  --catalog     strategy definitions (default: config/strategies.json)\n"
              << "  --refetch     ignore the cached price data under cache/daily/\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string universePath = "config/universe.json";
    std::string startDate    = "2021-01-01";
    std::string splitDate    = "2025-01-01";
    std::string endDate      = "2026-09-18";
    std::size_t minTrades    = 8;
    double      positionPct  = 1.0;
    bool        refetch      = false;
    std::string fetchStart   = "2019-01-01";  // cached once; the test window is sliced from it
    std::string gridFor;                      // when set, sweep this strategy type's parameter grid
    std::string catalogPath;                  // empty resolves to config/strategies.json

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--universe" && i + 1 < argc)
            universePath = argv[++i];
        else if (arg == "--start" && i + 1 < argc)
            startDate = argv[++i];
        else if (arg == "--split" && i + 1 < argc)
            splitDate = argv[++i];
        else if (arg == "--end" && i + 1 < argc)
            endDate = argv[++i];
        else if (arg == "--min-trades" && i + 1 < argc)
            minTrades = std::stoul(argv[++i]);
        else if (arg == "--position" && i + 1 < argc)
            positionPct = std::stod(argv[++i]);
        else if (arg == "--fetch-start" && i + 1 < argc)
            fetchStart = argv[++i];
        else if (arg == "--strategy" && i + 1 < argc)
            gridFor = argv[++i];
        else if (arg == "--catalog" && i + 1 < argc)
            catalogPath = argv[++i];
        else if (arg == "--refetch")
            refetch = true;
        else {
            printUsage();
            return (arg == "--help" || arg == "-h") ? 0 : 1;
        }
    }

    const auto universe = util::loadJsonConfig(universePath);
    if (!universe || !universe->contains("tickers")) {
        std::cerr << "[-] No tickers in " << universePath << std::endl;
        return 1;
    }

    const auto catalog = StrategyCatalog::loadFromFile(catalogPath);
    if (!catalog.loaded()) {
        std::cerr << "[-] No strategies loaded from " << catalog.path() << std::endl;
        return 1;
    }

    const int64_t splitTs = parseDate(splitDate);
    const auto    cands   = candidatesFrom(catalog, gridFor);
    if (cands.empty()) {
        std::cerr << "[-] No " << (gridFor.empty() ? "strategies" : "grid for '" + gridFor + "'") << " in "
                  << catalog.path() << std::endl;
        return 1;
    }

    std::cout << "========================================================================================\n";
    std::cout << " Strategy x ticker sweep\n";
    std::cout << " in-sample  " << startDate << " ~ " << splitDate << "   (selection)\n";
    std::cout << " out-sample " << splitDate << " ~ " << endDate << "   (verification, untouched)\n";
    std::cout << " min trades " << minTrades << "   position " << (positionPct * 100) << "%\n";
    if (!gridFor.empty()) {
        std::cout << " parameter grid for " << gridFor << " (" << cands.size() << " combinations)\n";
    }
    std::cout << "========================================================================================\n";

    KisProvider                              kis;
    std::map<std::string, std::vector<Cell>> byStrategy;
    int                                      usable = 0;

    for (const auto& t : (*universe)["tickers"]) {
        const std::string code = t.value("code", "");
        const std::string name = t.value("name", code);
        if (code.empty())
            continue;

        const std::string cp     = cachePath(code);
        auto              cached = refetch ? nullptr : loadCache(cp);

        // Only refetch when the cache does not already cover what was asked for.
        const bool covers = cached && !cached->timestamps.empty()
                         && cached->timestamps.front() <= parseDate(fetchStart) + 7 * 86400
                         && cached->timestamps.back() >= parseDate(endDate) - 7 * 86400;
        auto data = covers ? cached : nullptr;
        if (!data) {
            data = kis.getStockInfo(code, fetchStart, endDate);
            if (data && !data->close.empty())
                saveCache(cp, *data);
        }
        if (!data || data->close.size() < 300) {
            std::cout << "  skip " << code << " " << name << " (insufficient data)\n";
            continue;
        }

        // The cache may hold far more than the test window; slice to it.
        const StockInfo window = sliceUntil(sliceFrom(*data, parseDate(startDate)), parseDate(endDate) + 86400);
        if (window.close.size() < 250) {
            std::cout << "  skip " << code << " " << name << " (only " << window.close.size() << " bars in window)\n";
            continue;
        }
        ++usable;
        std::cout << "  " << code << " " << name << "  " << window.close.size() << " bars\n" << std::flush;

        const StockInfo is  = sliceUntil(window, splitTs);
        const StockInfo oos = sliceFrom(window, splitTs);
        if (is.close.size() < 200 || oos.close.size() < 60)
            continue;

        // Buy-and-hold over the same out-of-sample window: the bar any strategy has
        // to clear to be worth its complexity. Its drawdown is measured too —
        // claiming a strategy "reduces risk" is meaningless without the comparison.
        const double bh   = (oos.close.back() - oos.close.front()) / oos.close.front() * 100.0;
        double       bhDD = 0.0;
        {
            double peak = oos.close.front();
            for (const double px : oos.close) {
                peak = std::max(peak, px);
                bhDD = std::min(bhDD, (px - peak) / peak * 100.0);
            }
        }

        for (const auto& c : cands) {
            StrategyProfile p;
            p.type    = c.type;
            p.params  = c.params;
            p.ticker  = code;
            p.market  = "KRX";
            auto sIs  = p.createStrategy();
            auto sOos = p.createStrategy();
            if (!sIs || !sOos)
                continue;

            auto cfg        = BacktestConfig::forMarket("KRX");
            cfg.positionPct = positionPct;

            BacktestEngine engine(10000000.0);
            const auto     rIs  = engine.run(*sIs, is, cfg);
            const auto     rOos = engine.run(*sOos, oos, cfg);

            Cell cell;
            cell.ticker     = code;
            cell.name       = name;
            cell.isReturn   = rIs.totalReturnPct;
            cell.oosReturn  = rOos.totalReturnPct;
            cell.oosBuyHold = bh;
            cell.maxDD      = rOos.maxDrawdownPct;
            cell.bhMaxDD    = bhDD;
            cell.trades     = rIs.trades.size() + rOos.trades.size();
            cell.valid      = cell.trades >= minTrades;
            byStrategy[c.label].push_back(cell);
        }
    }

    std::cout << "\n" << usable << " ticker(s) usable.\n";

    struct Row {
        std::string label;
        double      isMedian = 0, oosMedian = 0, bhMedian = 0, ddMedian = 0, bhDdMedian = 0;
        double      hitRate = 0, beatBhRate = 0;
        int         n = 0;
    };
    std::vector<Row> rows;

    for (const auto& c : cands) {
        const auto&         cells = byStrategy[c.label];
        std::vector<double> isR, oosR, bh, dd, bhDd;
        int                 hits = 0, beats = 0, n = 0;
        for (const auto& cell : cells) {
            if (!cell.valid)
                continue;
            ++n;
            isR.push_back(cell.isReturn);
            oosR.push_back(cell.oosReturn);
            bh.push_back(cell.oosBuyHold);
            dd.push_back(cell.maxDD);
            bhDd.push_back(cell.bhMaxDD);
            if (cell.oosReturn > 0)
                ++hits;
            if (cell.oosReturn > cell.oosBuyHold)
                ++beats;
        }
        if (n == 0)
            continue;
        Row r;
        r.label      = c.label;
        r.isMedian   = median(isR);
        r.oosMedian  = median(oosR);
        r.bhMedian   = median(bh);
        r.ddMedian   = median(dd);
        r.bhDdMedian = median(bhDd);
        r.hitRate    = 100.0 * hits / n;
        r.beatBhRate = 100.0 * beats / n;
        r.n          = n;
        rows.push_back(r);
    }

    // Ranked by out-of-sample median: the period that had no say in the selection.
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.oosMedian > b.oosMedian; });

    std::cout << "\n=== Ranked by OUT-OF-SAMPLE median (the period selection never saw) ===\n\n";
    std::cout << std::left << std::setw(20) << "strategy" << std::right << std::setw(7) << "n" << std::setw(11)
              << "IS med%" << std::setw(12) << "OOS med%" << std::setw(11) << "B&H med%" << std::setw(10) << "win%"
              << std::setw(11) << ">B&H%" << std::setw(10) << "MDD%" << std::setw(11) << "B&H MDD%" << "\n";
    std::cout << std::string(102, '-') << "\n";
    for (const auto& r : rows) {
        std::cout << std::left << std::setw(20) << r.label << std::right << std::fixed << std::setprecision(1)
                  << std::setw(7) << r.n << std::setprecision(2) << std::setw(11) << r.isMedian << std::setw(12)
                  << r.oosMedian << std::setw(11) << r.bhMedian << std::setprecision(1) << std::setw(10) << r.hitRate
                  << std::setw(11) << r.beatBhRate << std::setprecision(2) << std::setw(10) << r.ddMedian
                  << std::setw(11) << r.bhDdMedian << "\n";
    }

    std::cout << "\n  n       tickers where the strategy traded at least " << minTrades << " times\n"
              << "  IS med% median return over the selection period\n"
              << "  OOS med% median return over the held-out period — the number that matters\n"
              << "  win%    share of tickers profitable out-of-sample\n"
              << "  >B&H%   share of tickers where it beat buy-and-hold; below 50 means the\n"
              << "          strategy is worse than doing nothing on most of the universe\n"
              << "  MDD vs B&H MDD  whether the strategy actually bought safety with the return\n"
              << "          it gave up\n";

    std::cout << "\n  [!] Every ticker here is currently listed. Companies that delisted are absent,\n"
              << "      so these numbers are an upper bound on what was actually achievable.\n";
    return 0;
}
