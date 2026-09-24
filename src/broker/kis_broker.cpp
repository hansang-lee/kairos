#include "broker/ibroker.hpp"
#include "broker/kis_auth.hpp"
#include "broker/kis_trader.hpp"

OrderResult KisBroker::placeOrder(OrderSide side, const std::string& ticker, int64_t quantity, double price) {
    return KisTrader::placeOrder(side, ticker, quantity, price);
}

AccountBalance KisBroker::getBalance() {
    return KisTrader::getBalance();
}

FillHistory KisBroker::getDailyFills(const std::string& startYmd, const std::string& endYmd, bool filledOnly) {
    return KisTrader::getDailyFills(startYmd, endYmd, filledOnly);
}

std::string KisBroker::mode() const {
    auto& auth = KisAuth::instance();
    auth.loadFromEnv();
    return auth.isPaper() ? "paper" : "live";
}
