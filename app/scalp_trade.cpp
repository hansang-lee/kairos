#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "broker/kis_trader.hpp"
#include "data/bar_recorder.hpp"
#include "data/kis_provider.hpp"
#include "data/krx_calendar.hpp"
#include "notify/telegram.hpp"
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

/** KST calendar date, "YYYY-MM-DD". */
std::string kstToday() {
    const std::time_t  kst = std::time(nullptr) + 9 * 3600;
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&kst), "%Y-%m-%d");
    return oss.str();
}

/** @return empty if KRX is open right now, otherwise why it is not. */
std::string krxClosedReason(const data::KrxCalendar& calendar) {
    if (const std::string why = calendar.closedReason(kstToday()); !why.empty()) {
        return why;
    }
    const auto [wday, hm] = kstNow();
    (void)wday;
    return (hm >= 900 && hm <= 1530) ? "" : "outside 09:00-15:30 KST";
}

std::string nowLabel() {
    const auto [wday, hm] = kstNow();
    (void)wday;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << hm;
    return oss.str();
}

std::string signalName(Signal s) {
    return s == Signal::BUY ? "BUY" : (s == Signal::SELL ? "SELL" : "HOLD");
}

/**
 * @brief One profile's live state: its strategy instance and its executor.
 *
 * The profile is held by pointer because SignalExecutor keeps a reference to it,
 * and a vector of runners reallocates as it grows — a by-value profile would
 * leave that reference dangling.
 */
struct Runner {
    std::shared_ptr<StrategyProfile>       profile;
    std::unique_ptr<IStrategy>             strategy;
    std::unique_ptr<trade::SignalExecutor> executor;
};

/** Parse "--id 18" or "--id 18,19,20" into ids. */
void appendIds(const std::string& arg, std::vector<int>& out) {
    std::stringstream ss(arg);
    std::string       item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            out.push_back(std::stoi(item));
        }
    }
}

/**
 * @brief Build a runner per requested profile, sharing one execution context.
 *
 * The context must be shared: a per-runner risk guard or position store would
 * overwrite the others' file on every save.
 */
std::vector<Runner> buildRunners(const PortfolioConfig& config, const std::vector<int>& ids, bool allScalp, bool live,
                                 int maxTrades, const trade::ExecutionContext& ctx) {
    std::vector<Runner> runners;

    std::vector<const StrategyProfile*> selected;
    if (allScalp) {
        for (const auto& p : config.getProfiles()) {
            if (p.market == "KRX" && p.category == "scalp" && p.enabled) {
                selected.push_back(&p);
            }
        }
    }
    for (const int id : ids) {
        const auto* p = config.findById(id);
        if (!p) {
            std::cerr << "[-] Strategy with ID " << id << " not found; skipping.\n";
            continue;
        }
        if (std::find(selected.begin(), selected.end(), p) == selected.end()) {
            selected.push_back(p);
        }
    }

    for (const auto* p : selected) {
        if (p->market != "KRX") {
            std::cerr << "[-] #" << p->id << " " << p->name << " is market=" << p->market
                      << "; order execution is KRX-only. Skipping.\n";
            continue;
        }
        auto strat = p->createStrategy();
        if (!strat) {
            std::cerr << "[-] #" << p->id << " " << p->name << ": failed to create strategy. Skipping.\n";
            continue;
        }
        Runner r;
        // A copy, not a pointer into the config: the config is replaced wholesale on
        // reload, which would otherwise leave every executor pointing at freed data.
        r.profile  = std::make_shared<StrategyProfile>(*p);
        r.strategy = std::move(strat);
        r.executor = std::make_unique<trade::SignalExecutor>(*r.profile, live, maxTrades, ctx);
        runners.push_back(std::move(r));
    }
    return runners;
}

void printUsage() {
    std::cout << "Usage:\n"
              << "  scalp_trade --id <id>[,<id>...] [--all-scalp] [--interval <seconds>] [--live]\n"
              << "              [--max-trades <n>] [--config <path>]\n\n"
              << "  Polls KIS intraday minute bars for one or more KRX profiles and evaluates each\n"
              << "  strategy every --interval seconds during market hours (09:00-15:30 KST).\n"
              << "  Without --live, orders are only logged (dry-run) — nothing is actually placed.\n\n"
              << "  --all-scalp   run every KRX profile with category 'scalp'\n"
              << "  --max-trades  per-profile cap on orders sent this session (default 10)\n\n"
              << "  The config file is re-read when it changes on disk, so parameters can be\n"
              << "  adjusted without restarting.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // line-buffer stdout even when piped/redirected

    std::string      configPath = "config/portfolio.json";
    std::vector<int> ids;
    bool             allScalp    = false;
    int              intervalSec = 60;
    bool             live        = false;
    int              maxTrades   = 10;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--id" && i + 1 < argc) {
            appendIds(argv[++i], ids);
        } else if (arg == "--all-scalp") {
            allScalp = true;
        } else if (arg == "--interval" && i + 1 < argc) {
            intervalSec = std::stoi(argv[++i]);
        } else if (arg == "--live") {
            live = true;
        } else if (arg == "--max-trades" && i + 1 < argc) {
            maxTrades = std::stoi(argv[++i]);
        } else if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else {
            printUsage();
            return (arg == "--help" || arg == "-h") ? 0 : 1;
        }
    }

    if (ids.empty() && !allScalp) {
        printUsage();
        return 1;
    }

    auto config  = PortfolioConfig::loadFromFile(configPath);
    auto ctx     = trade::ExecutionContext::create(config.getRiskLimits());
    auto runners = buildRunners(config, ids, allScalp, live, maxTrades, ctx);
    if (runners.empty()) {
        std::cerr << "[-] No runnable profiles." << std::endl;
        return 1;
    }

    std::cout << "========================================================================================\n";
    std::cout << " Scalp Trade  mode=" << (live ? "LIVE" : "DRY-RUN") << "  interval=" << intervalSec << "s"
              << "  max-trades=" << maxTrades << "/profile"
              << "  alerts=" << (notify::Telegram().enabled() ? "on" : "off") << "\n";
    for (const auto& r : runners) {
        std::cout << "   #" << r.profile->id << " " << r.profile->name << " (" << r.profile->ticker << ")"
                  << "  pos=" << (r.profile->positionPct * 100.0) << "%" << "  stop=" << r.profile->stopLossPct << "%";
        if (r.profile->takeProfitPct > 0.0) {
            std::cout << "  tp=" << r.profile->takeProfitPct << "%";
        }
        if (r.profile->trailingStopPct > 0.0) {
            std::cout << "  trail=" << r.profile->trailingStopPct << "%";
        }
        if (r.profile->cooldownMinutes > 0) {
            std::cout << "  cooldown=" << r.profile->cooldownMinutes << "m";
        }
        std::cout << "\n";
    }
    std::cout << "========================================================================================\n";
    if (!live) {
        std::cout << "[*] Dry-run mode: no real orders will be placed. Pass --live to trade for real.\n";
    }
    std::cout << "[*] Trade journal: " << ctx.journal->path() << "\n";

    std::signal(SIGINT, onSigint);

    const data::KrxCalendar calendar;
    if (!calendar.loaded()) {
        std::cout << "[!] No holiday list loaded; only weekends are treated as closed.\n";
    }

    KisProvider       provider;
    data::BarRecorder recorder;
    int               barsRecorded = 0;
    std::cout << "[*] Bar archive: " << recorder.root() << "\n";

    while (!g_stop) {
        // An edit to the config takes effect on the next cycle. Rebuilding the
        // runners also resets strategy state, which is correct: the parameters that
        // produced it are gone.
        if (config.sourceChanged()) {
            auto reloaded = PortfolioConfig::loadFromFile(configPath);
            if (reloaded.getProfiles().empty()) {
                std::cout << "[!] Config changed but reloaded empty — keeping the previous one.\n";
                config = std::move(reloaded);  // adopt the new mtime so we do not retry every cycle
            } else {
                config       = std::move(reloaded);
                auto rebuilt = buildRunners(config, ids, allScalp, live, maxTrades, ctx);
                if (rebuilt.empty()) {
                    std::cout << "[!] Config changed but no runnable profiles — keeping the previous ones.\n";
                } else {
                    runners = std::move(rebuilt);
                    ctx.risk->setLimits(config.getRiskLimits());
                    std::cout << "[" << nowLabel() << "] Config reloaded (" << runners.size() << " profile(s)).\n";
                }
            }
        }

        if (const std::string closed = krxClosedReason(calendar); !closed.empty()) {
            std::cout << "[" << nowLabel() << "] Market closed (" << closed << "). Waiting...\n";
            std::this_thread::sleep_for(std::chrono::seconds(intervalSec));
            continue;
        }

        // One balance fetch per cycle, shared by every profile: it describes the
        // account, not a strategy, and re-fetching it per profile would burn the
        // rate limit for nothing.
        const auto balance = KisTrader::getBalance();
        if (!balance.success) {
            std::cout << "[" << nowLabel() << "] Balance fetch failed: " << balance.message << ". Waiting...\n";
            std::this_thread::sleep_for(std::chrono::seconds(intervalSec));
            continue;
        }

        for (auto& r : runners) {
            auto data = provider.getIntradayBars(r.profile->ticker);
            if (!data || data->close.empty()) {
                std::cout << "[" << nowLabel() << "] #" << r.profile->id << " no intraday data yet.\n";
                continue;
            }
            if (data->close.size() <= r.strategy->warmupPeriod()) {
                std::cout << "[" << nowLabel() << "] #" << r.profile->id << " only " << data->close.size()
                          << " bars, need " << (r.strategy->warmupPeriod() + 1) << ".\n";
                continue;
            }

            // Minute history cannot be fetched back later, so keep what we just saw.
            // Runs before the signal so a strategy error still leaves the data behind.
            if (const int added = recorder.record(r.profile->ticker, *data); added > 0) {
                barsRecorded += added;  // record() returns -1 on write failure
            }

            r.strategy->init(*data);
            // Index convention: evaluate(i) decides the order executed at bar i using
            // closes through i-1. The last bar is the minute still forming, so that
            // bar is "now" — exactly what the backtest does, with no look-ahead.
            const Signal signal       = r.strategy->evaluate(*data, data->close.size() - 1);
            const double currentPrice = data->close.back();

            const auto decision = r.executor->execute(signal, currentPrice, balance);

            std::cout << "[" << nowLabel() << "] #" << r.profile->id << " " << r.profile->ticker
                      << " price=" << currentPrice << " signal=" << signalName(signal)
                      << " holding=" << decision.heldQty;
            if (decision.heldQty > 0) {
                std::cout << " avg=" << decision.heldAvgPrice << " peak=" << decision.peakPrice;
            }
            std::cout << std::endl;

            if (decision.acted) {
                if (decision.skipped) {
                    std::cout << "  [BLOCKED] "
                              << (decision.blockedBy.empty() ? "max-trades reached" : decision.blockedBy) << " ("
                              << decision.reason << ")\n";
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
        }

        std::this_thread::sleep_for(std::chrono::seconds(intervalSec));
    }

    int total = 0;
    for (const auto& r : runners) {
        total += r.executor->ordersSent();
    }
    std::cout << "\n[*] Stopped (Ctrl+C). " << total << " order(s) attempted this session, " << barsRecorded
              << " bar(s) archived.\n";
    return 0;
}
