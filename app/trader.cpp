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
#include "common/process_lock.hpp"
#include "common/run_log.hpp"
#include "common/util.hpp"
#include "data/bar_recorder.hpp"
#include "data/kis_provider.hpp"
#include "data/krx_calendar.hpp"
#include "notify/telegram.hpp"
#include "strategy/strategy_factory.hpp"
#include "trade/schedule_state.hpp"
#include "trade/signal_executor.hpp"

/**
 * The single trading process.
 *
 * Whether a strategy trades on minute bars or daily ones is a property of the
 * strategy, not a reason to run a second service — so there is one trader, and
 * the config decides what it trades.
 *
 * It runs in either shape, chosen by the unit file rather than the code: --once
 * for a systemd timer, which is right while only once-a-day profiles are enabled,
 * and no flag for a continuous loop, which minute-bar polling requires. Either
 * way a profile is evaluated when the process next looks rather than at one exact
 * minute, because the schedule is recorded rather than assumed.
 *
 * Only one of these may run at a time: they share the risk guard, position store
 * and journal, and a second instance would erase the first's writes.
 */
namespace {

volatile std::sig_atomic_t g_stop = 0;

void onStop(int) {
    g_stop = 1;
}

/** Sleep that wakes on a stop signal, so SIGTERM does not wait out an interval. */
void interruptibleSleep(int seconds) {
    for (int elapsed = 0; elapsed < seconds && !g_stop; ++elapsed) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

/** Current KST time as {weekday(0=Sun), HHMM}; UTC+9 shift then gmtime, no TZ database. */
std::pair<int, int> kstNow() {
    const std::time_t kst   = std::time(nullptr) + 9 * 3600;
    const std::tm*    tmPtr = std::gmtime(&kst);
    return {tmPtr->tm_wday, tmPtr->tm_hour * 100 + tmPtr->tm_min};
}

std::string kstDate(int daysAgo = 0) {
    const std::time_t  kst = std::time(nullptr) + 9 * 3600 - static_cast<std::time_t>(daysAgo) * 86400;
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&kst), "%Y-%m-%d");
    return oss.str();
}

std::string nowLabel() {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << kstNow().second;
    return oss.str();
}

/** @return empty if KRX is open right now, otherwise why it is not. */
std::string krxClosedReason(const data::KrxCalendar& calendar) {
    if (const std::string why = calendar.closedReason(kstDate()); !why.empty()) {
        return why;
    }
    const int hm = kstNow().second;
    return (hm >= 900 && hm <= 1530) ? "" : "outside 09:00-15:30 KST";
}

std::string signalName(Signal s) {
    return s == Signal::BUY ? "BUY" : (s == Signal::SELL ? "SELL" : "HOLD");
}

/** Calendar date of a daily bar; KisProvider stamps them at 09:00 UTC on their own date. */
std::string barDate(int64_t ts) {
    const std::time_t  t = static_cast<std::time_t>(ts);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&t), "%Y-%m-%d");
    return oss.str();
}

/**
 * @brief One profile's live state.
 *
 * The profile is held by pointer because SignalExecutor keeps a reference to it
 * and this vector reallocates as it grows.
 */
struct Runner {
    std::shared_ptr<StrategyProfile>       profile;
    std::unique_ptr<IStrategy>             strategy;
    std::unique_ptr<trade::SignalExecutor> executor;

    /** Minute-bar strategies evaluate every cycle; the rest run once a day. */
    [[nodiscard]] bool isIntraday() const { return profile->category == "scalp"; }
};

std::vector<Runner> buildRunners(const PortfolioConfig& config, bool live, int maxTrades,
                                 const trade::ExecutionContext& ctx) {
    std::vector<Runner> runners;
    for (const auto& p : config.getProfiles()) {
        if (!p.enabled) {
            continue;
        }
        if (p.market != "KRX") {
            std::cerr << "[-] #" << p.id << " " << p.name << " is market=" << p.market
                      << "; order execution is KRX-only. Skipping.\n";
            continue;
        }
        auto strat = p.createStrategy();
        if (!strat) {
            std::cerr << "[-] #" << p.id << " " << p.name << ": failed to create strategy. Skipping.\n";
            continue;
        }
        Runner r;
        // A copy, not a pointer into the config: the config is replaced wholesale on
        // reload, which would leave every executor pointing at freed data.
        r.profile  = std::make_shared<StrategyProfile>(p);
        r.strategy = std::move(strat);
        r.executor = std::make_unique<trade::SignalExecutor>(*r.profile, live, maxTrades, ctx);
        runners.push_back(std::move(r));
    }
    return runners;
}

void printUsage() {
    std::cout << "Usage:\n"
              << "  trader [--live] [--interval <seconds>] [--daily-at <HHMM>] [--max-trades <n>]\n"
              << "         [--config <path>] [--quiet] [--once] [--force]\n\n"
              << "  Runs every enabled KRX profile in config/live.json. Minute-bar profiles\n"
              << "  (category 'scalp') are evaluated each --interval; the rest run once a day at\n"
              << "  or after --daily-at, whenever the loop next looks.\n\n"
              << "  --live        actually place orders; without it, decisions are logged only\n"
              << "  --interval    seconds between cycles (default 60)\n"
              << "  --daily-at    earliest KST time for once-a-day profiles (default 1515)\n"
              << "  --max-trades  per-profile cap on orders sent this session (default 10)\n"
              << "  --once        run one cycle and exit, still honouring market hours and the\n"
              << "                once-a-day schedule. This is what a systemd timer runs.\n"
              << "  --force       ignore market hours and the schedule, for inspection. Implies --once\n"
              << "  --quiet       suppress the Telegram summary\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // line-buffer stdout even when piped

    std::string configPath   = "config/live.json";
    int         intervalSec  = 60;
    int         dailyAtHhmm  = 1515;
    int         maxTrades    = 10;
    bool        live         = false;
    bool        quiet        = false;
    bool        once         = false;
    bool        force        = false;
    int         lookbackDays = 400;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--live") {
            live = true;
        } else if (arg == "--interval" && i + 1 < argc) {
            intervalSec = std::stoi(argv[++i]);
        } else if (arg == "--daily-at" && i + 1 < argc) {
            dailyAtHhmm = std::stoi(argv[++i]);
        } else if (arg == "--max-trades" && i + 1 < argc) {
            maxTrades = std::stoi(argv[++i]);
        } else if (arg == "--lookback-days" && i + 1 < argc) {
            lookbackDays = std::stoi(argv[++i]);
        } else if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--once") {
            once = true;
        } else if (arg == "--force") {
            force = true;
            once  = true;  // inspecting is inherently a single pass
        } else {
            printUsage();
            return (arg == "--help" || arg == "-h") ? 0 : 1;
        }
    }

    // Only one trading process may run: they share mutable state on disk.
    const util::ProcessLock lock("trading");
    if (!lock.held()) {
        std::cerr << "[-] Another kairos trader is already running.\n"
                  << "    They share the risk guard, position store and journal, so running both\n"
                  << "    would lose orders from the day's count and could commit the cash twice.\n"
                  << "    Lock: " << lock.path() << std::endl;
        return 1;
    }

    const util::RunLog runLog("trader");

    auto config  = PortfolioConfig::loadFromFile(configPath);
    auto ctx     = trade::ExecutionContext::create(config.getRiskLimits());
    auto runners = buildRunners(config, live, maxTrades, ctx);
    if (runners.empty()) {
        std::cerr << "[-] No enabled KRX profiles in " << configPath << std::endl;
        return 1;
    }

    const auto& limits = config.getRiskLimits();
    std::cout << "========================================================================================\n";
    std::cout << " kairos trader  mode=" << (live ? "LIVE" : "DRY-RUN") << "  interval=" << intervalSec << "s"
              << "  daily-at=" << std::setfill('0') << std::setw(4) << dailyAtHhmm << std::setfill(' ') << "\n";
    std::cout << " Risk  daily-loss-limit=" << limits.dailyLossLimitPct
              << "%  max-orders/day=" << limits.maxOrdersPerDay << "  per-profile/session=" << maxTrades
              << "  alerts=" << (notify::Telegram().enabled() ? "on" : "off") << "\n";
    for (const auto& r : runners) {
        std::cout << "   #" << r.profile->id << " " << r.profile->name << " (" << r.profile->ticker << ")" << "  "
                  << (r.isIntraday() ? "intraday" : "daily") << "  pos=" << (r.profile->positionPct * 100.0)
                  << "%  stop=" << r.profile->stopLossPct << "%";
        if (r.profile->takeProfitPct > 0.0) {
            std::cout << "  tp=" << r.profile->takeProfitPct << "%";
        }
        if (r.profile->trailingStopPct > 0.0) {
            std::cout << "  trail=" << r.profile->trailingStopPct << "%";
        }
        std::cout << "\n";
    }
    std::cout << "========================================================================================\n";
    if (!live) {
        std::cout << "[*] Dry-run: no real orders will be placed. Pass --live to trade for real.\n";
    }
    std::cout << "[*] Journal: " << ctx.journal->path() << "\n";

    std::signal(SIGINT, onStop);
    std::signal(SIGTERM, onStop);  // what systemd sends to stop a service

    const data::KrxCalendar calendar;
    if (!calendar.loaded()) {
        std::cout << "[!] No holiday list loaded; only weekends are treated as closed.\n";
    }

    KisProvider          provider;
    data::BarRecorder    recorder;
    trade::ScheduleState schedule;
    int                  barsRecorded = 0;
    std::string          summarySentFor;  // KST date the daily summary was last sent for

    while (!g_stop) {
        // A config edit takes effect on the next cycle. Rebuilding also resets
        // strategy state, which is correct: the parameters that produced it are gone.
        if (config.sourceChanged()) {
            auto reloaded = PortfolioConfig::loadFromFile(configPath);
            auto rebuilt =
                reloaded.getProfiles().empty() ? std::vector<Runner>{} : buildRunners(reloaded, live, maxTrades, ctx);
            config = std::move(reloaded);  // adopt the mtime either way, so we do not retry every cycle
            if (rebuilt.empty()) {
                std::cout << "[!] Config changed but yielded no runnable profiles — keeping the previous set.\n";
            } else {
                runners = std::move(rebuilt);
                ctx.risk->setLimits(config.getRiskLimits());
                std::cout << "[" << nowLabel() << "] Config reloaded (" << runners.size() << " profile(s)).\n";
            }
        }

        // --once still honours market hours: a timer firing at 15:15 on a holiday must
        // do nothing, not act on stale prices. Only --force overrides that.
        const std::string closed = krxClosedReason(calendar);
        if (!closed.empty() && !force) {
            std::cout << "[" << nowLabel() << "] Market closed (" << closed << ").";
            if (once) {
                std::cout << " Nothing to do.\n";
                return 0;
            }
            std::cout << " Waiting...\n";
            interruptibleSleep(intervalSec);
            continue;
        }

        // One balance per cycle, shared by every profile: it describes the account,
        // not a strategy, and re-fetching per profile would burn the rate limit.
        const auto balance = KisTrader::getBalance();
        if (!balance.success) {
            std::cout << "[" << nowLabel() << "] Balance fetch failed: " << balance.message << ". Waiting...\n";
            if (once) {
                return 1;
            }
            interruptibleSleep(intervalSec);
            continue;
        }
        ctx.risk->observe(balance);

        const std::string        today   = kstDate();
        const int                nowHhmm = kstNow().second;
        std::vector<std::string> notable;
        bool                     ranDaily = false;

        for (auto& r : runners) {
            const auto& p = *r.profile;

            const bool due = r.isIntraday() || force
                          || trade::isDailyProfileDue(schedule.lastEvaluated(p.id), today, nowHhmm, dailyAtHhmm);
            if (!due) {
                continue;
            }

            std::shared_ptr<StockInfo> data;
            std::size_t                evalIdx = 0;

            if (r.isIntraday()) {
                data = provider.getIntradayBars(p.ticker);
                if (!data || data->close.empty()) {
                    std::cout << "[" << nowLabel() << "] #" << p.id << " no intraday data yet.\n";
                    continue;
                }
                // Minute history cannot be fetched back later, so keep what we saw.
                if (const int added = recorder.record(p.ticker, *data, "1m"); added > 0) {
                    barsRecorded += added;
                }
                // The last bar is the minute still forming, so it is "now".
                evalIdx = data->close.size() - 1;
            } else {
                data = provider.getStockInfo(p.ticker, kstDate(lookbackDays), today);
                if (!data || data->close.empty()) {
                    std::cerr << "[" << nowLabel() << "] #" << p.id << " no daily data for " << p.ticker << ".\n";
                    notable.push_back("#" + std::to_string(p.id) + " " + p.ticker + " 데이터 없음");
                    continue;
                }
                // Archive the bars this decision was made on: KIS re-serves revised
                // history, so the inputs would otherwise be unrecoverable.
                recorder.recordSeries(p.ticker, "daily", *data);
                // The index to evaluate is the bar the order fills at: today's if KIS
                // has published it, otherwise the one past the last.
                const std::size_t lastIdx = data->close.size() - 1;
                evalIdx = (barDate(data->timestamps[lastIdx]) == today) ? lastIdx : data->close.size();
            }

            if (data->close.size() <= r.strategy->warmupPeriod()) {
                std::cout << "[" << nowLabel() << "] #" << p.id << " only " << data->close.size() << " bars, need "
                          << (r.strategy->warmupPeriod() + 1) << ".\n";
                continue;
            }

            r.strategy->init(*data);
            const Signal signal = r.strategy->evaluate(*data, evalIdx);
            const double price  = data->close.back();

            const auto decision = r.executor->execute(signal, price, balance);

            std::cout << "[" << nowLabel() << "] #" << p.id << " " << p.ticker << " price=" << std::fixed
                      << std::setprecision(0) << price << " signal=" << signalName(signal)
                      << " holding=" << decision.heldQty;
            if (decision.heldQty > 0) {
                std::cout << " avg=" << decision.heldAvgPrice << " peak=" << decision.peakPrice;
            }
            std::cout << std::endl;

            const std::string tag = "#" + std::to_string(p.id) + " " + p.ticker + " ";
            if (decision.acted && decision.skipped) {
                std::cout << "  [BLOCKED] " << decision.blockedBy << " (" << decision.reason << ")\n";
                notable.push_back(tag + "차단됨 — " + decision.blockedBy);
            } else if (decision.skipped) {
                std::cout << "  [BLOCKED] " << decision.blockedBy << "\n";
            } else if (decision.acted && !decision.sent) {
                std::cout << "  [DRY-RUN] would " << decision.side << " x" << decision.quantity << " ("
                          << decision.reason << ")\n";
                notable.push_back(tag + "모의 " + decision.side + " x" + std::to_string(decision.quantity));
            } else if (decision.sent && decision.order.success) {
                std::cout << "  [ORDER] " << decision.side << " x" << decision.quantity << " (" << decision.reason
                          << ") -> No: " << decision.order.orderNo << "\n";
                notable.push_back(tag + decision.side + " x" + std::to_string(decision.quantity) + " 주문");
            } else if (decision.sent) {
                std::cout << "  [ORDER FAILED] " << decision.side << " x" << decision.quantity << ": "
                          << decision.order.message << "\n";
                notable.push_back(tag + decision.side + " 주문 실패 — " + decision.order.message);
            }

            if (!r.isIntraday() && !force) {
                // Recorded even under --once, so a timer firing twice, or a retry after
                // a restart, does not evaluate the same profile again today.
                schedule.markEvaluated(p.id, today);
                ranDaily = true;
            }
        }

        // One summary per day, after the once-a-day pass, whether or not anything
        // happened: silence must not mean both "held correctly" and "never ran".
        if (ranDaily && !quiet && summarySentFor != today) {
            const notify::Telegram telegram;
            if (telegram.enabled()) {
                std::ostringstream msg;
                msg << "kairos " << today << " " << nowLabel() << " — " << (live ? "실전" : "모의") << "\n"
                    << "자산 " << std::fixed << std::setprecision(0) << balance.totalEvalAmount << "원";
                if (notable.empty()) {
                    msg << "\n신호 없음";
                } else {
                    for (const auto& line : notable) {
                        msg << "\n" << line;
                    }
                }
                if (!telegram.send(msg.str())) {
                    std::cerr << "[!] Daily summary could not be delivered to Telegram.\n";
                }
            }
            summarySentFor = today;
        }

        if (once) {
            break;
        }
        interruptibleSleep(intervalSec);
    }

    int total = 0;
    for (const auto& r : runners) {
        total += r.executor->ordersSent();
    }
    std::cout << "\n[*] Stopped. " << total << " order(s) attempted this session, " << barsRecorded
              << " bar(s) archived.\n";
    return 0;
}
