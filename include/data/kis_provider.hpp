#pragma once

#include "broker/kis_auth.hpp"
#include "data/idata_provider.hpp"

class KisProvider: public IDataProvider {
   public:
    KisProvider()           = default;
    ~KisProvider() override = default;

    [[nodiscard]] std::string name() const override { return "KisProvider"; }

    [[nodiscard]] std::shared_ptr<StockInfo> getStockInfo(std::string_view ticker, std::string_view startDate,
                                                          std::string_view endDate,
                                                          std::string_view interval = "1d") override;

    /**
     * @brief Fetch today's 1-minute bars for a KRX ticker (up to the most recent ~30).
     *
     * KIS only exposes the current trading day's minute bars — no history. Intended
     * for live polling during market hours, not backtesting.
     * @param ticker    6-digit KRX ticker.
     * @param asOfTime  HHMMSS to query "as of" (empty = now).
     * @return          StockInfo with up to ~30 minute bars, oldest-first, or nullptr on failure.
     */
    [[nodiscard]] std::shared_ptr<StockInfo> getIntradayBars(std::string_view ticker, std::string_view asOfTime = "");

    /**
     * @brief Current price from the quote endpoint (KIS 주식현재가 시세, FHKST01010100).
     *
     * Outside market hours this is the last close, which is the same number the
     * daily series ends with — the value of the call is during the session, where
     * the two differ by the day's move so far.
     * @return stck_prpr as a double, or 0.0 on any failure.
     */
    [[nodiscard]] double getCurrentPrice(std::string_view ticker) override;
};
