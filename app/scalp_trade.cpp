#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "broker/kis_auth.hpp"
#include "broker/kis_trader.hpp"
#include "common/util.hpp"
#include "data/kis_provider.hpp"
#include "strategy/strategy_factory.hpp"
#include "trade/signal_executor.hpp"

namespace {

volatile std::sig_atomic_t g_stop = 0;

void onSigint(int) {
    g_stop = 1;
}

/**
 * @brief Current KST wall-clock time, as {weekday(0=Sun), HHMM}.
 * Uses the same UTC+9h-shift-then-gmtime trick as KisProvider (no external TZ DB).
 */
std::pair<int, int> kstNow() {
    const std::time_t kst   = std::time(nullptr) + 9 * 3600;
    const std::tm*    tmPtr = std::gmtime(&kst);
    return {tmPtr->tm_wday, tmPtr->tm_hour * 100 + tmPtr->tm_min};
}

bool isKrxMarketOpen() {
    const auto [wday, hm] = kstNow();
    if (wday == 0 || wday == 6) {
        return false;
    }
    return hm >= 900 && hm <= 1530;
}

std::string nowLabel() {
    const auto [wday, hm] = kstNow();
    (void)wday;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << hm;
    return oss.str();
}

void printUsage() {
    std::cout << "Usage:\n"
              << "  scalp_trade --id <portfolio-id> [--interval <seconds>] [--live] [--max-trades <n>]\n"
              << "              [--config <path>]\n\n"
              << "  Polls KIS intraday minute bars for a KRX portfolio profile and evaluates its\n"
              << "  strategy signal every --interval seconds during market hours (09:00-15:30 KST).\n"
              << "  Without --live, orders are only logged (dry-run) — nothing is actually placed.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // line-buffer stdout even when piped/redirected

    std::string configPath  = "config/portfolio.json";
    int         targetId    = -1;
    int         intervalSec = 60;
    bool        live        = false;
    int         maxTrades   = 10;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--id" && i + 1 < argc) {
            targetId = std::stoi(argv[++i]);
        } else if (arg == "--interval" && i + 1 < argc) {
            intervalSec = std::stoi(argv[++i]);
        } else if (arg == "--live") {
            live = true;
        } else if (arg == "--max-trades" && i + 1 < argc) {
            maxTrades = std::stoi(argv[++i]);
        } else if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
    }

    if (targetId == -1) {
        printUsage();
        return 1;
    }

    auto        config  = PortfolioConfig::loadFromFile(configPath);
    const auto* profile = config.findById(targetId);
    if (!profile) {
        std::cerr << "[-] Strategy with ID " << targetId << " not found in " << configPath << std::endl;
        return 1;
    }
    if (profile->market != "KRX") {
        std::cerr << "[-] scalp_trade only supports KRX profiles (live order execution is KRX-only); " << profile->name
                  << " is market=" << profile->market << std::endl;
        return 1;
    }

    auto strat = profile->createStrategy();
    if (!strat) {
        std::cerr << "[-] Failed to create strategy instance for " << profile->name << std::endl;
        return 1;
    }

    std::cout << "========================================================================================\n";
    std::cout << " Scalp Trade: #" << profile->id << " " << profile->name << " (" << profile->ticker << ")\n";
    std::cout << " interval=" << intervalSec << "s  mode=" << (live ? "LIVE" : "DRY-RUN")
              << "  max-trades=" << maxTrades << "  stop-loss=" << profile->stopLossPct << "%\n";
    std::cout << "========================================================================================\n";
    if (!live) {
        std::cout << "[*] Dry-run mode: no real orders will be placed. Pass --live to trade for real.\n";
    }

    std::signal(SIGINT, onSigint);

    trade::SignalExecutor executor(*profile, live, maxTrades);
    std::cout << "[*] Trade journal: " << executor.journal().path() << "\n";

    KisProvider provider;

    while (!g_stop) {
        if (!isKrxMarketOpen()) {
            std::cout << "[" << nowLabel() << "] Market closed (KRX hours: 09:00-15:30 KST, weekdays). Waiting...\n";
            std::this_thread::sleep_for(std::chrono::seconds(intervalSec));
            continue;
        }

        auto data = provider.getIntradayBars(profile->ticker);
        if (!data || data->close.empty()) {
            std::cout << "[" << nowLabel() << "] No intraday data yet. Waiting...\n";
            std::this_thread::sleep_for(std::chrono::seconds(intervalSec));
            continue;
        }
        if (data->close.size() <= strat->warmupPeriod()) {
            std::cout << "[" << nowLabel() << "] Only " << data->close.size() << " bars so far, need "
                      << (strat->warmupPeriod() + 1) << ". Waiting...\n";
            std::this_thread::sleep_for(std::chrono::seconds(intervalSec));
            continue;
        }

        strat->init(*data);
        // Index convention: evaluate(i) decides the order executed at bar i using
        // closes through i-1. The last bar is the minute still forming, so that
        // bar is "now" — exactly what the backtest does, with no look-ahead.
        const Signal signal       = strat->evaluate(*data, data->close.size() - 1);
        const double currentPrice = data->close.back();

        const auto decision = executor.execute(signal, currentPrice, KisTrader::getBalance());

        std::cout << "[" << nowLabel() << "] price=" << currentPrice << " signal="
                  << (signal == Signal::BUY    ? "BUY"
                      : signal == Signal::SELL ? "SELL"
                                               : "HOLD")
                  << " holding=" << decision.heldQty;
        if (decision.heldQty > 0) {
            std::cout << " avgPrice=" << decision.heldAvgPrice;
        }
        std::cout << std::endl;

        if (decision.acted) {
            if (decision.skipped) {
                std::cout << "  [SKIPPED] max-trades (" << maxTrades << ") reached this session (" << decision.reason
                          << ")\n";
            } else if (!decision.sent) {
                std::cout << "  [DRY-RUN] would " << decision.side << " x" << decision.quantity << " ("
                          << decision.reason << ")\n";
            } else if (decision.order.success) {
                std::cout << "  [ORDER] " << decision.side << " x" << decision.quantity << " (" << decision.reason
                          << ") -> No: " << decision.order.orderNo << "\n";
            } else {
                std::cout << "  [ORDER FAILED] " << decision.side << " x" << decision.quantity << ": "
                          << decision.order.message << "\n";
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(intervalSec));
    }

    std::cout << "\n[*] Stopped (Ctrl+C). " << executor.ordersSent() << " order(s) attempted this session.\n";
    return 0;
}
