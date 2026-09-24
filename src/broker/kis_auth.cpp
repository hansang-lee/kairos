#include "broker/kis_auth.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "common/util.hpp"

KisAuth& KisAuth::instance() {
    static KisAuth inst;
    return inst;
}

void KisAuth::init(const std::string& appKey, const std::string& appSecret, bool isPaper) {
    appKey_    = appKey;
    appSecret_ = appSecret;
    isPaper_   = isPaper;
}

void KisAuth::loadFromEnv(const std::string& envPath) {
    std::unordered_map<std::string, std::string> envMap;
    std::ifstream                                file(envPath);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            const auto pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string val = line.substr(pos + 1);
                // trim whitespace
                key.erase(0, key.find_first_not_of(" \t\r\n"));
                key.erase(key.find_last_not_of(" \t\r\n") + 1);
                val.erase(0, val.find_first_not_of(" \t\r\n"));
                val.erase(val.find_last_not_of(" \t\r\n") + 1);
                envMap[key] = val;
            }
        }
    }

    // Determine mode: default is paper
    std::string mode = "paper";
    if (envMap.count("KIS_MODE")) {
        mode = envMap["KIS_MODE"];
    } else if (const char* m = std::getenv("KIS_MODE")) {
        mode = m;
    }
    isPaper_ = (mode != "live" && mode != "real");

    auto getEnvVal = [&](const std::string& k1, const std::string& k2) -> std::string {
        if (envMap.count(k1) && !envMap[k1].empty())
            return envMap[k1];
        if (const char* v = std::getenv(k1.c_str()))
            return v;
        if (!k2.empty()) {
            if (envMap.count(k2) && !envMap[k2].empty())
                return envMap[k2];
            if (const char* v = std::getenv(k2.c_str()))
                return v;
        }
        return "";
    };

    if (isPaper_) {
        appKey_      = getEnvVal("KIS_PAPER_APP_KEY", "KIS_APP_KEY");
        appSecret_   = getEnvVal("KIS_PAPER_APP_SECRET", "KIS_APP_SECRET");
        accountNo_   = getEnvVal("KIS_PAPER_ACCOUNT_NO", "KIS_ACCOUNT_NO");
        accountProd_ = getEnvVal("KIS_PAPER_ACCOUNT_PROD", "KIS_ACCOUNT_PROD");
    } else {
        appKey_      = getEnvVal("KIS_REAL_APP_KEY", "KIS_APP_KEY");
        appSecret_   = getEnvVal("KIS_REAL_APP_SECRET", "KIS_APP_SECRET");
        accountNo_   = getEnvVal("KIS_REAL_ACCOUNT_NO", "KIS_ACCOUNT_NO");
        accountProd_ = getEnvVal("KIS_REAL_ACCOUNT_PROD", "KIS_ACCOUNT_PROD");
    }
    if (accountProd_.empty()) {
        accountProd_ = "01";
    }
}

std::string KisAuth::getBaseUrl() const {
    return isPaper_ ? "https://openapivts.koreainvestment.com:29443" : "https://openapi.koreainvestment.com:9443";
}

std::size_t KisAuth::writeCallback(void* contents, std::size_t size, std::size_t nmemb, void* userp) {
    auto* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

bool KisAuth::loadTokenFromCache() {
    const std::string cachePath =
        util::resolveFromExe(isPaper_ ? "cache/kis_token_paper.json" : "cache/kis_token_real.json");
    if (!std::filesystem::exists(cachePath)) {
        return false;
    }

    try {
        std::ifstream  file(cachePath);
        nlohmann::json j;
        file >> j;

        const auto now =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();

        const int64_t exp = j.value("expires_at", 0LL);
        if (exp > now + 60) {
            cachedToken_ = j.value("token", "");
            expiresAt_   = exp;
            return !cachedToken_.empty();
        }
    } catch (const std::exception& e) {
        // Cache read failed, ignore and fetch new
    }
    return false;
}

bool KisAuth::saveTokenToCache(const std::string& token, int64_t expiresIn) {
    try {
        std::filesystem::create_directories(util::resolveFromExe("cache"));
        const auto now =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();

        nlohmann::json j;
        j["token"]      = token;
        j["expires_at"] = now + expiresIn;

        const std::string cachePath =
            util::resolveFromExe(isPaper_ ? "cache/kis_token_paper.json" : "cache/kis_token_real.json");
        std::ofstream file(cachePath);
        file << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save token cache: " << e.what() << std::endl;
        return false;
    }
}

std::string KisAuth::requestNewToken() {
    if (appKey_.empty() || appSecret_.empty()) {
        std::cerr << "KisAuth Error: KIS_APP_KEY or KIS_APP_SECRET is not set." << std::endl;
        return "";
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return "";
    }

    const std::string url = getBaseUrl() + "/oauth2/tokenP";
    nlohmann::json    body;
    body["grant_type"]        = "client_credentials";
    body["appkey"]            = appKey_;
    body["appsecret"]         = appSecret_;
    const std::string bodyStr = body.dump();

    std::string        response;
    struct curl_slist* headers = nullptr;
    headers                    = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    // Without these a hung connection blocks the process indefinitely: the
    // scalping loop would stop polling and stop answering SIGTERM, and a
    // one-shot run would be killed by systemd part-way through.
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "Token request failed: " << curl_easy_strerror(res) << std::endl;
        return "";
    }

    try {
        const auto j = nlohmann::json::parse(response);
        if (j.contains("access_token")) {
            cachedToken_            = j["access_token"].get<std::string>();
            const int64_t expiresIn = j.value("expires_in", 86400LL);
            saveTokenToCache(cachedToken_, expiresIn);
            return cachedToken_;
        } else {
            std::cerr << "Token error response: " << response << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "JSON parse error during token request: " << e.what() << std::endl;
    }

    return "";
}

std::string KisAuth::getAccessToken() {
    if (loadTokenFromCache()) {
        return cachedToken_;
    }
    return requestNewToken();
}
