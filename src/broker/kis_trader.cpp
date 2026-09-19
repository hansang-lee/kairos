#include "broker/kis_trader.hpp"

#include <sstream>
#include <stdexcept>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "broker/kis_auth.hpp"

namespace {

/* KRW order prices are whole won, no decimals. */
std::string formatPrice(double price) {
    return std::to_string(static_cast<int64_t>(price));
}

/* KIS returns numbers as strings and leaves fields blank rather than sending "0". */
int64_t parseLong(const std::string& s) {
    try {
        return s.empty() ? 0 : std::stoll(s);
    } catch (const std::exception&) {
        return 0;
    }
}

double parseDouble(const std::string& s) {
    try {
        return s.empty() ? 0.0 : std::stod(s);
    } catch (const std::exception&) {
        return 0.0;
    }
}

/* Continuation keys come back space-padded to their fixed width. */
std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

}  // namespace

std::size_t KisTrader::writeCallback(void* contents, std::size_t size, std::size_t nmemb, void* userp) {
    auto* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

OrderResult KisTrader::placeOrder(OrderSide side, const std::string& ticker, int64_t quantity, double price) {
    OrderResult result;

    auto& auth = KisAuth::instance();
    auth.loadFromEnv();
    const std::string token = auth.getAccessToken();
    if (token.empty()) {
        result.message = "Failed to acquire access token.";
        return result;
    }

    const bool        isSell = (side == OrderSide::Sell);
    const std::string trId =
        auth.isPaper() ? (isSell ? "VTTC0011U" : "VTTC0012U") : (isSell ? "TTTC0011U" : "TTTC0012U");
    const bool isMarket = (price <= 0.0);

    nlohmann::json body;
    body["CANO"]              = auth.getAccountNo();
    body["ACNT_PRDT_CD"]      = auth.getAccountProd();
    body["PDNO"]              = ticker;
    body["ORD_DVSN"]          = isMarket ? "01" : "00";  // 01: market, 00: limit
    body["ORD_QTY"]           = std::to_string(quantity);
    body["ORD_UNPR"]          = isMarket ? "0" : formatPrice(price);
    body["EXCG_ID_DVSN_CD"]   = "KRX";
    body["SLL_TYPE"]          = isSell ? "01" : "";
    body["CNDT_PRIC"]         = "";
    const std::string bodyStr = body.dump();

    const std::string url = auth.getBaseUrl() + "/uapi/domestic-stock/v1/trading/order-cash";

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.message = "curl_easy_init failed.";
        return result;
    }

    std::string        response;
    struct curl_slist* headers = nullptr;
    headers                    = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
    headers                    = curl_slist_append(headers, ("authorization: Bearer " + token).c_str());
    headers                    = curl_slist_append(headers, ("appkey: " + auth.getAppKey()).c_str());
    headers                    = curl_slist_append(headers, ("appsecret: " + auth.getAppSecret()).c_str());
    headers                    = curl_slist_append(headers, ("tr_id: " + trId).c_str());
    headers                    = curl_slist_append(headers, "custtype: P");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        result.message = std::string("Order request failed: ") + curl_easy_strerror(res);
        return result;
    }

    try {
        const auto        j    = nlohmann::json::parse(response);
        const std::string rtCd = j.value("rt_cd", "1");
        result.message         = j.value("msg1", "");
        if (rtCd == "0" && j.contains("output")) {
            result.success   = true;
            result.orderNo   = j["output"].value("ODNO", "");
            result.orderTime = j["output"].value("ORD_TMD", "");
        }
    } catch (const std::exception& e) {
        result.message = std::string("JSON parse error: ") + e.what();
    }

    return result;
}

AccountBalance KisTrader::getBalance() {
    AccountBalance result;

    auto& auth = KisAuth::instance();
    auth.loadFromEnv();
    const std::string token = auth.getAccessToken();
    if (token.empty()) {
        result.message = "Failed to acquire access token.";
        return result;
    }

    const std::string trId = auth.isPaper() ? "VTTC8434R" : "TTTC8434R";

    // clang-format off
    std::ostringstream url;
    url << auth.getBaseUrl() << "/uapi/domestic-stock/v1/trading/inquire-balance"
        << "?CANO=" << auth.getAccountNo()
        << "&ACNT_PRDT_CD=" << auth.getAccountProd()
        << "&AFHR_FLPR_YN=N"
        << "&OFL_YN="
        << "&INQR_DVSN=02"
        << "&UNPR_DVSN=01"
        << "&FUND_STTL_ICLD_YN=N"
        << "&FNCG_AMT_AUTO_RDPT_YN=N"
        << "&PRCS_DVSN=00"
        << "&CTX_AREA_FK100="
        << "&CTX_AREA_NK100=";
    // clang-format on
    const std::string urlStr = url.str();

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.message = "curl_easy_init failed.";
        return result;
    }

    std::string        response;
    struct curl_slist* headers = nullptr;
    headers                    = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
    headers                    = curl_slist_append(headers, ("authorization: Bearer " + token).c_str());
    headers                    = curl_slist_append(headers, ("appkey: " + auth.getAppKey()).c_str());
    headers                    = curl_slist_append(headers, ("appsecret: " + auth.getAppSecret()).c_str());
    headers                    = curl_slist_append(headers, ("tr_id: " + trId).c_str());
    headers                    = curl_slist_append(headers, "custtype: P");

    curl_easy_setopt(curl, CURLOPT_URL, urlStr.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        result.message = std::string("Balance request failed: ") + curl_easy_strerror(res);
        return result;
    }

    try {
        const auto j = nlohmann::json::parse(response);
        if (j.value("rt_cd", "1") != "0" || !j.contains("output1") || !j.contains("output2")) {
            result.message = j.value("msg1", "Unknown error");
            return result;
        }

        for (const auto& item : j["output1"]) {
            const int64_t qty = std::stoll(item.value("hldg_qty", "0"));
            if (qty == 0) {
                continue;  // fully sold today; stays listed with qty 0 until D-2
            }
            StockHolding h;
            h.ticker           = item.value("pdno", "");
            h.name             = item.value("prdt_name", "");
            h.quantity         = qty;
            h.avgPrice         = std::stod(item.value("pchs_avg_pric", "0"));
            h.currentPrice     = std::stod(item.value("prpr", "0"));
            h.evalAmount       = std::stod(item.value("evlu_amt", "0"));
            h.profitLossAmount = std::stod(item.value("evlu_pfls_amt", "0"));
            h.profitLossRate   = std::stod(item.value("evlu_pfls_rt", "0"));
            result.holdings.push_back(h);
        }

        if (!j["output2"].empty()) {
            const auto& summary    = j["output2"][0];
            result.cashBalance     = std::stod(summary.value("dnca_tot_amt", "0"));
            result.totalEvalAmount = std::stod(summary.value("tot_evlu_amt", "0"));
        }

        result.success = true;
    } catch (const std::exception& e) {
        result.message = std::string("JSON parse error: ") + e.what();
    }

    return result;
}

FillHistory KisTrader::getDailyFills(const std::string& startYmd, const std::string& endYmd, bool filledOnly) {
    FillHistory result;

    auto& auth = KisAuth::instance();
    auth.loadFromEnv();
    const std::string token = auth.getAccessToken();
    if (token.empty()) {
        result.message = "Failed to acquire access token.";
        return result;
    }

    // Inner (within 3 months) lookup. The "before" variants (CTSC9215R / VTSC9215R)
    // would be needed for older ranges, which paper trading has no use for yet.
    const std::string trId = auth.isPaper() ? "VTTC0081R" : "TTTC0081R";
    const std::string from = startYmd;
    const std::string to   = endYmd.empty() ? startYmd : endYmd;

    // Paper accounts cap a page at 15 records, so a busy scalping day needs many pages.
    constexpr int kMaxPages = 60;
    std::string   fk100;
    std::string   nk100;

    for (int page = 0; page < kMaxPages; ++page) {
        // clang-format off
        std::ostringstream url;
        url << auth.getBaseUrl() << "/uapi/domestic-stock/v1/trading/inquire-daily-ccld"
            << "?CANO=" << auth.getAccountNo()
            << "&ACNT_PRDT_CD=" << auth.getAccountProd()
            << "&INQR_STRT_DT=" << from
            << "&INQR_END_DT=" << to
            << "&SLL_BUY_DVSN_CD=00"   // 00: both sides
            << "&INQR_DVSN=01"         // 01: oldest first
            << "&PDNO="
            << "&CCLD_DVSN=" << (filledOnly ? "01" : "00")
            << "&ORD_GNO_BRNO="
            << "&ODNO="
            << "&INQR_DVSN_3=00"
            << "&INQR_DVSN_1="
            << "&EXCG_ID_DVSN_CD=KRX"
            << "&CTX_AREA_FK100=" << fk100
            << "&CTX_AREA_NK100=" << nk100;
        // clang-format on
        const std::string urlStr = url.str();

        CURL* curl = curl_easy_init();
        if (!curl) {
            result.message = "curl_easy_init failed.";
            return result;
        }

        std::string        response;
        struct curl_slist* headers = nullptr;
        headers                    = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
        headers                    = curl_slist_append(headers, ("authorization: Bearer " + token).c_str());
        headers                    = curl_slist_append(headers, ("appkey: " + auth.getAppKey()).c_str());
        headers                    = curl_slist_append(headers, ("appsecret: " + auth.getAppSecret()).c_str());
        headers                    = curl_slist_append(headers, ("tr_id: " + trId).c_str());
        headers                    = curl_slist_append(headers, "custtype: P");
        headers                    = curl_slist_append(headers, page == 0 ? "tr_cont: " : "tr_cont: N");

        curl_easy_setopt(curl, CURLOPT_URL, urlStr.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            result.message = std::string("Fill history request failed: ") + curl_easy_strerror(res);
            return result;
        }

        try {
            const auto j = nlohmann::json::parse(response);
            if (j.value("rt_cd", "1") != "0") {
                result.message = j.value("msg1", "Unknown error");
                return result;
            }

            const std::size_t before = result.fills.size();
            if (j.contains("output1")) {
                for (const auto& item : j["output1"]) {
                    const std::string ticker = item.value("pdno", "");
                    if (ticker.empty()) {
                        continue;  // padding row
                    }
                    Fill f;
                    f.orderDate    = item.value("ord_dt", "");
                    f.orderTime    = item.value("ord_tmd", "");
                    f.orderNo      = item.value("odno", "");
                    f.ticker       = ticker;
                    f.name         = item.value("prdt_name", "");
                    f.side         = (item.value("sll_buy_dvsn_cd", "02") == "01") ? OrderSide::Sell : OrderSide::Buy;
                    f.orderQty     = parseLong(item.value("ord_qty", "0"));
                    f.filledQty    = parseLong(item.value("tot_ccld_qty", "0"));
                    f.orderPrice   = parseDouble(item.value("ord_unpr", "0"));
                    f.avgPrice     = parseDouble(item.value("avg_prvs", "0"));
                    f.filledAmount = parseDouble(item.value("tot_ccld_amt", "0"));
                    f.cancelled    = (item.value("cncl_yn", "N") == "Y");
                    result.fills.push_back(f);
                }
            }

            const std::string nextFk = trim(j.value("ctx_area_fk100", ""));
            const std::string nextNk = trim(j.value("ctx_area_nk100", ""));
            // Stop when the server hands back no continuation key, repeats the one we
            // just used, or returns a page that added nothing.
            if (nextNk.empty() || nextNk == nk100 || result.fills.size() == before) {
                break;
            }
            fk100 = nextFk;
            nk100 = nextNk;
        } catch (const std::exception& e) {
            result.message = std::string("JSON parse error: ") + e.what();
            return result;
        }
    }

    result.success = true;
    return result;
}
