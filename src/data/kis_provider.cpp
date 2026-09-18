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
 *        bars per call, regardless of the requested date range). Retries with
 *        backoff on KIS's per-second rate-limit error (EGW00201) instead of
 *        silently treating a throttled call as "no more data."
 * @return Bars in chronological (oldest-first) order, or empty on failure/no data.
 */
std::vector<DayBar> fetchChunk(const std::string& ticker, const std::string& startYmd, const std::string& endYmd,
                               const std::string& periodCode) {
    auto& auth = KisAuth::instance();
    auth.loadFromEnv();
    const std::string token = auth.getAccessToken();
    if (token.empty()) {
        std::cerr << "KisProvider: Failed to acquire access token." << std::endl;
        return {};
    }

    constexpr int kMaxAttempts = 5;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        const std::string response = fetchOnce(ticker, startYmd, endYmd, periodCode, token, auth);
        if (response.empty()) {
            return {};  // transport-level failure already logged by fetchOnce
        }

        try {
            const auto json = nlohmann::json::parse(response);
            if (json.value("msg_cd", "") == "EGW00201") {
                // Per-second rate limit; back off and retry rather than giving up.
                const auto backoff = std::chrono::milliseconds(1500 * (attempt + 1));
                std::this_thread::sleep_for(backoff);
                continue;
            }
            if (!json.contains("output2") || !json["output2"].is_array()) {
                std::cerr << "KisProvider error response: " << response << std::endl;
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
            return {};
        }
    }

    std::cerr << "KisProvider: gave up after " << kMaxAttempts << " rate-limit retries." << std::endl;
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
        auto chunk = fetchChunk(tickerStr, normStart, currentEnd, periodCode);
        if (chunk.empty()) {
            break;
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
