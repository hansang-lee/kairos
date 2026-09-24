#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "broker/kis_trader.hpp"

namespace notify {

/**
 * @brief Turning a chat message into an answer about the account.
 *
 * Kept as functions over plain data rather than something that queries the broker,
 * so the formatting and the access check can be tested without an account, a
 * network, or a phone.
 */

/** @brief One line of the account, as a command needs to render it. */
struct BotSnapshot {
    bool        ok = false;
    std::string message;  ///< why, when ok is false

    double initialCapital = 0.0;
    double cashBalance    = 0.0;
    double totalEval      = 0.0;

    std::vector<StockHolding> holdings;
};

/** @brief What a strategy is saying right now, for /signals. */
struct BotSignal {
    int         profileId = -1;
    std::string name;
    std::string ticker;
    std::string signal;  ///< "BUY", "SELL", "HOLD"
    double      price     = 0.0;
    int64_t     heldQty   = 0;
};

/** @brief A line of the trade journal, for /trades. */
struct BotTrade {
    std::string time;
    std::string event;
    std::string ticker;
    std::string side;
    int64_t     quantity = 0;
    double      price    = 0.0;
    std::string reason;
};

/**
 * @brief Whether this sender is allowed an answer at all.
 *
 * A bot's username is searchable, so anyone who finds it can message it. Without
 * this check the first stranger to type /status reads the account. The configured
 * chat is the only one that gets a reply; everyone else gets silence rather than a
 * refusal, because a refusal confirms the bot is live and worth probing.
 */
[[nodiscard]] bool isAuthorised(const std::string& senderChatId, const std::string& configuredChatId);

/** @brief The command word of a message, lowercased and without its @botname suffix. */
[[nodiscard]] std::string commandOf(const std::string& text);

[[nodiscard]] std::string formatStatus(const BotSnapshot& snap);
[[nodiscard]] std::string formatPositions(const BotSnapshot& snap);
[[nodiscard]] std::string formatSignals(const std::vector<BotSignal>& signals);
[[nodiscard]] std::string formatTrades(const std::vector<BotTrade>& trades, std::size_t limit);
[[nodiscard]] std::string helpText();

}  // namespace notify
