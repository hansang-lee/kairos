#include "data/kis_provider.hpp"

#include <algorithm>
#include <ctime>
#include <iostream>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

static std::string normalizeDate(std::string_view date) {
    std::string result;
    for (char c : date) {
        if (c >= '0' && c <= '9') {
            result.push_back(c);
        }
    }
    return result;
}

static int64_t parseKisDateToTimestamp(const std::string& yyyymmdd) {
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

std::size_t KisProvider::writeCallback(void* contents, std::size_t size, std::size_t nmemb, void* userp) {
    auto* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

std::shared_ptr<StockInfo> KisProvider::getStockInfo(std::string_view ticker, std::string_view startDate,
                                                     std::string_view endDate, std::string_view interval) {
    auto& auth = KisAuth::instance();
    auth.loadFromEnv();
    const std::string token = auth.getAccessToken();
    if (token.empty()) {
        std::cerr << "KisProvider: Failed to acquire access token." << std::endl;
        return nullptr;
    }

    const std::string normStart = normalizeDate(startDate);
    const std::string normEnd   = normalizeDate(endDate);

    std::string periodCode = "D";
    if (interval == "1wk" || interval == "W") {
        periodCode = "W";
    } else if (interval == "1mo" || interval == "M") {
        periodCode = "M";
    }

    const std::string url = auth.getBaseUrl()
                          + "/uapi/domestic-stock/v1/quotations/inquire-daily-itemchartprice"
                            "?FID_COND_MRKT_DIV_CODE=J"
                            "&FID_INPUT_ISCD="
                          + std::string(ticker) + "&FID_INPUT_DATE_1=" + normStart + "&FID_INPUT_DATE_2=" + normEnd
                          + "&FID_PERIOD_DIV_CODE=" + periodCode + "&FID_ORG_ADJ_PRC=0";

    CURL* curl = curl_easy_init();
    if (!curl) {
        return nullptr;
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
        return nullptr;
    }

    try {
        const auto json = nlohmann::json::parse(response);
        if (!json.contains("output2") || !json["output2"].is_array()) {
            std::cerr << "KisProvider error response: " << response << std::endl;
            return nullptr;
        }

        auto data            = std::make_shared<StockInfo>();
        data->ticker         = std::string(ticker);
        data->currency       = "KRW";
        data->exchangeName   = "KRX";
        data->instrumentType = "COMMON_STOCK";
        data->timezone       = "Asia/Seoul";

        const auto& list = json["output2"];
        // KIS returns rows in reverse chronological order (newest first).
        // For backtesting, we need chronological order (oldest first).
        std::vector<nlohmann::json> reversedRows(list.begin(), list.end());
        std::reverse(reversedRows.begin(), reversedRows.end());

        for (const auto& item : reversedRows) {
            const std::string dateStr = item.value("stck_bsop_date", "");
            if (dateStr.empty()) {
                continue;
            }

            const double  op = std::stod(item.value("stck_oprc", "0"));
            const double  hp = std::stod(item.value("stck_hgpr", "0"));
            const double  lp = std::stod(item.value("stck_lwpr", "0"));
            const double  cp = std::stod(item.value("stck_clpr", "0"));
            const int64_t vl = std::stoll(item.value("acml_vol", "0"));

            data->timestamps.push_back(parseKisDateToTimestamp(dateStr));
            data->open.push_back(op);
            data->high.push_back(hp);
            data->low.push_back(lp);
            data->close.push_back(cp);
            data->volume.push_back(vl);
        }

        if (!data->close.empty()) {
            data->regularMarketPrice = data->close.back();
            data->chartPreviousClose =
                data->close.size() > 1 ? data->close[data->close.size() - 2] : data->close.back();
        }

        return data;
    } catch (const std::exception& e) {
        std::cerr << "KisProvider parse exception: " << e.what() << std::endl;
    }

    return nullptr;
}
