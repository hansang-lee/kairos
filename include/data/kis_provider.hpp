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
};
