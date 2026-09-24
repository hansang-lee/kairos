#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace notify {

/**
 * @brief Telegram push notifications for events worth interrupting someone over.
 *
 * Disabled unless both TELEGRAM_BOT_TOKEN and TELEGRAM_CHAT_ID are set (in .env
 * or the environment). A disabled notifier is not an error — send() just returns
 * false — so an unconfigured setup never blocks trading.
 */
class Telegram {
   public:
    Telegram();

    /** @brief True when credentials were found and send() will attempt delivery. */
    [[nodiscard]] bool enabled() const { return !token_.empty() && !chatId_.empty(); }

    /**
     * @brief Send a plain-text message. Never throws; a delivery failure is reported
     *        by the return value and must not stop a trading loop.
     * @return true if Telegram accepted the message.
     */
    bool send(const std::string& text) const;

    /** @brief One incoming message, reduced to what a command handler needs. */
    struct Update {
        int64_t     updateId = 0;
        std::string chatId;  ///< who sent it; compare against the configured chat
        std::string text;
    };

    /**
     * @brief Fetch messages waiting at Telegram, oldest first.
     *
     * Long polling is deliberately not used: this runs as a one-shot on a timer, and
     * a call that blocks for thirty seconds waiting for a message would hold the
     * process open for no reason. It asks, takes whatever is there, and exits.
     *
     * @param offset Lowest update id to return. Passing (last seen + 1) is what tells
     *        Telegram the earlier ones are handled; without it the same message comes
     *        back forever.
     */
    [[nodiscard]] std::vector<Update> getUpdates(int64_t offset) const;

    /** @brief The chat this bot is configured to talk to, and the only one it answers. */
    [[nodiscard]] const std::string& chatId() const { return chatId_; }

   private:
    std::string token_;
    std::string chatId_;
};

}  // namespace notify
