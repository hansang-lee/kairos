#include <iostream>
#include <string>

#include "notify/telegram.hpp"

/**
 * Sends one message so a Telegram setup can be verified before it is relied on
 * during a live session — a notifier that silently does nothing is worse than none.
 */
int main(int argc, char* argv[]) {
    const std::string text = (argc >= 2) ? argv[1] : "kairos: notification test";

    const notify::Telegram telegram;
    if (!telegram.enabled()) {
        std::cerr << "[-] Telegram is not configured. Set TELEGRAM_BOT_TOKEN and TELEGRAM_CHAT_ID\n"
                  << "    in .env (both must be non-empty), then run this again.\n";
        return 1;
    }

    std::cout << "[*] Sending: " << text << std::endl;
    if (!telegram.send(text)) {
        std::cerr << "[-] Telegram rejected the message. Check the bot token, and that the chat ID\n"
                  << "    refers to a chat the bot has been started in.\n";
        return 1;
    }

    std::cout << "[+] Sent. If it did not arrive, the credentials point at a different chat." << std::endl;
    return 0;
}
