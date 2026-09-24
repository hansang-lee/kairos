#include "notify/telegram.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "common/util.hpp"

namespace notify {

namespace {

std::size_t discardBody(void*, std::size_t size, std::size_t nmemb, void*) {
    return size * nmemb;  // the response carries nothing we act on
}

std::size_t collectBody(void* contents, std::size_t size, std::size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
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

std::vector<Telegram::Update> Telegram::getUpdates(int64_t offset) const {
    std::vector<Update> out;
    if (!enabled()) {
        return out;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return out;
    }

    const std::string url =
        "https://api.telegram.org/bot" + token_ + "/getUpdates?timeout=0&offset=" + std::to_string(offset);
    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collectBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    const CURLcode res    = curl_easy_perform(curl);
    long           status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || status != 200) {
        return out;
    }

    try {
        const auto parsed = nlohmann::json::parse(body);
        if (!parsed.value("ok", false) || !parsed.contains("result")) {
            return out;
        }
        for (const auto& u : parsed["result"]) {
            Update up;
            up.updateId = u.value("update_id", static_cast<int64_t>(0));
            // Edited messages and channel posts carry no command worth acting on, and
            // a missing message block must not abort the ones that follow it.
            if (!u.contains("message") || !u["message"].contains("chat")) {
                out.push_back(up);  // still returned, so the offset advances past it
                continue;
            }
            const auto& m = u["message"];
            up.chatId     = std::to_string(m["chat"].value("id", static_cast<int64_t>(0)));
            up.text       = m.value("text", "");
            out.push_back(up);
        }
    } catch (const std::exception&) {
        return {};  // a malformed page is not worth guessing at
    }
    return out;
}

}  // namespace notify
