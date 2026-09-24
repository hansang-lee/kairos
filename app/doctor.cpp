#include <cstdint>
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

#include "broker/kis_auth.hpp"
#include "broker/kis_trader.hpp"
#include "common/kst_time.hpp"
#include "common/util.hpp"
#include "data/kis_provider.hpp"
#include "data/krx_calendar.hpp"
#include "notify/telegram.hpp"
#include "strategy/strategy_factory.hpp"
#include "trade/position_store.hpp"
#include "trade/risk_guard.hpp"
#include "trade/trade_journal.hpp"

/**
 * Checks everything the trading loops depend on, in the order a failure would
 * bite: credentials, then the account, then market data, then local state.
 * Exists so a broken setup is found by running one command rather than by
 * watching a loop do nothing for an hour.
 */
namespace {

int g_fail = 0;
int g_warn = 0;

void ok(const std::string& what, const std::string& detail = "") {
    std::cout << "  [ OK ] " << what << (detail.empty() ? "" : "  " + detail) << "\n";
}
void warn(const std::string& what, const std::string& detail = "") {
    std::cout << "  [WARN] " << what << (detail.empty() ? "" : "  " + detail) << "\n";
    ++g_warn;
}
void fail(const std::string& what, const std::string& detail = "") {
    std::cout << "  [FAIL] " << what << (detail.empty() ? "" : "  " + detail) << "\n";
    ++g_fail;
}

void section(const std::string& title) {
    std::cout << "\n" << title << "\n" << std::string(title.size(), '-') << "\n";
}

void checkEnvironment() {
    section("Environment");

    std::cout << "  KST now: " << util::kstTimestamp(std::time(nullptr)) << "\n";

    // The timer and every market-hours gate read wall-clock time.
    const char*   tz = std::getenv("TZ");
    std::ifstream tzFile("/etc/timezone");
    std::string   systemTz;
    if (tzFile.is_open()) {
        std::getline(tzFile, systemTz);
    }
    if (systemTz == "Asia/Seoul" || (tz && std::string(tz) == "Asia/Seoul")) {
        ok("System timezone is Asia/Seoul");
    } else {
        warn("System timezone is not Asia/Seoul",
             "found '" + (systemTz.empty() ? "unknown" : systemTz) + "'; systemd OnCalendar times would not be KST");
    }

    if (std::filesystem::exists(".env")) {
        ok(".env found in the working directory");
    } else {
        fail(".env not found in the working directory",
             "credentials are read relative to the cwd — run from the project root");
    }
}

void checkCredentials() {
    section("KIS credentials");

    auto& auth = KisAuth::instance();
    auth.loadFromEnv();

    std::cout << "  mode: " << (auth.isPaper() ? "paper (모의투자)" : "LIVE (실전투자)") << "\n";
    std::cout << "  base: " << auth.getBaseUrl() << "\n";

    if (auth.getAppKey().empty() || auth.getAppSecret().empty()) {
        fail("App key/secret missing", "check KIS_PAPER_APP_KEY / KIS_PAPER_APP_SECRET in .env");
        return;
    }
    ok("App key and secret present");

    if (auth.getAccountNo().empty()) {
        fail("Account number missing", "check KIS_PAPER_ACCOUNT_NO in .env");
        return;
    }
    ok("Account number present", "product code " + auth.getAccountProd());

    if (auth.getAccessToken().empty()) {
        fail("Could not obtain an access token", "the key/secret may be wrong, or KIS may be rejecting them");
    } else {
        ok("Access token obtained");
    }
}

void checkAccount() {
    section("Account");

    const auto balance = KisTrader::getBalance();
    if (!balance.success) {
        fail("Balance inquiry failed", balance.message);
        return;
    }

    std::cout << std::fixed << std::setprecision(0);
    ok("Balance inquiry", "cash " + std::to_string(static_cast<int64_t>(balance.cashBalance)) + " KRW, total "
                              + std::to_string(static_cast<int64_t>(balance.totalEvalAmount)) + " KRW");

    if (balance.holdings.empty()) {
        std::cout << "  holdings: none\n";
    } else {
        for (const auto& h : balance.holdings) {
            std::cout << "  holding: " << h.ticker << " " << h.name << " x" << h.quantity << " avg " << h.avgPrice
                      << " now " << h.currentPrice << " (" << std::setprecision(2) << h.profitLossRate << "%)"
                      << std::setprecision(0) << "\n";
        }
    }
}

void checkMarketData(const PortfolioConfig& config) {
    section("Market data");

    const data::KrxCalendar calendar;
    if (calendar.loaded()) {
        ok("Holiday calendar loaded");
    } else {
        warn("No holiday calendar", "only weekends will be treated as closed");
    }
    const std::string closed = calendar.closedReason(util::kstToday());
    std::cout << "  today (" << util::kstToday() << "): " << (closed.empty() ? "trading day" : "closed — " + closed)
              << "\n";

    // One KRX profile is enough to prove the data path; fetching all of them would
    // spend the rate limit to learn the same thing.
    const StrategyProfile* sample = nullptr;
    for (const auto& p : config.getProfiles()) {
        if (p.market == "KRX") {
            sample = &p;
            break;
        }
    }
    if (!sample) {
        warn("No KRX profile to test with");
        return;
    }

    KisProvider provider;
    const auto  daily = provider.getStockInfo(sample->ticker, "2026-01-01", util::kstToday());
    if (!daily || daily->close.empty()) {
        fail("Daily bars unavailable", "ticker " + sample->ticker);
    } else {
        ok("Daily bars", std::to_string(daily->close.size()) + " bars for " + sample->ticker);
    }

    if (closed.empty()) {
        const auto intraday = provider.getIntradayBars(sample->ticker);
        if (!intraday || intraday->close.empty()) {
            warn("No intraday bars", "expected during market hours; empty outside them");
        } else {
            ok("Intraday bars", std::to_string(intraday->close.size()) + " minute bars");
        }
    } else {
        std::cout << "  intraday: skipped (market closed — KIS serves today's bars only)\n";
    }
}

void checkStrategies(const PortfolioConfig& config) {
    section("Strategies");

    if (config.getProfiles().empty()) {
        fail("No profiles loaded", "check config/live.json");
        return;
    }
    ok("Profiles loaded", std::to_string(config.getProfiles().size()) + " total");

    // A position naming a strategy that is not in the catalog is dropped at load.
    // Silently trading four profiles when five were configured is not acceptable.
    for (const auto& id : config.unresolvedStrategies()) {
        fail("Unresolved strategy '" + id + "'", "no such id in the catalog; that position will not trade");
    }

    int broken = 0;
    for (const auto& p : config.getProfiles()) {
        if (!p.createStrategy()) {
            fail("#" + std::to_string(p.id) + " " + p.name, "unknown type '" + p.type + "'");
            ++broken;
        }
    }
    if (broken == 0) {
        ok("Every profile builds a strategy");
    }

    // Most profiles are disabled — kept for backtesting or retired. Printing only a
    // total would let the wrong set run unnoticed, so name exactly what trades.
    std::vector<const StrategyProfile*> live;
    std::vector<const StrategyProfile*> liveScalp;
    for (const auto& p : config.getProfiles()) {
        if (!p.enabled || p.market != "KRX") {
            continue;
        }
        (p.category == "scalp" ? liveScalp : live).push_back(&p);
    }

    if (live.empty() && liveScalp.empty()) {
        warn("No profile will trade", "every KRX profile is disabled; --all would do nothing");
    } else {
        ok("Live profiles", std::to_string(live.size()) + " daily (daily_trade --all), "
                                + std::to_string(liveScalp.size()) + " scalp (scalp_trade --all-scalp)");
    }
    for (const auto* p : live) {
        std::cout << "    daily  #" << p->id << " " << p->ticker << " " << p->name << "  pos "
                  << (p->positionPct * 100.0) << "%  stop " << p->stopLossPct << "%\n";
    }
    for (const auto* p : liveScalp) {
        std::cout << "    scalp  #" << p->id << " " << p->ticker << " " << p->name << "  pos "
                  << (p->positionPct * 100.0) << "%  stop " << p->stopLossPct << "%\n";
    }

    // Two strategies on one holding each act on the other's position, because the
    // broker balance cannot tell them apart.
    std::map<std::string, int> tickerCount;
    for (const auto* p : live) {
        ++tickerCount[p->ticker];
    }
    for (const auto* p : liveScalp) {
        ++tickerCount[p->ticker];
    }
    for (const auto& [ticker, count] : tickerCount) {
        if (count > 1) {
            fail("Ticker " + ticker + " is traded by " + std::to_string(count) + " live profiles",
                 "they share one holding and will act on each other's position");
        }
    }

    // Sizing and exit mistakes are silent until they spend the account or sell at
    // the wrong moment. Out-of-range percentages are already clamped when the config
    // loads; what remains here is what is in range and still unwise.
    for (const auto& p : config.getProfiles()) {
        const std::string label = "#" + std::to_string(p.id) + " " + p.name;
        if (p.positionPct <= 0.0) {
            fail(label + " position_pct=" + std::to_string(p.positionPct), "nothing would ever be bought");
        }
        if (!p.enabled) {
            continue;  // a disabled profile's settings cannot do harm
        }
        if (p.stopLossPct == 0.0 && p.trailingStopPct == 0.0 && p.market == "KRX") {
            warn(label, "no stop-loss and no trailing stop");
        }
        // A take-profit below the round trip loses money on every win it takes.
        if (p.takeProfitPct > 0.0 && p.takeProfitPct < 0.5) {
            warn(label + " take_profit_pct=" + std::to_string(p.takeProfitPct),
                 "below the ~0.34% KRX round trip — each win would net a loss");
        }
        if (p.takeProfitPct > 0.0 && p.stopLossPct > 0.0 && p.takeProfitPct < p.stopLossPct / 3.0) {
            warn(label, "take-profit is far tighter than the stop; losses would dwarf wins");
        }
    }
}

void checkRisk(const PortfolioConfig& config) {
    section("Risk limits");

    const auto& limits = config.getRiskLimits();
    if (limits.dailyLossLimitPct <= 0.0) {
        warn("No daily loss limit", "set risk.daily_loss_limit_pct in config/live.json");
    } else {
        ok("Daily loss limit", std::to_string(limits.dailyLossLimitPct) + "%");
    }
    if (limits.maxOrdersPerDay <= 0) {
        warn("No daily order cap", "set risk.max_orders_per_day");
    } else {
        ok("Daily order cap", std::to_string(limits.maxOrdersPerDay));
    }

    const trade::RiskGuard guard(limits);
    std::cout << "  today so far: " << guard.ordersToday() << " order(s), opening equity "
              << static_cast<int64_t>(guard.openingEquity()) << " KRW\n";
}

void checkLocalState() {
    section("Local state");

    const trade::TradeJournal journal;
    std::cout << "  journal: " << journal.path() << "\n";
    std::ifstream in(journal.path());
    if (!in.is_open()) {
        std::cout << "  (no journal yet — nothing has been decided)\n";
    } else {
        int               lines = 0, malformed = 0, today = 0;
        std::string       line;
        const std::string todayStr = util::kstToday();
        while (std::getline(in, line)) {
            if (line.empty()) {
                continue;
            }
            ++lines;
            try {
                const auto j = nlohmann::json::parse(line);
                if (j.value("time", "").rfind(todayStr, 0) == 0) {
                    ++today;
                }
            } catch (const std::exception&) {
                ++malformed;
            }
        }
        ok("Journal readable", std::to_string(lines) + " entries, " + std::to_string(today) + " today");
        if (malformed > 0) {
            warn("Malformed journal lines",
                 std::to_string(malformed) + " (a torn write from a killed process; harmless, skipped on read)");
        }
    }

    const trade::PositionStore positions;
    std::cout << "  positions: " << positions.path() << "\n";

    // The dashboard reads these; if they are stale the display is lying.
    const std::string snapshot = util::resolveFromExe("cache/portfolio.json");
    if (std::filesystem::exists(snapshot)) {
        std::error_code ec;
        const auto      age = std::filesystem::last_write_time(snapshot, ec);
        (void)age;
        ok("Dashboard snapshot present", snapshot);
    } else {
        warn("No dashboard snapshot", "the dashboard server has not run yet");
    }
}

void checkAlerts() {
    section("Alerts");
    const notify::Telegram telegram;
    if (telegram.enabled()) {
        ok("Telegram configured", "run notify_test to confirm delivery");
    } else {
        warn("Telegram not configured", "TELEGRAM_BOT_TOKEN / TELEGRAM_CHAT_ID are empty in .env");
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string configPath  = "config/live.json";
    bool        skipNetwork = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--offline") {
            skipNetwork = true;
        } else {
            std::cout << "Usage: doctor [--config <path>] [--offline]\n\n"
                      << "  Checks credentials, account access, market data, strategies, risk limits\n"
                      << "  and local state, and reports what would stop the trading loops working.\n"
                      << "  --offline skips everything that talks to KIS.\n";
            return (arg == "--help" || arg == "-h") ? 0 : 1;
        }
    }

    std::cout << "========================================================================================\n";
    std::cout << " kairos doctor\n";
    std::cout << "========================================================================================\n";

    const auto config = PortfolioConfig::loadFromFile(configPath);

    checkEnvironment();
    checkStrategies(config);
    checkRisk(config);
    checkLocalState();
    checkAlerts();
    if (!skipNetwork) {
        checkCredentials();
        checkAccount();
        checkMarketData(config);
    } else {
        section("Network checks");
        std::cout << "  skipped (--offline)\n";
    }

    std::cout << "\n========================================================================================\n";
    std::cout << " " << g_fail << " failure(s), " << g_warn << " warning(s)\n";
    std::cout << "========================================================================================\n";
    return g_fail > 0 ? 1 : 0;
}
