#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "broker/kis_trader.hpp"
#include "common/util.hpp"
#include "data/kis_provider.hpp"
#include "notify/bot_commands.hpp"
#include "notify/telegram.hpp"
#include "strategy/strategy_catalog.hpp"
#include "strategy/strategy_factory.hpp"

/**
 * Answers account questions asked from a phone.
 *
 * Runs as a one-shot on a timer rather than as a daemon, which is the same shape as
 * the trader and keeps the service count where it was. It asks Telegram what has
 * arrived, answers it, and exits — so the cost of being reachable is a few
 * milliseconds a minute rather than a process that must be kept alive.
 *
 * Telegram redelivers an update until it is acknowledged by asking for a later
 * offset, so the last id handled is kept on disk. Without it a single "/status"
 * would be answered once a minute forever.
 */
namespace {

/** KST calendar date, `daysAgo` days back, as "YYYY-MM-DD". */
std::string kstDate(int daysAgo = 0) {
    const std::time_t  kst = std::time(nullptr) + 9 * 3600 - static_cast<std::time_t>(daysAgo) * 86400;
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&kst), "%Y-%m-%d");
    return oss.str();
}

std::string offsetPath() {
    return util::resolveFromExe("data/telegram_offset.json");
}

int64_t loadOffset() {
    std::ifstream in(offsetPath());
    if (!in.is_open()) {
        return 0;
    }
    try {
        nlohmann::json j;
        in >> j;
        return j.value("next_update_id", static_cast<int64_t>(0));
    } catch (const std::exception&) {
        return 0;
    }
}

void saveOffset(int64_t next) {
    std::ofstream out(offsetPath(), std::ios::trunc);
    if (out.is_open()) {
        out << nlohmann::json{{"next_update_id", next}}.dump() << "\n";
    }
}

notify::BotSnapshot snapshot(const nlohmann::json& live) {
    notify::BotSnapshot s;
    const auto          balance = KisTrader::getBalance();
    if (!balance.success) {
        s.message = balance.message;
        return s;
    }
    s.ok             = true;
    s.cashBalance    = balance.cashBalance;
    s.totalEval      = balance.totalEvalAmount;
    s.holdings       = balance.holdings;
    s.initialCapital = live.value("initial_capital_krw", 0.0);
    return s;
}

std::vector<notify::BotSignal> currentSignals(const nlohmann::json& live, const StrategyCatalog& catalog) {
    std::vector<notify::BotSignal> out;
    if (!live.contains("positions")) {
        return out;
    }
    const auto balance = KisTrader::getBalance();
    KisProvider provider;

    for (const auto& pos : live["positions"]) {
        if (!pos.value("enabled", false)) {
            continue;
        }
        const auto* def = catalog.find(pos.value("strategy", ""));
        if (def == nullptr) {
            continue;
        }
        StrategyProfile prof;
        prof.type    = def->type;
        prof.params  = def->params;
        auto strat   = prof.createStrategy();
        const auto data = provider.getStockInfo(pos.value("ticker", ""), kstDate(400), kstDate(0));
        if (!strat || !data || data->close.size() <= strat->warmupPeriod()) {
            continue;
        }
        strat->init(*data);

        notify::BotSignal s;
        s.profileId = pos.value("id", -1);
        s.name      = def->name;
        s.ticker    = pos.value("ticker", "");
        s.price     = data->close.back();

        const Signal sig = strat->evaluate(*data, data->close.size());
        s.signal         = sig == Signal::BUY ? "BUY" : (sig == Signal::SELL ? "SELL" : "HOLD");
        for (const auto& h : balance.holdings) {
            if (h.ticker == s.ticker) {
                s.heldQty = h.quantity;
            }
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<notify::BotTrade> recentTrades() {
    std::vector<notify::BotTrade> out;
    std::ifstream                 in(util::resolveFromExe("data/trades.jsonl"));
    if (!in.is_open()) {
        return out;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        try {
            const auto        j = nlohmann::json::parse(line);
            notify::BotTrade  t;
            t.time     = j.value("time", "");
            t.event    = j.value("event", "");
            t.ticker   = j.value("ticker", "");
            t.side     = j.value("side", "");
            t.quantity = j.value("qty", static_cast<int64_t>(0));
            t.price    = j.value("price", 0.0);
            t.reason   = j.value("reason", "");
            out.push_back(std::move(t));
        } catch (const std::exception&) {
            continue;
        }
    }
    return out;
}

}  // namespace

int main(int argc, char* argv[]) {
    // Silence is the normal outcome — nothing arrived — and it is also what every
    // failure looks like from the outside. --verbose is the difference.
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--verbose" || a == "-v") {
            verbose = true;
        }
        if (a == "--help" || a == "-h") {
            std::cout << "Usage:\n  bot [--verbose]\n\n"
                      << "  Answers Telegram commands about the account, once, then exits.\n"
                      << "  Meant to run from a short-interval timer. Only the configured\n"
                      << "  TELEGRAM_CHAT_ID is answered; anyone else is ignored.\n"
                      << "  --verbose prints what arrived and what was done with it.\n";
            return 0;
        }
    }

    const notify::Telegram telegram;
    if (!telegram.enabled()) {
        std::cerr << "[-] TELEGRAM_BOT_TOKEN / TELEGRAM_CHAT_ID not set; nothing to do." << std::endl;
        return 1;
    }

    const int64_t offset  = loadOffset();
    const auto    updates = telegram.getUpdates(offset);
    if (verbose) {
        std::cerr << "[v] offset " << offset << ", " << updates.size() << " update(s), configured chat ["
                  << telegram.chatId() << "]" << std::endl;
    }
    if (updates.empty()) {
        return 0;
    }

    const auto live    = util::loadJsonConfig("config/live.json");
    const auto catalog = StrategyCatalog::loadFromFile();

    int64_t highest = offset - 1;
    for (const auto& u : updates) {
        highest = std::max(highest, u.updateId);

        // Silence rather than a refusal: a refusal confirms the bot is live and
        // worth probing. The attempt is logged so it is not invisible.
        if (!notify::isAuthorised(u.chatId, telegram.chatId())) {
            if (!u.chatId.empty()) {
                std::cerr << "[!] Ignored a message from an unconfigured chat." << std::endl;
            }
            continue;
        }

        const std::string cmd = notify::commandOf(u.text);
        if (verbose) {
            std::cerr << "[v] id " << u.updateId << " from [" << u.chatId << "] text [" << u.text << "] cmd [" << cmd
                      << "]" << std::endl;
        }
        std::string       reply;
        if (cmd == "/status" || cmd == "/start") {
            reply = notify::formatStatus(snapshot(live ? *live : nlohmann::json::object()));
        } else if (cmd == "/positions") {
            reply = notify::formatPositions(snapshot(live ? *live : nlohmann::json::object()));
        } else if (cmd == "/signals") {
            reply = catalog.loaded() && live ? notify::formatSignals(currentSignals(*live, catalog))
                                             : "설정을 읽지 못했습니다";
        } else if (cmd == "/trades") {
            reply = notify::formatTrades(recentTrades(), 10);
        } else if (cmd == "/help") {
            reply = notify::helpText();
        } else if (!cmd.empty() && cmd.front() == '/') {
            reply = "모르는 명령입니다\n\n" + notify::helpText();
        }

        if (reply.empty()) {
            if (verbose) {
                std::cerr << "[v] no reply for [" << cmd << "]" << std::endl;
            }
            continue;
        }
        if (verbose) {
            std::cerr << "[v] replying " << reply.size() << " bytes" << std::endl;
        }
        if (!telegram.send(reply)) {
            std::cerr << "[!] Reply to " << cmd << " could not be delivered." << std::endl;
        }
    }

    // Written after the loop: a crash mid-batch leaves the offset alone, so the
    // unanswered messages come back rather than disappearing.
    saveOffset(highest + 1);
    return 0;
}
