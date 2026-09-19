#pragma once

#include <string>

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

   private:
    std::string token_;
    std::string chatId_;
};

}  // namespace notify
