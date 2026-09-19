#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "broker/kis_auth.hpp"
#include "broker/kis_trader.hpp"
#include "trade/trade_journal.hpp"

namespace {

std::string todayKst() {
    const std::time_t  kst = std::time(nullptr) + 9 * 3600;
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&kst), "%Y%m%d");
    return oss.str();
}

void printUsage() {
    std::cout << "Usage:\n"
              << "  kis_order balance\n"
              << "  kis_order fills [YYYYMMDD] [YYYYMMDD]  (default: today; syncs into the trade journal)\n"
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

int runFills(int argc, char* argv[]) {
    const std::string from = (argc >= 3) ? argv[2] : todayKst();
    const std::string to   = (argc >= 4) ? argv[3] : from;

    const auto history = KisTrader::getDailyFills(from, to);
    if (!history.success) {
        std::cerr << "[-] Failed to fetch fill history: " << history.message << std::endl;
        return 1;
    }

    std::cout << "[*] " << from << " ~ " << to << ": " << history.fills.size() << " order(s)\n\n";
    if (history.fills.empty()) {
        return 0;
    }

    std::cout << std::fixed << std::setprecision(0);
    std::cout << std::left << std::setw(10) << "Date" << std::setw(8) << "Time" << std::setw(12) << "OrderNo"
              << std::setw(10) << "Ticker" << std::setw(6) << "Side" << std::right << std::setw(8) << "Qty"
              << std::setw(8) << "Filled" << std::setw(12) << "AvgPrice" << std::setw(14) << "Amount" << "\n";
    for (const auto& f : history.fills) {
        std::cout << std::left << std::setw(10) << f.orderDate << std::setw(8) << f.orderTime << std::setw(12)
                  << f.orderNo << std::setw(10) << f.ticker << std::setw(6)
                  << (f.side == OrderSide::Buy ? "BUY" : "SELL") << std::right << std::setw(8) << f.orderQty
                  << std::setw(8) << f.filledQty << std::setw(12) << f.avgPrice << std::setw(14) << f.filledAmount
                  << (f.cancelled ? "  (cancelled)" : "") << "\n";
    }

    // Append what KIS confirmed, so the journal carries real fill prices next to
    // the intent we recorded when the order was sent. Re-running is a no-op.
    const trade::TradeJournal journal;
    const auto                already = journal.recordedFillKeys();
    const std::string         mode    = KisAuth::instance().isPaper() ? "paper" : "live";

    int added = 0;
    for (const auto& f : history.fills) {
        if (f.filledQty == 0 || already.count(f.orderNo + ":" + std::to_string(f.filledQty))) {
            continue;
        }
        trade::JournalEntry entry;
        entry.event    = "fill";
        entry.mode     = mode;
        entry.ticker   = f.ticker;
        entry.side     = (f.side == OrderSide::Buy) ? "BUY" : "SELL";
        entry.quantity = f.filledQty;
        entry.price    = f.avgPrice;
        entry.reason   = f.cancelled ? "cancelled" : "filled";
        entry.orderNo  = f.orderNo;
        entry.success  = !f.cancelled;
        entry.message  = f.orderDate + " " + f.orderTime + " " + f.name;
        if (journal.append(entry)) {
            ++added;
        }
    }
    std::cout << "\n[+] Journal: " << added << " new fill record(s) -> " << journal.path() << std::endl;
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

    // Manual orders go into the same journal as automated ones — otherwise they
    // silently distort the per-strategy performance read from the account balance.
    trade::JournalEntry entry;
    entry.mode     = KisAuth::instance().isPaper() ? "paper" : "live";
    entry.ticker   = ticker;
    entry.side     = (side == OrderSide::Buy) ? "BUY" : "SELL";
    entry.quantity = qty;
    entry.price    = price;
    entry.reason   = "manual";
    entry.orderNo  = result.orderNo;
    entry.success  = result.success;
    entry.message  = result.message;
    trade::TradeJournal().append(entry);

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
    if (cmd == "fills") {
        return runFills(argc, argv);
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
