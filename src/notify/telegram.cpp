#include "notify/telegram.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "common/util.hpp"

namespace notify {

namespace {

std::size_t discardBody(void*, std::size_t size, std::size_t nmemb, void*) {
    return size * nmemb;  // the response carries nothing we act on
}

}  // namespace

Telegram::Telegram()
    : token_(util::envValue("TELEGRAM_BOT_TOKEN"))
    , chatId_(util::envValue("TELEGRAM_CHAT_ID")) {}

bool Telegram::send(const std::string& text) const {
    if (!enabled()) {
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    nlohmann::json body;
    body["chat_id"]           = chatId_;
    body["text"]              = text;
    const std::string bodyStr = body.dump();
    const std::string url     = "https://api.telegram.org/bot" + token_ + "/sendMessage";

    struct curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardBody);
    // A hung notification must not stall the trading loop behind it.
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    const CURLcode res    = curl_easy_perform(curl);
    long           status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK && status == 200;
}

}  // namespace notify
