#include "trade/signal_executor.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

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

/** Current KST time as HHMM (UTC+9 shift then gmtime — no TZ database needed). */
int kstHhmm() {
    const std::time_t kst   = std::time(nullptr) + 9 * 3600;
    const std::tm*    tmPtr = std::gmtime(&kst);
    return tmPtr->tm_hour * 100 + tmPtr->tm_min;
}

/** Parse "HHMM" to an int; -1 when empty or malformed, meaning "no bound". */
int parseHhmm(const std::string& s) {
    if (s.size() != 4) {
        return -1;
    }
    try {
        return std::stoi(s);
    } catch (const std::exception&) {
        return -1;
    }
}

std::string pctString(double v) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << v;
    return oss.str();
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

std::string SignalExecutor::entryBlockReason(const PositionState& state) const {
    if (const int start = parseHhmm(profile_.tradeStart); start >= 0 && kstHhmm() < start) {
        return "outside trade window (before " + profile_.tradeStart + ")";
    }
    if (const int end = parseHhmm(profile_.tradeEnd); end >= 0 && kstHhmm() > end) {
        return "outside trade window (after " + profile_.tradeEnd + ")";
    }
    if (profile_.cooldownMinutes > 0 && state.lastExitTs > 0) {
        const int64_t elapsed = static_cast<int64_t>(std::time(nullptr)) - state.lastExitTs;
        const int64_t window  = static_cast<int64_t>(profile_.cooldownMinutes) * 60;
        if (elapsed < window) {
            std::ostringstream oss;
            oss << "cooldown (" << ((window - elapsed + 59) / 60) << " min left of " << profile_.cooldownMinutes << ")";
            return oss.str();
        }
    }
    return "";
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
    const int64_t       heldQty = holding ? holding->quantity : 0;

    // Reconcile local state with the broker before reading anything from it, so a
    // position closed by hand cannot leave a stale peak behind.
    const PositionState& state = positions_.sync(profile_.ticker, heldQty, price);
    d.heldQty                  = heldQty;
    d.heldAvgPrice             = holding ? holding->avgPrice : 0.0;
    d.peakPrice                = state.peakPrice;
    d.entryTranchesDone        = state.entryTranches;

    // Forced exits leave the whole position at once — scaling out of a stop defeats
    // the point of having one. Only a strategy SELL is allowed to be staged.
    bool forcedExit = false;

    if (holding) {
        const double avg = holding->avgPrice;
        if (profile_.stopLossPct > 0.0 && price <= avg * (1.0 - profile_.stopLossPct / 100.0)) {
            d.acted = forcedExit = true;
            d.reason             = "STOP-LOSS (-" + pctString(profile_.stopLossPct) + "%)";
        } else if (profile_.trailingStopPct > 0.0 && state.peakPrice > 0.0
                   && price <= state.peakPrice * (1.0 - profile_.trailingStopPct / 100.0)) {
            d.acted = forcedExit = true;
            d.reason             = "TRAILING-STOP (-" + pctString(profile_.trailingStopPct) + "% from peak "
                     + std::to_string(static_cast<int64_t>(state.peakPrice)) + ")";
        } else if (profile_.takeProfitPct > 0.0 && price >= avg * (1.0 + profile_.takeProfitPct / 100.0)) {
            d.acted = forcedExit = true;
            d.reason             = "TAKE-PROFIT (+" + pctString(profile_.takeProfitPct) + "%)";
        } else if (signal == Signal::SELL) {
            d.acted  = true;
            d.reason = "signal SELL";
        }
        if (d.acted) {
            d.side = "SELL";
        }
    }

    // A buy can add to an existing position until the entry plan is filled.
    if (!d.acted && signal == Signal::BUY && state.entryTranches < profile_.entryTranches) {
        d.acted  = true;
        d.side   = "BUY";
        d.reason = profile_.entryTranches > 1 ? ("signal BUY (tranche " + std::to_string(state.entryTranches + 1) + "/"
                                                 + std::to_string(profile_.entryTranches) + ")")
                                              : "signal BUY";
    }

    if (!d.acted) {
        return d;
    }

    const bool isBuy = (d.side == "BUY");

    // Size the order before deciding whether to send it, so the journal records
    // what a dry run or a blocked round would have done.
    if (isBuy) {
        // Each tranche is sized from the cash available right now, so a staged entry
        // lands slightly under position_pct rather than over it.
        const double allocCash = balance.cashBalance * profile_.positionPct / profile_.entryTranches;
        d.quantity             = std::max<int64_t>(1, static_cast<int64_t>(allocCash / price));
    } else if (forcedExit || profile_.exitTranches <= 1) {
        d.quantity = heldQty;
    } else {
        // Split what is left over the tranches still to go, so the final one clears
        // the position exactly instead of leaving a remainder behind.
        const int remaining = std::max(1, profile_.exitTranches - state.exitTranches);
        d.quantity          = std::max<int64_t>(1, heldQty / remaining);
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

    const OrderSide side    = isBuy ? OrderSide::Buy : OrderSide::Sell;
    const auto      verdict = risk_.check(side, balance);

    // Entry-side gates (window, cooldown) never apply to an exit: refusing to close
    // a position is the one thing these rules must not do.
    const std::string entryBlock = isBuy ? entryBlockReason(state) : "";

    if (maxOrders_ >= 0 && ordersSent_ >= maxOrders_) {
        d.skipped     = true;
        entry.event   = "skip";
        entry.message = "order cap reached this session";
    } else if (!entryBlock.empty()) {
        d.skipped     = true;
        d.blockedBy   = entryBlock;
        entry.event   = "skip";
        entry.message = entryBlock;
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
        if (d.order.success) {
            if (isBuy) {
                positions_.recordEntryTranche(profile_.ticker);
            } else {
                positions_.recordExitTranche(profile_.ticker);
            }
        }
        entry.orderNo = d.order.orderNo;
        entry.success = d.order.success;
        entry.message = d.order.message;
    }

    journal_.append(entry);

    // Only real activity is worth a push: a dry run that would have traded, or a
    // limit blocking a run that never intended to trade, is noise.
    if (live_ && (d.sent || d.skipped)) {
        std::ostringstream msg;
        msg << (d.sent ? (d.order.success ? "[ORDER] " : "[ORDER FAILED] ") : "[BLOCKED] ") << d.side << " "
            << profile_.ticker << " x" << d.quantity << " @ " << static_cast<int64_t>(price) << " KRW\n"
            << profile_.name << " (#" << profile_.id << ")\n"
            << "reason: " << d.reason;
        if (!entry.message.empty()) {
            msg << "\n" << entry.message;
        }
        notify_.send(msg.str());
    }

    return d;
}

}  // namespace trade
