#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * @file broker_types.hpp
 *
 * The results a broker hands back, independent of which broker. They lived in
 * kis_trader.hpp, which meant that everything wanting to describe an order or a
 * balance — the executor, the journal, the bot — named KIS to do it. The Meritz
 * migration needs the same words with a different sender behind them.
 */

enum class OrderSide {
    Buy,
    Sell
};

struct OrderResult {
    bool        success = false;
    std::string orderNo;    ///< KIS order number (ODNO)
    std::string orderTime;  ///< Order accepted time, HHMMSS (ORD_TMD)
    std::string message;    ///< Server message (msg1) or local error description

    /**
     * @brief The request did not complete, so whether the order was placed is unknown.
     *
     * A timeout or dropped connection after the request was sent leaves the order
     * possibly accepted by KIS with the response lost. That is not the same as a
     * rejection, and retrying it blindly could double the position — so it is
     * distinguished, and the fill history is the only thing that can settle it.
     */
    bool indeterminate = false;
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
    double                    cashBalance     = 0.0;  ///< dnca_tot_amt (cash balance, 예수금총금액)
    double                    totalEvalAmount = 0.0;  ///< tot_evlu_amt (total account value, 총평가금액)
    std::string               message;
    std::vector<StockHolding> holdings;
};

/**
 * @brief One order as KIS recorded it, including how much of it actually filled.
 *
 * This is the authoritative side of the trade log: the journal knows why an
 * order was sent, KIS knows what it cost. Records are matched on orderNo.
 */
struct Fill {
    std::string orderDate;                      ///< ord_dt, YYYYMMDD
    std::string orderTime;                      ///< ord_tmd, HHMMSS
    std::string orderNo;                        ///< odno
    std::string ticker;                         ///< pdno
    std::string name;                           ///< prdt_name
    OrderSide   side         = OrderSide::Buy;  ///< sll_buy_dvsn_cd (01 sell, 02 buy)
    int64_t     orderQty     = 0;               ///< ord_qty
    int64_t     filledQty    = 0;               ///< tot_ccld_qty (0 = accepted but unfilled)
    double      orderPrice   = 0.0;             ///< ord_unpr (0 for market orders)
    double      avgPrice     = 0.0;             ///< avg_prvs, average fill price
    double      filledAmount = 0.0;             ///< tot_ccld_amt
    bool        cancelled    = false;           ///< cncl_yn == "Y"
};

struct FillHistory {
    bool              success = false;
    std::string       message;
    std::vector<Fill> fills;
};
