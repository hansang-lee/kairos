#pragma once

#include <cstdint>
#include <string>

#include "broker/broker_types.hpp"

/**
 * @brief The three things the trading path needs from a broker, and nothing else.
 *
 * Until this existed the executor called KisTrader's static functions directly,
 * which had two costs. The stated plan is to trade for real through Meritz, and
 * that could not happen without editing the executor. And the executor's live
 * branch — the one that actually sends money — had never been unit-tested,
 * because there was no seam to put a fake behind. Every test ran dry.
 *
 * Deliberately narrow: quotes stay on IDataProvider, and the order-history
 * pagination stays the broker's own problem behind getDailyFills.
 */
struct IBroker {
    virtual ~IBroker() = default;

    /**
     * @param price 0 places a market order; anything else is a limit price.
     */
    [[nodiscard]] virtual OrderResult placeOrder(OrderSide side, const std::string& ticker, int64_t quantity,
                                                 double price = 0.0) = 0;

    [[nodiscard]] virtual AccountBalance getBalance() = 0;

    /**
     * @param startYmd Inclusive, "YYYYMMDD" or "YYYY-MM-DD".
     * @param endYmd   Inclusive; empty means startYmd.
     * @param filledOnly Only orders that filled at least partly.
     */
    [[nodiscard]] virtual FillHistory getDailyFills(const std::string& startYmd, const std::string& endYmd = "",
                                                    bool filledOnly = true) = 0;

    /** @brief "paper" or "live" — what the journal stamps on every entry. */
    [[nodiscard]] virtual std::string mode() const = 0;
};

/**
 * @brief KIS behind the IBroker seam.
 *
 * A forwarding shell over KisTrader's static API, which the CLI tools keep using
 * directly. The trading path goes through this so it can also go through a fake.
 */
class KisBroker: public IBroker {
   public:
    [[nodiscard]] OrderResult    placeOrder(OrderSide side, const std::string& ticker, int64_t quantity,
                                            double price = 0.0) override;
    [[nodiscard]] AccountBalance getBalance() override;
    [[nodiscard]] FillHistory    getDailyFills(const std::string& startYmd, const std::string& endYmd = "",
                                               bool filledOnly = true) override;
    [[nodiscard]] std::string    mode() const override;
};
