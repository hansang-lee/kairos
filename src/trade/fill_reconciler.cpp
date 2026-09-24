#include "trade/fill_reconciler.hpp"

namespace trade {

ReconcileResult reconcileFills(const TradeJournal& journal, const std::vector<Fill>& fills, const std::string& mode) {
    ReconcileResult result;
    if (fills.empty()) {
        return result;
    }

    const auto known    = journal.recordedFillKeys();
    const auto contexts = journal.orderContexts();

    for (const auto& f : fills) {
        // An order KIS accepted but has not filled yet is not a fill, and a cancelled
        // one never will be. Recording either would put a price in the journal that
        // nobody paid.
        if (f.cancelled || f.filledQty <= 0) {
            ++result.ignored;
            continue;
        }

        const std::string key = f.orderNo + ":" + std::to_string(f.filledQty);
        if (known.count(key) > 0) {
            ++result.alreadyKnown;
            continue;
        }

        JournalEntry e;
        e.event    = "fill";
        e.mode     = mode;
        e.dryRun   = false;
        e.ticker   = f.ticker;
        e.side     = (f.side == OrderSide::Sell) ? "SELL" : "BUY";
        e.quantity = f.filledQty;
        e.price    = f.avgPrice;
        e.orderNo  = f.orderNo;
        e.success  = true;
        e.message  = f.name;

        const auto it = contexts.find(f.orderNo);
        if (it != contexts.end()) {
            e.strategyId = it->second.strategyId;
            e.strategy   = it->second.strategy;
            e.category   = it->second.category;
            e.reason     = it->second.reason;
        } else {
            // Placed outside this system, or before the journal existed. Worth
            // recording anyway: the account moved, and a gap in the log is harder to
            // explain later than a row with no strategy on it.
            e.reason = "fill without a matching order in this journal";
        }

        if (journal.append(e)) {
            ++result.recorded;
        }
    }
    return result;
}

}  // namespace trade
