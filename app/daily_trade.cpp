#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "data/kis_provider.hpp"
#include "strategy/strategy_factory.hpp"
#include "trade/signal_executor.hpp"

namespace {

/** KST wall-clock date as "YYYY-MM-DD" (UTC+9 shift then gmtime — no TZ database needed). */
std::string kstDate(int daysAgo = 0) {
    const std::time_t  kst = std::time(nullptr) + 9 * 3600 - static_cast<std::time_t>(daysAgo) * 86400;
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&kst), "%Y-%m-%d");
    return oss.str();
}

/**
 * @brief Calendar date of a daily bar, as "YYYY-MM-DD".
 *
 * KisProvider stamps each daily bar at 09:00 UTC on its own date, so the date is
 * read back with gmtime — formatting it in local time would shift the day on a
 * machine west of UTC-9.
 */
std::string barDate(int64_t ts) {
    const std::time_t  t = static_cast<std::time_t>(ts);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&t), "%Y-%m-%d");
    return oss.str();
}

bool isKrxMarketOpen() {
    const std::time_t kst   = std::time(nullptr) + 9 * 3600;
    const std::tm*    tmPtr = std::gmtime(&kst);
    if (tmPtr->tm_wday == 0 || tmPtr->tm_wday == 6) {
        return false;
    }
    const int hm = tmPtr->tm_hour * 100 + tmPtr->tm_min;
    return hm >= 900 && hm <= 1530;
}

std::string signalName(Signal s) {
    return s == Signal::BUY ? "BUY" : (s == Signal::SELL ? "SELL" : "HOLD");
}

void printUsage() {
    std::cout << "Usage:\n"
              << "  daily_trade --id <portfolio-id> [--live] [--force] [--lookback-days <n>] [--config <path>]\n"
              << "  daily_trade --all              [--live] [--force] [--lookback-days <n>] [--config <path>]\n\n"
              << "  Evaluates a daily-bar strategy once against live KIS data and places at most\n"
              << "  one order per profile. Meant to be run near the close (e.g. 15:15 KST) by cron,\n"
              << "  not as a loop — for minute-bar scalping use scalp_trade instead.\n\n"
              << "  --all      every KRX profile except category 'scalp' (those belong to scalp_trade)\n"
              << "  --live     actually place orders; without it, decisions are logged and journaled only\n"
              << "  --force    skip the KRX market-hours guard (dry-run inspection outside trading hours)\n";
}

/**
 * @brief Run one profile end to end: fetch bars, evaluate, execute.
 * @return true if the profile was evaluated (not that an order was placed).
 */
bool runProfile(const StrategyProfile& profile, KisProvider& kis, bool live, int lookbackDays) {
    std::cout << "\n----------------------------------------------------------------------------------------\n";
    std::cout << " #" << profile.id << " " << profile.name << " (" << profile.ticker
              << ")  position=" << (profile.positionPct * 100.0) << "%  stop-loss=" << profile.stopLossPct << "%\n";

    auto strat = profile.createStrategy();
    if (!strat) {
        std::cerr << "[-] Failed to create strategy instance.\n";
        return false;
    }

    auto data = kis.getStockInfo(profile.ticker, kstDate(lookbackDays), kstDate(0));
    if (!data || data->close.empty()) {
        std::cerr << "[-] No daily data fetched for " << profile.ticker << ".\n";
        return false;
    }
    if (data->close.size() <= strat->warmupPeriod()) {
        std::cerr << "[-] Only " << data->close.size() << " bars, strategy needs " << (strat->warmupPeriod() + 1)
                  << ". Increase --lookback-days.\n";
        return false;
    }

    // Index convention: evaluate(i) decides the order executed at bar i using closes
    // through i-1. So the index to evaluate is the position of the bar we are about
    // to trade at. If today's bar is already in the series it is that bar (size-1);
    // if KIS has not published it yet, it is the one after the last (size).
    const std::size_t lastIdx   = data->close.size() - 1;
    const bool        hasToday  = !data->timestamps.empty() && barDate(data->timestamps[lastIdx]) == kstDate(0);
    const std::size_t evalIdx   = hasToday ? lastIdx : data->close.size();
    const double      lastClose = data->close[lastIdx];

    strat->init(*data);
    const Signal signal = strat->evaluate(*data, evalIdx);

    std::cout << " bars=" << data->close.size() << " last=" << barDate(data->timestamps[lastIdx])
              << " close=" << std::fixed << std::setprecision(0) << lastClose
              << (hasToday ? " (today, still forming)" : " (today not published yet)")
              << "  signal=" << signalName(signal) << "\n";

    trade::SignalExecutor executor(profile, live, 1);  // one order per run, per profile
    const auto            decision = executor.execute(signal, lastClose, KisTrader::getBalance());

    std::cout << " holding=" << decision.heldQty;
    if (decision.heldQty > 0) {
        std::cout << " avgPrice=" << decision.heldAvgPrice;
    }
    std::cout << "\n";

    if (!decision.acted) {
        std::cout << " -> no action\n";
    } else if (!decision.sent) {
        std::cout << " -> [DRY-RUN] would " << decision.side << " x" << decision.quantity << " (" << decision.reason
                  << ")\n";
    } else if (decision.order.success) {
        std::cout << " -> [ORDER] " << decision.side << " x" << decision.quantity << " (" << decision.reason
                  << ") No: " << decision.order.orderNo << "\n";
    } else {
        std::cout << " -> [ORDER FAILED] " << decision.side << " x" << decision.quantity << ": "
                  << decision.order.message << "\n";
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string configPath   = "config/portfolio.json";
    int         targetId     = -1;
    bool        runAll       = false;
    bool        live         = false;
    bool        force        = false;
    int         lookbackDays = 400;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--id" && i + 1 < argc) {
            targetId = std::stoi(argv[++i]);
        } else if (arg == "--all") {
            runAll = true;
        } else if (arg == "--live") {
            live = true;
        } else if (arg == "--force") {
            force = true;
        } else if (arg == "--lookback-days" && i + 1 < argc) {
            lookbackDays = std::stoi(argv[++i]);
        } else if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else {
            printUsage();
            return arg == "--help" || arg == "-h" ? 0 : 1;
        }
    }

    if (targetId == -1 && !runAll) {
        printUsage();
        return 1;
    }

    const auto config = PortfolioConfig::loadFromFile(configPath);

    std::vector<const StrategyProfile*> targets;
    if (runAll) {
        for (const auto& p : config.getProfiles()) {
            // Scalping profiles are driven by scalp_trade on minute bars; running
            // them here too would have two loops fighting over the same position.
            if (p.market == "KRX" && p.category != "scalp") {
                targets.push_back(&p);
            }
        }
    } else {
        const auto* profile = config.findById(targetId);
        if (!profile) {
            std::cerr << "[-] Strategy with ID " << targetId << " not found in " << configPath << std::endl;
            return 1;
        }
        if (profile->market != "KRX") {
            std::cerr << "[-] daily_trade only supports KRX profiles (order execution is KRX-only); " << profile->name
                      << " is market=" << profile->market << std::endl;
            return 1;
        }
        targets.push_back(profile);
    }

    if (targets.empty()) {
        std::cerr << "[-] No KRX profiles to run." << std::endl;
        return 1;
    }

    std::cout << "========================================================================================\n";
    std::cout << " Daily Trade  mode=" << (live ? "LIVE" : "DRY-RUN") << "  profiles=" << targets.size()
              << "  lookback=" << lookbackDays << "d\n";
    std::cout << "========================================================================================\n";

    if (!isKrxMarketOpen()) {
        if (!force) {
            std::cout << "[*] KRX is closed (09:00-15:30 KST, weekdays). Nothing to do — pass --force to\n"
                      << "    evaluate anyway (orders placed outside hours would be rejected by KIS).\n";
            return 0;
        }
        std::cout << "[!] KRX is closed; --force given, evaluating anyway.\n";
    }
    if (!live) {
        std::cout << "[*] Dry-run: no real orders will be placed. Pass --live to trade for real.\n";
    }

    KisProvider kis;
    int         evaluated = 0;
    for (const auto* p : targets) {
        if (runProfile(*p, kis, live, lookbackDays)) {
            ++evaluated;
        }
    }

    std::cout << "\n[*] Evaluated " << evaluated << "/" << targets.size() << " profile(s).\n";
    return evaluated == 0 ? 1 : 0;
}
