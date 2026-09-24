#pragma once

#include "common/util.hpp"

#include <string>

class KisAuth {
   public:
    static KisAuth& instance();

    void init(const std::string& appKey, const std::string& appSecret, bool isPaper = true);
    /**
     * @param envPath Defaults to <project-root>/.env, resolved from the executable
     *        like every other file this program touches. It was ".env" relative to
     *        the working directory, which meant a run started from anywhere else
     *        found no credentials and requested a fresh token — and KIS limits how
     *        often that may happen.
     */
    void loadFromEnv(const std::string& envPath = util::resolveFromExe(".env"));

    [[nodiscard]] std::string getAccessToken();
    [[nodiscard]] std::string getAppKey() const { return appKey_; }
    [[nodiscard]] std::string getAppSecret() const { return appSecret_; }
    [[nodiscard]] std::string getAccountNo() const { return accountNo_; }
    [[nodiscard]] std::string getAccountProd() const { return accountProd_; }
    [[nodiscard]] std::string getBaseUrl() const;
    [[nodiscard]] bool        isPaper() const { return isPaper_; }

   private:
    KisAuth()  = default;
    ~KisAuth() = default;

    std::string appKey_;
    std::string appSecret_;
    std::string accountNo_;
    std::string accountProd_ = "01";
    bool        isPaper_     = true;
    std::string cachedToken_;
    int64_t     expiresAt_ = 0;

    bool        loadTokenFromCache();
    bool        saveTokenToCache(const std::string& token, int64_t expiresIn);
    std::string requestNewToken();

    static std::size_t writeCallback(void* contents, std::size_t size, std::size_t nmemb, void* userp);
};
