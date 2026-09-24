#include <ctime>
#include <iostream>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "yfinance.hpp"

namespace {

/**
 * @brief Fill OHLCV from a Yahoo "quote" object, dropping bars with no data.
 *
 * Yahoo returns null for a bucket in which nothing traded, which is routine in
 * intraday series. Reading the arrays wholesale throws a type_error on those, and
 * a bar with no prices has nothing to contribute anyway — so such indices are
 * dropped. The timestamp is dropped with them: keeping it would leave the series
 * misaligned by one from that point on, which no later code could detect.
 *
 * @return Number of bars dropped.
 */
std::size_t fillQuotes(const nlohmann::json& quote, const nlohmann::json& timestamps,
                       const std::shared_ptr<StockInfo>& data, const nlohmann::json& adjclose) {
    auto at = [](const nlohmann::json& arr, std::size_t i) -> const nlohmann::json* {
        return (arr.is_array() && i < arr.size()) ? &arr[i] : nullptr;
    };

    const auto& open   = quote.contains("open") ? quote["open"] : nlohmann::json::array();
    const auto& high   = quote.contains("high") ? quote["high"] : nlohmann::json::array();
    const auto& low    = quote.contains("low") ? quote["low"] : nlohmann::json::array();
    const auto& close  = quote.contains("close") ? quote["close"] : nlohmann::json::array();
    const auto& volume = quote.contains("volume") ? quote["volume"] : nlohmann::json::array();

    const std::size_t n       = timestamps.is_array() ? timestamps.size() : 0;
    std::size_t       dropped = 0;

    for (std::size_t i = 0; i < n; ++i) {
        const auto* c = at(close, i);
        if (c == nullptr || c->is_null()) {
            ++dropped;  // no trade in this bucket
            continue;
        }

        const auto* o = at(open, i);
        const auto* h = at(high, i);
        const auto* l = at(low, i);
        const auto* v = at(volume, i);

        const double closeVal = c->get<double>();

        // The whole bar is scaled by the same factor rather than only the close,
        // so a stop reading the low and a signal reading the close stay on one
        // series instead of half a total-return one and half a price one.
        const auto*  a      = at(adjclose, i);
        const double factor = (a && !a->is_null() && closeVal > 0.0) ? a->get<double>() / closeVal : 1.0;
        if (factor != 1.0) {
            data->adjustedForDistributions = true;
        }

        data->timestamps.push_back(timestamps[i].get<int64_t>());
        data->close.push_back(closeVal * factor);
        // A missing open/high/low on a bar that did trade falls back to the close
        // rather than dropping the bar, which would lose a real price.
        data->open.push_back(((o && !o->is_null()) ? o->get<double>() : closeVal) * factor);
        data->high.push_back(((h && !h->is_null()) ? h->get<double>() : closeVal) * factor);
        data->low.push_back(((l && !l->is_null()) ? l->get<double>() : closeVal) * factor);
        data->volume.push_back((v && !v->is_null()) ? v->get<int64_t>() : 0);
    }

    return dropped;
}

}  // namespace

void yFinance::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void yFinance::close() {
    curl_global_cleanup();
}

static time_t parseDateToTimestamp(const std::string& date) {
    std::tm tm = {};
    if (strptime(date.c_str(), "%Y-%m-%d", &tm) == nullptr) {
        return 0;
    }
    tm.tm_isdst = 0;
    return timegm(&tm);
}

std::shared_ptr<StockInfo> yFinance::getStockInfo(const std::string& ticker, const std::string& interval,
                                                  const std::string& range) {
    const auto fetched =
        fetch(std::string(url_base_) + ticker + "?interval=" + interval + "&range=" + range + "&events=div%7Csplit");
    if (fetched.empty()) {
        return nullptr;
    }

    const auto data = std::make_shared<StockInfo>();
    if (!data) {
        return nullptr;
    }

    try {
        const auto parsed = nlohmann::json::parse(fetched);
        if (!parsed.contains("chart") || !parsed["chart"].contains("result") || parsed["chart"]["result"].is_null()) {
            return nullptr;
        }

        const auto& result = parsed["chart"]["result"][0];
        const auto& meta   = result["meta"];

        data->ticker = ticker;

        if (meta.contains("currency")) {
            data->currency = meta["currency"];
        }

        if (meta.contains("exchangeName")) {
            data->exchangeName = meta["exchangeName"];
        }

        if (meta.contains("instrumentType")) {
            data->instrumentType = meta["instrumentType"];
        }

        if (meta.contains("regularMarketPrice")) {
            data->regularMarketPrice = meta["regularMarketPrice"];
        }

        if (meta.contains("chartPreviousClose")) {
            data->chartPreviousClose = meta["chartPreviousClose"];
        }

        if (meta.contains("firstTradeDate")) {
            data->firstTradeDate = meta["firstTradeDate"];
        }

        if (meta.contains("gmtoffset")) {
            data->gmtoffset = meta["gmtoffset"];
        }

        if (meta.contains("timezone")) {
            data->timezone = meta["timezone"];
        }

        if (result.contains("timestamp") && result.contains("indicators") && result["indicators"].contains("quote")
            && !result["indicators"]["quote"].empty()) {
            const auto& adj = (result["indicators"].contains("adjclose") && !result["indicators"]["adjclose"].empty()
                               && result["indicators"]["adjclose"][0].contains("adjclose"))
                                ? result["indicators"]["adjclose"][0]["adjclose"]
                                : nlohmann::json::array();
            fillQuotes(result["indicators"]["quote"][0], result["timestamp"], data, adj);
        } else if (result.contains("timestamp")) {
            data->timestamps = result["timestamp"].get<std::vector<int64_t>>();
        }
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "JSON error: " << e.what() << std::endl;
    }

    return data;
}

std::shared_ptr<StockInfo> yFinance::getStockInfo(const std::string& ticker, const std::string& startDate,
                                                  const std::string& endDate, const std::string& interval) {
    auto p1 = parseDateToTimestamp(startDate);
    auto p2 = parseDateToTimestamp(endDate);

    if (p1 == -1 || p2 == -1) {
        return nullptr;
    }

    p2 += 86400;

    // events=div|split makes Yahoo return the adjusted series alongside the raw one.
    const std::string url = std::string(url_base_) + ticker + "?period1=" + std::to_string(p1)
                          + "&period2=" + std::to_string(p2) + "&interval=" + interval + "&events=div%7Csplit";

    const auto fetched = fetch(url);
    if (fetched.empty()) {
        return nullptr;
    }

    const auto data = std::make_shared<StockInfo>();
    if (!data) {
        return nullptr;
    }

    try {
        const auto parsed = nlohmann::json::parse(fetched);
        if (!parsed.contains("chart") || !parsed["chart"].contains("result") || parsed["chart"]["result"].is_null()) {
            return nullptr;
        }

        const auto& result = parsed["chart"]["result"][0];
        const auto& meta   = result["meta"];

        data->ticker = ticker;

        if (meta.contains("currency")) {
            data->currency = meta["currency"];
        }

        if (meta.contains("exchangeName")) {
            data->exchangeName = meta["exchangeName"];
        }

        if (meta.contains("instrumentType")) {
            data->instrumentType = meta["instrumentType"];
        }

        if (meta.contains("regularMarketPrice")) {
            data->regularMarketPrice = meta["regularMarketPrice"];
        }

        if (meta.contains("chartPreviousClose")) {
            data->chartPreviousClose = meta["chartPreviousClose"];
        }

        if (meta.contains("firstTradeDate")) {
            data->firstTradeDate = meta["firstTradeDate"];
        }

        if (meta.contains("gmtoffset")) {
            data->gmtoffset = meta["gmtoffset"];
        }

        if (meta.contains("timezone")) {
            data->timezone = meta["timezone"];
        }

        if (result.contains("timestamp") && result.contains("indicators") && result["indicators"].contains("quote")
            && !result["indicators"]["quote"].empty()) {
            const auto& adj = (result["indicators"].contains("adjclose") && !result["indicators"]["adjclose"].empty()
                               && result["indicators"]["adjclose"][0].contains("adjclose"))
                                ? result["indicators"]["adjclose"][0]["adjclose"]
                                : nlohmann::json::array();
            fillQuotes(result["indicators"]["quote"][0], result["timestamp"], data, adj);
        } else if (result.contains("timestamp")) {
            data->timestamps = result["timestamp"].get<std::vector<int64_t>>();
        }
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "JSON error: " << e.what() << std::endl;
    }

    return data;
}

std::shared_ptr<FearAndGreedInfo> yFinance::getFearAndGreedIndex() {
    const auto fetched = fetch(std::string(cnn_url_base_), true);
    if (fetched.empty()) {
        return nullptr;
    }

    const auto data = std::make_shared<FearAndGreedInfo>();
    if (!data) {
        return nullptr;
    }

    try {
        const auto parsed = nlohmann::json::parse(fetched);
        if (!parsed.contains("fear_and_greed")) {
            return nullptr;
        }

        const auto& fng = parsed["fear_and_greed"];

        data->score         = fng.value("score", 0.0);
        data->rating        = fng.value("rating", "");
        data->timestamp     = fng.value("timestamp", "");
        data->previousClose = fng.value("previous_close", 0.0);
        data->previousWeek  = fng.value("previous_1_week", 0.0);
        data->previousMonth = fng.value("previous_1_month", 0.0);
        data->previousYear  = fng.value("previous_1_year", 0.0);

        if (parsed.contains("fear_and_greed_historical") && parsed["fear_and_greed_historical"].contains("data")) {
            for (const auto& item : parsed["fear_and_greed_historical"]["data"]) {
                data->timestamps.push_back(static_cast<int64_t>(item.value("x", 0.0) / 1000.0));
                data->scores.push_back(item.value("y", 0.0));
                data->ratings.push_back(item.value("rating", ""));
            }
        }
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "JSON error: " << e.what() << std::endl;
        return nullptr;
    }

    return data;
}

std::shared_ptr<FredSeriesInfo> yFinance::getFredSeries(const std::string& seriesId, const std::string& apiKey,
                                                        const std::string& observationStart,
                                                        const std::string& observationEnd,
                                                        const std::string& frequency) {
    std::string baseUrl =
        std::string(fred_url_base_) + "?series_id=" + seriesId + "&api_key=" + apiKey + "&file_type=json";

    if (!observationStart.empty()) {
        baseUrl += "&observation_start=" + observationStart;
    }
    if (!observationEnd.empty()) {
        baseUrl += "&observation_end=" + observationEnd;
    }

    std::string url = baseUrl;
    if (!frequency.empty()) {
        url += "&frequency=" + frequency;
    }

    auto fetched = fetch(url);
    if (fetched.empty()) {
        return nullptr;
    }

    const auto data = std::make_shared<FredSeriesInfo>();
    if (!data) {
        return nullptr;
    }

    try {
        auto parsed = nlohmann::json::parse(fetched);

        /* Retry without frequency if the series doesn't support it */
        if (parsed.contains("error_code") && !frequency.empty()) {
            const auto msg = parsed.value("error_message", "");
            if (msg.find("frequency") != std::string::npos) {
                fetched = fetch(baseUrl);
                if (fetched.empty()) {
                    return nullptr;
                }
                parsed = nlohmann::json::parse(fetched);
            }
        }

        if (parsed.contains("error_code")) {
            std::cerr << "FRED API error: " << parsed.value("error_message", "Unknown error") << std::endl;
            return nullptr;
        }

        if (!parsed.contains("observations")) {
            return nullptr;
        }

        data->seriesId = seriesId;

        for (const auto& obs : parsed["observations"]) {
            const auto dateStr  = obs.value("date", "");
            const auto valueStr = obs.value("value", "");

            /* skip missing data marked as "." */
            if (valueStr == "." || valueStr.empty()) {
                continue;
            }

            data->dates.push_back(dateStr);
            data->values.push_back(std::stod(valueStr));
        }
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "JSON error: " << e.what() << std::endl;
        return nullptr;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return nullptr;
    }

    return data;
}

std::string yFinance::fetch(const std::string& url, bool is_cnn) {
    CURL*    curl = nullptr;
    CURLcode res  = CURLE_OK;

    std::string buffer("");

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        // Without these a hung connection blocks the process indefinitely: the
        // scalping loop would stop polling and stop answering SIGTERM, and a
        // one-shot run would be killed by systemd part-way through.
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT,
                         "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
                         "Chrome/120.0.0.0 Safari/537.36");

        if (is_cnn) {
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "Referer: https://www.cnn.com/markets/fear-and-greed");
            headers = curl_slist_append(headers, "Accept: application/json");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            res = curl_easy_perform(curl);
            curl_slist_free_all(headers);
        } else {
            res = curl_easy_perform(curl);
        }

        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        }
        curl_easy_cleanup(curl);
    }
    return buffer;
}

std::size_t yFinance::write(void* contents, std::size_t size, std::size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}
