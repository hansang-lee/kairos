#include "trade/signal_executor.hpp"

#include <algorithm>

#include "broker/kis_auth.hpp"

namespace trade {

namespace {

const StockHolding* findHolding(const AccountBalance& balance, const std::string& ticker) {
    for (const auto& h : balance.holdings) {
        if (h.ticker == ticker) {
            return &h;
        }
    }
    return nullptr;
}

}  // namespace

SignalExecutor::SignalExecutor(const StrategyProfile& profile, bool live, int maxOrders, const RiskLimits& limits)
    : profile_(profile)
    , live_(live)
    , maxOrders_(maxOrders)
    , risk_(limits) {
    KisAuth::instance().loadFromEnv();
    mode_ = KisAuth::instance().isPaper() ? "paper" : "live";
}

Decision SignalExecutor::execute(Signal signal, double price, const AccountBalance& balance) {
    Decision d;
    d.signal = signal;
    d.price  = price;

    // A failed balance fetch must not be read as "flat" — that would re-buy a
    // position we already hold, so the round is skipped entirely.
    if (!balance.success) {
        return d;
    }

    const StockHolding* holding = findHolding(balance, profile_.ticker);
    if (holding) {
        d.heldQty      = holding->quantity;
        d.heldAvgPrice = holding->avgPrice;
    }

    // The stop is checked before the signal: a position that has fallen through
    // its stop is closed whether or not the strategy wants out yet.
    if (holding && profile_.stopLossPct > 0.0 && price <= holding->avgPrice * (1.0 - profile_.stopLossPct / 100.0)) {
        d.acted  = true;
        d.side   = "SELL";
        d.reason = "STOP-LOSS";
    } else if (signal == Signal::BUY && !holding) {
        d.acted  = true;
        d.side   = "BUY";
        d.reason = "signal BUY";
    } else if (signal == Signal::SELL && holding) {
        d.acted  = true;
        d.side   = "SELL";
        d.reason = "signal SELL";
    }

    if (!d.acted) {
        return d;
    }

    // Size the order before deciding whether to send it, so the journal records
    // what a dry run or a capped round would have done.
    if (d.side == "BUY") {
        const double allocCash = balance.cashBalance * profile_.positionPct;
        d.quantity             = std::max<int64_t>(1, static_cast<int64_t>(allocCash / price));
    } else {
        d.quantity = holding ? holding->quantity : 0;
    }

    JournalEntry entry;
    entry.mode       = mode_;
    entry.dryRun     = !live_;
    entry.strategyId = profile_.id;
    entry.strategy   = profile_.name;
    entry.category   = profile_.category;
    entry.ticker     = profile_.ticker;
    entry.side       = d.side;
    entry.quantity   = d.quantity;
    entry.price      = price;
    entry.reason     = d.reason;

    const OrderSide side    = (d.side == "BUY") ? OrderSide::Buy : OrderSide::Sell;
    const auto      verdict = risk_.check(side, balance);

    if (maxOrders_ >= 0 && ordersSent_ >= maxOrders_) {
        d.skipped     = true;
        entry.event   = "skip";
        entry.message = "order cap reached this session";
    } else if (!verdict.allowed) {
        d.skipped     = true;
        d.blockedBy   = verdict.reason;
        entry.event   = "skip";
        entry.message = verdict.reason;
    } else if (live_) {
        d.order = KisTrader::placeOrder(side, profile_.ticker, d.quantity);
        d.sent  = true;
        ++ordersSent_;
        risk_.recordOrder();
        entry.orderNo = d.order.orderNo;
        entry.success = d.order.success;
        entry.message = d.order.message;
    }

    journal_.append(entry);
    return d;
}

}  // namespace trade
