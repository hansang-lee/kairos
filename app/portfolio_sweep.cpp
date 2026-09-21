#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "common/util.hpp"
#include "data/kis_provider.hpp"
#include "portfolio/portfolio_engine.hpp"
#include "portfolio/strategies.hpp"

/**
 * Compares strategies that allocate one account across several assets, against
 * equal-weight buy-and-hold of the same assets.
 *
 * Separate from `sweep`, which asks whether a signal works on each ticker
 * independently. The question here is different: whether deciding *how much* of
 * each to hold beats holding all of them.
 */
namespace {

std::shared_ptr<StockInfo> loadCached(const std::string& ticker) {
    auto data    = std::make_shared<StockInfo>();
    data->ticker = ticker;

    std::ifstream in(util::resolveFromExe("cache/daily/" + ticker + ".csv"));
    if (!in.is_open()) {
        return nullptr;
    }
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string        cell;
        double             ohlc[4];
        try {
            if (!std::getline(ss, cell, ','))
                continue;
            const int64_t ts = std::stoll(cell);
            for (double& v : ohlc) {
                if (!std::getline(ss, cell, ','))
                    throw std::runtime_error("short row");
                v = std::stod(cell);
            }
            if (!std::getline(ss, cell, ','))
                continue;
            data->timestamps.push_back(ts);
            data->open.push_back(ohlc[0]);
            data->high.push_back(ohlc[1]);
            data->low.push_back(ohlc[2]);
            data->close.push_back(ohlc[3]);
            data->volume.push_back(std::stoll(cell));
        } catch (const std::exception&) {
            continue;
        }
    }
    return data->close.empty() ? nullptr : data;
}

int64_t parseDate(const std::string& d) {
    std::tm tm = {};
    tm.tm_year = std::stoi(d.substr(0, 4)) - 1900;
    tm.tm_mon  = std::stoi(d.substr(5, 2)) - 1;
    tm.tm_mday = std::stoi(d.substr(8, 2));
    tm.tm_hour = 9;
    return timegm(&tm);
}

std::shared_ptr<StockInfo> sliceTo(const std::shared_ptr<StockInfo>& s, int64_t from, int64_t to) {
    auto out    = std::make_shared<StockInfo>();
    out->ticker = s->ticker;
    for (std::size_t i = 0; i < s->timestamps.size(); ++i) {
        if (s->timestamps[i] < from || s->timestamps[i] >= to)
            continue;
        out->timestamps.push_back(s->timestamps[i]);
        out->open.push_back(s->open[i]);
        out->high.push_back(s->high[i]);
        out->low.push_back(s->low[i]);
        out->close.push_back(s->close[i]);
        out->volume.push_back(s->volume[i]);
    }
    return out;
}

void printRow(const portfolio::PortfolioResult& r) {
    std::cout << std::left << std::setw(34) << r.strategyName << std::right << std::fixed << std::setprecision(1)
              << std::setw(10) << r.totalReturnPct << std::setw(9) << r.cagr << std::setw(10) << r.maxDrawdownPct
              << std::setprecision(2) << std::setw(9)
              << (r.maxDrawdownPct != 0.0 ? r.totalReturnPct / -r.maxDrawdownPct : 0.0) << std::setw(9) << r.sharpeRatio
              << std::setprecision(0) << std::setw(9) << r.orders << std::setw(12) << r.totalCosts << "\n";
}

void printUsage() {
    std::cout << "Usage:\n"
              << "  portfolio_sweep [--universe <path>] [--start YYYY-MM-DD] [--end YYYY-MM-DD]\n"
              << "                  [--rebalance <bars>]\n\n"
              << "  Allocates one account across the universe and compares that against holding\n"
              << "  all of it in equal weight. Prices come from cache/daily/, which `sweep` fills.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string universePath = "config/universe_index.json";
    std::string startDate    = "2015-01-01";
    std::string endDate      = "2026-01-01";
    int         rebalance    = 21;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--universe" && i + 1 < argc)
            universePath = argv[++i];
        else if (arg == "--start" && i + 1 < argc)
            startDate = argv[++i];
        else if (arg == "--end" && i + 1 < argc)
            endDate = argv[++i];
        else if (arg == "--rebalance" && i + 1 < argc)
            rebalance = std::stoi(argv[++i]);
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

    std::vector<std::shared_ptr<StockInfo>> series;
    for (const auto& t : (*universe)["tickers"]) {
        const std::string code = t.value("code", "");
        auto              s    = loadCached(code);
        if (!s) {
            std::cerr << "  " << code << ": not cached; run sweep first to fetch it\n";
            continue;
        }
        auto w = sliceTo(s, parseDate(startDate), parseDate(endDate));
        if (w->close.size() > 300) {
            series.push_back(w);
        }
    }
    if (series.empty()) {
        std::cerr << "[-] No usable price data." << std::endl;
        return 1;
    }

    const auto data = portfolio::PortfolioData::align(series);
    std::cout << "========================================================================================\n";
    std::cout << " Portfolio sweep  " << startDate << " ~ " << endDate << "  " << data.assetCount() << " assets, "
              << data.barCount() << " bars, rebalance every " << rebalance << " bars\n";
    std::cout << "========================================================================================\n\n";

    std::cout << std::left << std::setw(34) << "" << std::right << std::setw(10) << "return%" << std::setw(9) << "CAGR"
              << std::setw(10) << "MDD%" << std::setw(9) << "ret/MDD" << std::setw(9) << "sharpe" << std::setw(9)
              << "orders" << std::setw(12) << "costs" << "\n"
              << std::string(102, '-') << "\n";

    printRow(portfolio::buyAndHold(data));

    portfolio::PortfolioConfigBt cfg;
    cfg.rebalanceEveryBars = rebalance;

    std::vector<std::unique_ptr<portfolio::IPortfolioStrategy>> strategies;
    strategies.push_back(std::make_unique<portfolio::EqualWeight>());
    strategies.push_back(std::make_unique<portfolio::RiskParity>(63, 0.4));
    strategies.push_back(std::make_unique<portfolio::RiskParity>(126, 0.25));
    for (std::size_t top : {1u, 2u, 3u, 5u}) {
        for (std::size_t look : {63u, 126u, 252u}) {
            strategies.push_back(std::make_unique<portfolio::MomentumRotation>(top, look, 0.0));
        }
    }

    portfolio::PortfolioEngine engine(10000000.0);
    for (auto& s : strategies) {
        printRow(engine.run(*s, data, cfg));
    }
    return 0;
}
