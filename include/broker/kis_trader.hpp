#pragma once

#include "broker/broker_types.hpp"

/**
 * @brief KIS domestic-stock cash order placement and balance inquiry.
 *
 * Reads credentials via KisAuth::instance() (paper vs real mode follows
 * KisAuth's KIS_MODE env setting — defaults to paper trading / 모의투자).
 */
class KisTrader {
   public:
    /**
     * @brief Place a domestic stock cash order (KIS: 주식주문 현금).
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

    /**
     * @brief Fetch order/fill history (KIS: 주식일별주문체결조회) for a date range.
     *
     * Paper accounts return only 15 records per call, so the continuation keys are
     * followed until the server stops handing one back. KIS recommends querying
     * after 15:30 KST — same-day results before the close may still be incomplete.
     *
     * @param startYmd Inclusive start date, "YYYYMMDD" or "YYYY-MM-DD".
     * @param endYmd   Inclusive end date, same forms. Empty (default) means startYmd.
     * @param filledOnly Return only orders with a fill (CCLD_DVSN=01) instead of all.
     */
    [[nodiscard]] static FillHistory getDailyFills(const std::string& startYmd, const std::string& endYmd = "",
                                                   bool filledOnly = true);

   private:
    static std::size_t writeCallback(void* contents, std::size_t size, std::size_t nmemb, void* userp);
};
