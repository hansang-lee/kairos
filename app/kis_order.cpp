#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

#include "broker/kis_trader.hpp"

namespace {

void printUsage() {
    std::cout << "Usage:\n"
              << "  kis_order balance\n"
              << "  kis_order buy  <ticker> <qty> [price]   (price omitted or 0 = market order)\n"
              << "  kis_order sell <ticker> <qty> [price]\n";
}

int runBalance() {
    const auto balance = KisTrader::getBalance();
    if (!balance.success) {
        std::cerr << "[-] Failed to fetch balance: " << balance.message << std::endl;
        return 1;
    }

    std::cout << std::fixed << std::setprecision(0);
    std::cout << "예수금(Cash)       : " << balance.cashBalance << " KRW\n";
    std::cout << "총평가금액(Total)  : " << balance.totalEvalAmount << " KRW\n\n";

    if (balance.holdings.empty()) {
        std::cout << "보유 종목 없음.\n";
        return 0;
    }

    std::cout << std::left << std::setw(10) << "Ticker" << std::setw(16) << "Name" << std::right << std::setw(8)
              << "Qty" << std::setw(12) << "AvgPrice" << std::setw(12) << "Current" << std::setw(14) << "P/L Amt"
              << std::setw(10) << "P/L %" << "\n";
    for (const auto& h : balance.holdings) {
        std::cout << std::left << std::setw(10) << h.ticker << std::setw(16) << h.name << std::right << std::setw(8)
                  << h.quantity << std::setw(12) << h.avgPrice << std::setw(12) << h.currentPrice << std::setw(14)
                  << h.profitLossAmount << std::setprecision(2) << std::setw(9) << h.profitLossRate << "%"
                  << std::setprecision(0) << "\n";
    }
    return 0;
}

int runOrder(OrderSide side, int argc, char* argv[]) {
    if (argc < 4) {
        printUsage();
        return 1;
    }
    const std::string ticker = argv[2];
    const int64_t     qty    = std::stoll(argv[3]);
    const double      price  = (argc >= 5) ? std::stod(argv[4]) : 0.0;

    std::cout << "[*] " << (side == OrderSide::Buy ? "BUY " : "SELL ") << ticker << " x" << qty
              << (price > 0.0 ? (" @ " + std::to_string(static_cast<int64_t>(price)) + " KRW") : " (market)")
              << std::endl;

    const auto result = KisTrader::placeOrder(side, ticker, qty, price);
    if (!result.success) {
        std::cerr << "[-] Order failed: " << result.message << std::endl;
        return 1;
    }

    std::cout << "[+] Order accepted. No: " << result.orderNo << " at " << result.orderTime;
    if (!result.message.empty()) {
        std::cout << " (" << result.message << ")";
    }
    std::cout << std::endl;
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    const std::string cmd = argv[1];
    if (cmd == "balance") {
        return runBalance();
    }
    if (cmd == "buy") {
        return runOrder(OrderSide::Buy, argc, argv);
    }
    if (cmd == "sell") {
        return runOrder(OrderSide::Sell, argc, argv);
    }

    printUsage();
    return 1;
}
