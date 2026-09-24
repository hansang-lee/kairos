#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "stock_info.hpp"

struct IDataProvider {
    virtual ~IDataProvider()                       = default;
    [[nodiscard]] virtual std::string name() const = 0;

    [[nodiscard]] virtual std::shared_ptr<StockInfo> getStockInfo(std::string_view ticker, std::string_view startDate,
                                                                  std::string_view endDate,
                                                                  std::string_view interval = "1d") = 0;

    /**
     * @brief The price the ticker is trading at right now; 0 when unavailable.
     *
     * A daily bar's close is not a live price. When the exchange has not yet
     * published today's bar, the last close in a series is yesterday's, and an
     * order sized and journaled against it records a day's move as slippage.
     * Defaulted so a provider with no quote feed still compiles; callers fall back
     * to the last close and say so.
     */
    [[nodiscard]] virtual double getCurrentPrice(std::string_view ticker) {
        (void)ticker;
        return 0.0;
    }
};
