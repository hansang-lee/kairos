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
};
