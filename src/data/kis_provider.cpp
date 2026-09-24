#include "data/kis_provider.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace {

struct DayBar {
    int64_t ts     = 0;
    double  open   = 0.0;
    double  high   = 0.0;
    double  low    = 0.0;
    double  close  = 0.0;
    int64_t volume = 0;
};

std::string normalizeDate(std::string_view date) {
    std::string result;
    for (char c : date) {
        if (c >= '0' && c <= '9') {
            result.push_back(c);
        }
    }
    return result;
}

int64_t parseKisDateToTimestamp(const std::string& yyyymmdd) {
    if (yyyymmdd.size() != 8) {
        return 0;
    }
    std::tm tm  = {};
    tm.tm_year  = std::stoi(yyyymmdd.substr(0, 4)) - 1900;
    tm.tm_mon   = std::stoi(yyyymmdd.substr(4, 2)) - 1;
    tm.tm_mday  = std::stoi(yyyymmdd.substr(6, 2));
    tm.tm_hour  = 9;  // KST Market Open 09:00
    tm.tm_isdst = 0;
    return timegm(&tm);
}

std::string shiftDateDays(const std::string& yyyymmdd, int deltaDays) {
    const int64_t     ts    = parseKisDateToTimestamp(yyyymmdd) + static_cast<int64_t>(deltaDays) * 86400;
    const std::time_t t     = static_cast<std::time_t>(ts);
    std::tm*          tmPtr = std::gmtime(&t);
    char              buf[16];
    std::strftime(buf, sizeof(buf), "%Y%m%d", tmPtr);
    return std::string(buf);
}

std::size_t writeCallback(void* contents, std::size_t size, std::size_t nmemb, void* userp) {
    auto* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

/**
 * @brief Perform one HTTP call for the given date window. Returns the raw response
 *        body, or empty on a transport-level failure.
 */
std::string fetchOnce(const std::string& ticker, const std::string& startYmd, const std::string& endYmd,
                      const std::string& periodCode, const std::string& token, const KisAuth& auth) {
    const std::string url = auth.getBaseUrl()
                          + "/uapi/domestic-stock/v1/quotations/inquire-daily-itemchartprice"
                            "?FID_COND_MRKT_DIV_CODE=J"
                            "&FID_INPUT_ISCD="
                          + ticker + "&FID_INPUT_DATE_1=" + startYmd + "&FID_INPUT_DATE_2=" + endYmd
                          + "&FID_PERIOD_DIV_CODE=" + periodCode + "&FID_ORG_ADJ_PRC=0";

    CURL* curl = curl_easy_init();
    if (!curl) {
        return "";
    }

    std::string        response;
    struct curl_slist* headers = nullptr;
    headers                    = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
    headers                    = curl_slist_append(headers, ("authorization: Bearer " + token).c_str());
    headers                    = curl_slist_append(headers, ("appkey: " + auth.getAppKey()).c_str());
    headers                    = curl_slist_append(headers, ("appsecret: " + auth.getAppSecret()).c_str());
    headers                    = curl_slist_append(headers, "tr_id: FHKST03010100");
    headers                    = curl_slist_append(headers, "custtype: P");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    // Without these a hung connection blocks the process indefinitely: the
    // scalping loop would stop polling and stop answering SIGTERM, and a
    // one-shot run would be killed by systemd part-way through.
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "KisProvider request failed: " << curl_easy_strerror(res) << std::endl;
        return "";
    }
    return response;
}

/**
 * @brief Fetch one page of KIS daily-chart data (the endpoint returns at most ~100
 *        bars per call, regardless of the requested date range).
 *
 * Retries with backoff on every error KIS raises that means "ask again" rather
 * than "there is nothing there": the per-second rate limit (EGW00201), the
 * generic retry request (EGW00316, whose own message says 재 조회 수행 부탁드립니다),
 * a routing failure (OPSQ0003), and a transport timeout. Treating any of those as
 * the end of the history is what turns a partial page into a cached series that
 * looks complete — a 2,700-bar fund arrived as 500 bars starting in 2024 and would
 * have been backtested that way.
 *
 * @param failed Set when the page could not be read after every retry, which the
 *        caller must distinguish from an empty page meaning no more history.
 * @return Bars in chronological (oldest-first) order; empty on failure or no data.
 */
std::vector<DayBar> fetchChunk(const std::string& ticker, const std::string& startYmd, const std::string& endYmd,
                               const std::string& periodCode, bool* failed = nullptr) {
    auto& auth = KisAuth::instance();
    auth.loadFromEnv();
    const std::string token = auth.getAccessToken();
    if (token.empty()) {
        std::cerr << "KisProvider: Failed to acquire access token." << std::endl;
        return {};
    }

    if (failed != nullptr) {
        *failed = false;
    }

    constexpr int kMaxAttempts = 6;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        const std::string response = fetchOnce(ticker, startYmd, endYmd, periodCode, token, auth);
        if (response.empty()) {
            // Transport-level failure, already logged by fetchOnce. Retryable: a
            // timeout says nothing about whether the history exists.
            std::this_thread::sleep_for(std::chrono::milliseconds(1500 * (attempt + 1)));
            continue;
        }

        try {
            const auto json = nlohmann::json::parse(response);
            const auto code = json.value("msg_cd", std::string{});
            if (code == "EGW00201" || code == "EGW00316" || code == "OPSQ0003") {
                const auto backoff = std::chrono::milliseconds(1500 * (attempt + 1));
                std::this_thread::sleep_for(backoff);
                continue;
            }
            if (!json.contains("output2") || !json["output2"].is_array()) {
                std::cerr << "KisProvider error response: " << response << std::endl;
                if (failed != nullptr) {
                    *failed = true;
                }
                return {};
            }

            // KIS returns rows newest-first; we want oldest-first.
            std::vector<nlohmann::json> rows(json["output2"].begin(), json["output2"].end());
            std::reverse(rows.begin(), rows.end());

            std::vector<DayBar> bars;
            for (const auto& item : rows) {
                const std::string dateStr = item.value("stck_bsop_date", "");
                if (dateStr.empty()) {
                    continue;
                }

                DayBar bar;
                bar.ts     = parseKisDateToTimestamp(dateStr);
                bar.open   = std::stod(item.value("stck_oprc", "0"));
                bar.high   = std::stod(item.value("stck_hgpr", "0"));
                bar.low    = std::stod(item.value("stck_lwpr", "0"));
                bar.close  = std::stod(item.value("stck_clpr", "0"));
                bar.volume = std::stoll(item.value("acml_vol", "0"));
                bars.push_back(bar);
            }
            return bars;
        } catch (const std::exception& e) {
            std::cerr << "KisProvider parse exception: " << e.what() << std::endl;
            if (failed != nullptr) {
                *failed = true;
            }
            return {};
        }
    }

    std::cerr << "KisProvider: gave up on " << ticker << " after " << kMaxAttempts << " retries." << std::endl;
    if (failed != nullptr) {
        *failed = true;
    }
    return {};
}

/**
 * @brief Perform one HTTP call for today's intraday minute chart.
 */
std::string fetchIntradayOnce(const std::string& ticker, const std::string& hourHHMMSS, const std::string& token,
                              const KisAuth& auth) {
    const std::string url = auth.getBaseUrl()
                          + "/uapi/domestic-stock/v1/quotations/inquire-time-itemchartprice"
                            "?FID_COND_MRKT_DIV_CODE=J"
                            "&FID_INPUT_ISCD="
                          + ticker + "&FID_INPUT_HOUR_1=" + hourHHMMSS + "&FID_PW_DATA_INCU_YN=Y&FID_ETC_CLS_CODE=";

    CURL* curl = curl_easy_init();
    if (!curl) {
        return "";
    }

    std::string        response;
    struct curl_slist* headers = nullptr;
    headers                    = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
    headers                    = curl_slist_append(headers, ("authorization: Bearer " + token).c_str());
    headers                    = curl_slist_append(headers, ("appkey: " + auth.getAppKey()).c_str());
    headers                    = curl_slist_append(headers, ("appsecret: " + auth.getAppSecret()).c_str());
    headers                    = curl_slist_append(headers, "tr_id: FHKST03010200");
    headers                    = curl_slist_append(headers, "custtype: P");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    // Without these a hung connection blocks the process indefinitely: the
    // scalping loop would stop polling and stop answering SIGTERM, and a
    // one-shot run would be killed by systemd part-way through.
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "KisProvider intraday request failed: " << curl_easy_strerror(res) << std::endl;
        return "";
    }
    return response;
}

/**
 * @brief Fetch today's minute bars (up to ~30) as of the given HHMMSS, with the
 *        same rate-limit retry/backoff as the daily-chart fetch.
 * @return Bars in chronological (oldest-first) order, or empty on failure/no data.
 */
std::vector<DayBar> fetchIntradayChunk(const std::string& ticker, const std::string& hourHHMMSS) {
    auto& auth = KisAuth::instance();
    auth.loadFromEnv();
    const std::string token = auth.getAccessToken();
    if (token.empty()) {
        std::cerr << "KisProvider: Failed to acquire access token." << std::endl;
        return {};
    }

    constexpr int kMaxAttempts = 5;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        const std::string response = fetchIntradayOnce(ticker, hourHHMMSS, token, auth);
        if (response.empty()) {
            return {};
        }

        try {
            const auto json = nlohmann::json::parse(response);
            if (json.value("msg_cd", "") == "EGW00201") {
                std::this_thread::sleep_for(std::chrono::milliseconds(1500 * (attempt + 1)));
                continue;
            }
            if (!json.contains("output2") || !json["output2"].is_array()) {
                std::cerr << "KisProvider intraday error response: " << response << std::endl;
                return {};
            }

            // Rows come newest-first; we want oldest-first.
            std::vector<nlohmann::json> rows(json["output2"].begin(), json["output2"].end());
            std::reverse(rows.begin(), rows.end());

            std::vector<DayBar> bars;
            for (const auto& item : rows) {
                const std::string dateStr = item.value("stck_bsop_date", "");
                const std::string hourStr = item.value("stck_cntg_hour", "");
                if (dateStr.size() != 8 || hourStr.size() != 6) {
                    continue;
                }

                std::tm tm  = {};
                tm.tm_year  = std::stoi(dateStr.substr(0, 4)) - 1900;
                tm.tm_mon   = std::stoi(dateStr.substr(4, 2)) - 1;
                tm.tm_mday  = std::stoi(dateStr.substr(6, 2));
                tm.tm_hour  = std::stoi(hourStr.substr(0, 2));
                tm.tm_min   = std::stoi(hourStr.substr(2, 2));
                tm.tm_sec   = std::stoi(hourStr.substr(4, 2));
                tm.tm_isdst = 0;

                DayBar bar;
                // Matches parseKisDateToTimestamp's convention: KST wall-clock fields
                // embedded via timegm rather than truly converted to UTC (relative
                // ordering within a series is what matters here, not absolute TZ).
                bar.ts     = timegm(&tm);
                bar.open   = std::stod(item.value("stck_oprc", "0"));
                bar.high   = std::stod(item.value("stck_hgpr", "0"));
                bar.low    = std::stod(item.value("stck_lwpr", "0"));
                bar.close  = std::stod(item.value("stck_prpr", "0"));
                bar.volume = std::stoll(item.value("cntg_vol", "0"));
                bars.push_back(bar);
            }
            return bars;
        } catch (const std::exception& e) {
            std::cerr << "KisProvider intraday parse exception: " << e.what() << std::endl;
            return {};
        }
    }

    std::cerr << "KisProvider: gave up after rate-limit retries (intraday)." << std::endl;
    return {};
}

}  // namespace

std::shared_ptr<StockInfo> KisProvider::getStockInfo(std::string_view ticker, std::string_view startDate,
                                                     std::string_view endDate, std::string_view interval) {
    const std::string tickerStr = std::string(ticker);
    const std::string normStart = normalizeDate(startDate);
    const std::string normEnd   = normalizeDate(endDate);
    const int64_t     startTs   = parseKisDateToTimestamp(normStart);
    const int64_t     endTs     = parseKisDateToTimestamp(normEnd);

    std::string periodCode = "D";
    if (interval == "1wk" || interval == "W") {
        periodCode = "W";
    } else if (interval == "1mo" || interval == "M") {
        periodCode = "M";
    }

    // Page backward in time until the requested start is covered, a page comes back
    // short (no more history available), or the safety cap is hit.
    std::vector<DayBar> allBars;
    std::string         currentEnd = normEnd;
    constexpr int       kMaxPages  = 40;

    for (int page = 0; page < kMaxPages; ++page) {
        bool pageFailed = false;
        auto chunk      = fetchChunk(tickerStr, normStart, currentEnd, periodCode, &pageFailed);

        // A page that could not be read is not the same as a fund whose history
        // ends here, and the difference is invisible once the bars are on disk.
        // Returning nothing makes the caller handle a failure it can see, instead
        // of caching a series that starts years later than it should.
        if (pageFailed) {
            std::cerr << "KisProvider: " << tickerStr << " incomplete at page " << page << " (have " << allBars.size()
                      << " bars, wanted back to " << normStart << "); returning nothing rather than a truncated series."
                      << std::endl;
            return nullptr;
        }
        if (chunk.empty()) {
            break;  // genuinely no more history
        }

        allBars.insert(allBars.end(), chunk.begin(), chunk.end());

        const bool reachedStart = chunk.front().ts <= startTs;
        const bool shortPage    = chunk.size() < 100;
        if (reachedStart || shortPage) {
            break;
        }

        std::time_t oldestTs = static_cast<std::time_t>(chunk.front().ts);
        std::tm*    tmPtr    = std::gmtime(&oldestTs);
        char        buf[16];
        std::strftime(buf, sizeof(buf), "%Y%m%d", tmPtr);
        currentEnd = shiftDateDays(std::string(buf), -1);

        std::this_thread::sleep_for(std::chrono::milliseconds(600));
    }

    if (allBars.empty()) {
        return nullptr;
    }

    // Sort chronologically, drop duplicate dates (chunks can overlap at the boundary
    // day), and trim to the requested range.
    std::sort(allBars.begin(), allBars.end(), [](const DayBar& a, const DayBar& b) { return a.ts < b.ts; });
    allBars.erase(
        std::unique(allBars.begin(), allBars.end(), [](const DayBar& a, const DayBar& b) { return a.ts == b.ts; }),
        allBars.end());
    allBars.erase(
        std::remove_if(allBars.begin(), allBars.end(), [&](const DayBar& b) { return b.ts < startTs || b.ts > endTs; }),
        allBars.end());

    auto data            = std::make_shared<StockInfo>();
    data->ticker         = tickerStr;
    data->currency       = "KRW";
    data->exchangeName   = "KRX";
    data->instrumentType = "COMMON_STOCK";
    data->timezone       = "Asia/Seoul";

    for (const auto& bar : allBars) {
        data->timestamps.push_back(bar.ts);
        data->open.push_back(bar.open);
        data->high.push_back(bar.high);
        data->low.push_back(bar.low);
        data->close.push_back(bar.close);
        data->volume.push_back(bar.volume);
    }

    if (!data->close.empty()) {
        data->regularMarketPrice = data->close.back();
        data->chartPreviousClose = data->close.size() > 1 ? data->close[data->close.size() - 2] : data->close.back();
    }

    return data;
}

std::shared_ptr<StockInfo> KisProvider::getIntradayBars(std::string_view ticker, std::string_view asOfTime) {
    const std::string tickerStr = std::string(ticker);
    std::string       hourStr   = normalizeDate(asOfTime);

    if (hourStr.empty()) {
        // "Now", as KST wall-clock HHMMSS (matches the timegm convention used elsewhere
        // in this file: shift the UTC epoch forward 9h, then read the fields as-is).
        const std::time_t kstNow = std::time(nullptr) + 9 * 3600;
        std::tm*          tmPtr  = std::gmtime(&kstNow);
        char              buf[7];
        std::strftime(buf, sizeof(buf), "%H%M%S", tmPtr);
        hourStr = buf;
    }

    const auto bars = fetchIntradayChunk(tickerStr, hourStr);
    if (bars.empty()) {
        return nullptr;
    }

    auto data            = std::make_shared<StockInfo>();
    data->ticker         = tickerStr;
    data->currency       = "KRW";
    data->exchangeName   = "KRX";
    data->instrumentType = "COMMON_STOCK";
    data->timezone       = "Asia/Seoul";

    for (const auto& bar : bars) {
        data->timestamps.push_back(bar.ts);
        data->open.push_back(bar.open);
        data->high.push_back(bar.high);
        data->low.push_back(bar.low);
        data->close.push_back(bar.close);
        data->volume.push_back(bar.volume);
    }

    if (!data->close.empty()) {
        data->regularMarketPrice = data->close.back();
        data->chartPreviousClose = data->close.size() > 1 ? data->close[data->close.size() - 2] : data->close.back();
    }

    return data;
}
