#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "common/util.hpp"
#include "data/bar_recorder.hpp"
#include "strategy/strategy_factory.hpp"
#include "yfinance.hpp"

/**
 * Seeds the intraday archive from Yahoo, which keeps a short window of history
 * KIS does not serve at all: roughly 5 days at 1-minute and a month at 5-minute.
 * Running this once gives the archive a head start instead of starting empty;
 * from then on scalp_trade extends it a day at a time.
 */
namespace {

void printUsage() {
    std::cout << "Usage:\n"
              << "  bar_collect [--interval 1m|5m|15m|30m|1h] [--range 5d|1mo|3mo] [--ticker <code>]\n"
              << "              [--config <path>]\n\n"
              << "  Seeds data/bars/ from Yahoo Finance. Without --ticker, every KRX ticker in the\n"
              << "  portfolio is collected.\n\n"
              << "  Yahoo's limits, measured: 1m keeps ~5 days, 5m/15m/30m ~1 month, 1h ~1 year.\n"
              << "  Asking for more returns nothing, so --range must match --interval.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string configPath = "config/portfolio.json";
    std::string interval   = "1m";
    std::string range      = "5d";
    std::string oneTicker;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--interval" && i + 1 < argc) {
            interval = argv[++i];
        } else if (arg == "--range" && i + 1 < argc) {
            range = argv[++i];
        } else if (arg == "--ticker" && i + 1 < argc) {
            oneTicker = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else {
            printUsage();
            return (arg == "--help" || arg == "-h") ? 0 : 1;
        }
    }

    std::vector<std::string> tickers;
    if (!oneTicker.empty()) {
        tickers.push_back(oneTicker);
    } else {
        const auto config = PortfolioConfig::loadFromFile(configPath);
        for (const auto& p : config.getProfiles()) {
            if (p.market == "KRX" && std::find(tickers.begin(), tickers.end(), p.ticker) == tickers.end()) {
                tickers.push_back(p.ticker);
            }
        }
    }
    if (tickers.empty()) {
        std::cerr << "[-] No tickers to collect." << std::endl;
        return 1;
    }

    yFinance::init();
    util::Defer cleanup([] { yFinance::close(); });

    data::BarRecorder recorder;
    std::cout << "[*] Archive: " << recorder.root() << "\n";
    std::cout << "[*] interval=" << interval << " range=" << range << " tickers=" << tickers.size() << "\n\n";

    int total = 0;
    for (const auto& ticker : tickers) {
        // Yahoo needs the exchange suffix for KRX listings; KIS does not use one.
        const auto data = yFinance::getStockInfo(ticker + ".KS", interval, range);
        if (!data || data->close.empty()) {
            std::cout << "  " << ticker << ": no data (Yahoo may not serve this range for " << interval << ")\n";
            continue;
        }
        const int added = recorder.record(ticker, *data, interval);
        if (added < 0) {
            std::cerr << "  " << ticker << ": FAILED to write archive\n";
            continue;
        }
        total += added;
        std::cout << "  " << ticker << ": " << data->close.size() << " bars fetched, " << added << " new\n";
    }

    std::cout << "\n[+] " << total << " new bar(s) archived.\n";
    return 0;
}
