#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class OrderSide {
    Buy,
    Sell
};

struct OrderResult {
    bool        success = false;
    std::string orderNo;    ///< KIS order number (ODNO)
    std::string orderTime;  ///< Order accepted time, HHMMSS (ORD_TMD)
    std::string message;    ///< Server message (msg1) or local error description
};

struct StockHolding {
    std::string ticker;                  ///< pdno
    std::string name;                    ///< prdt_name
    int64_t     quantity         = 0;    ///< hldg_qty
    double      avgPrice         = 0.0;  ///< pchs_avg_pric
    double      currentPrice     = 0.0;  ///< prpr
    double      evalAmount       = 0.0;  ///< evlu_amt
    double      profitLossAmount = 0.0;  ///< evlu_pfls_amt
    double      profitLossRate   = 0.0;  ///< evlu_pfls_rt (%)
};

struct AccountBalance {
    bool                      success         = false;
    double                    cashBalance     = 0.0;  ///< dnca_tot_amt (예수금총금액)
    double                    totalEvalAmount = 0.0;  ///< tot_evlu_amt (총평가금액)
    std::string               message;
    std::vector<StockHolding> holdings;
};

/**
 * @brief KIS domestic-stock cash order placement and balance inquiry.
 *
 * Reads credentials via KisAuth::instance() (paper vs real mode follows
 * KisAuth's KIS_MODE env setting — defaults to paper/모의투자).
 */
class KisTrader {
   public:
    /**
     * @brief Place a domestic stock cash order (주식주문 현금).
     * @param side     Buy or Sell.
     * @param ticker   6-digit KRX ticker (e.g. "005930").
     * @param quantity Number of shares.
     * @param price    Limit price in KRW. 0 (default) places a market order.
     */
    [[nodiscard]] static OrderResult placeOrder(OrderSide side, const std::string& ticker, int64_t quantity,
                                                double price = 0.0);

    /**
     * @brief Fetch cash balance and per-stock holdings for the configured account.
     */
    [[nodiscard]] static AccountBalance getBalance();

   private:
    static std::size_t writeCallback(void* contents, std::size_t size, std::size_t nmemb, void* userp);
};
