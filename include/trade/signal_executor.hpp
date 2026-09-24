#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "broker/ibroker.hpp"
#include "notify/telegram.hpp"
#include "strategy/istrategy.hpp"
#include "strategy/strategy_factory.hpp"
#include "trade/position_store.hpp"
#include "trade/risk_guard.hpp"
#include "trade/trade_journal.hpp"

namespace trade {

/** What one evaluation round decided, for the caller to log in its own format. */
struct Decision {
    Signal  signal            = Signal::HOLD;
    double  price             = 0.0;  ///< price the order was sized against
    int64_t heldQty           = 0;    ///< shares currently held, per the account balance
    double  heldAvgPrice      = 0.0;
    double  peakPrice         = 0.0;  ///< highest price seen since entry (trailing stop reference)
    int     entryTranchesDone = 0;    ///< buy tranches already filled for this position

    bool        acted = false;  ///< an order was warranted by the signal or the stop
    std::string side;           ///< "BUY" / "SELL", empty when acted == false
    std::string reason;         ///< "signal BUY", "signal SELL", "STOP-LOSS"
    int64_t     quantity = 0;

    bool        sent    = false;  ///< the order was actually transmitted to KIS
    bool        skipped = false;  ///< suppressed by the per-session cap or a risk limit
    std::string blockedBy;        ///< risk limit that suppressed it, empty otherwise
    OrderResult order;
};

/**
 * @brief State shared by every executor in a process.
 *
 * The risk guard is account-wide and the position store is keyed by ticker, so
 * both describe the account rather than one strategy. Giving each executor its
 * own copy would have them overwrite each other's file — the day's order count
 * and other tickers' peaks would vanish on every save — so a multi-profile loop
 * must build one context and hand it to all of them.
 */
struct ExecutionContext {
    std::shared_ptr<RiskGuard>     risk;
    std::shared_ptr<PositionStore> positions;
    std::shared_ptr<TradeJournal>  journal;

    /**
     * @brief Where orders go. KIS in production; a fake in tests.
     *
     * Shared like the rest of the context because it describes the account, not a
     * strategy: every executor in the process sends to the same place.
     */
    std::shared_ptr<IBroker> broker;

    /** @brief Build a context with the default file locations. */
    static ExecutionContext create(const RiskLimits& limits);
};

/**
 * @brief Turns one strategy signal into at most one order against the KIS account.
 *
 * Shared by the intraday (scalp_trade) and daily (daily_trade) loops so their
 * position-sizing and stop-loss rules cannot drift apart. The account balance is
 * the only source of truth for what is held — no local position state is kept,
 * so restarting a loop cannot double-buy.
 *
 * Every decision is journaled, including dry runs and orders suppressed by the
 * cap, because an order that was never sent is exactly the thing no other record
 * would remember.
 */
class SignalExecutor {
   public:
    /**
     * @param profile   Portfolio profile driving this execution (sizing, stop-loss, attribution).
     * @param live      false (default) logs and journals the decision without sending it.
     * @param maxOrders Cap on orders sent by this executor; -1 (default) means no cap.
     */
    explicit SignalExecutor(const StrategyProfile& profile, bool live = false, int maxOrders = -1,
                            const RiskLimits& limits = {});

    /**
     * @brief Executor sharing account-wide state with others in the same process.
     * @param context Shared risk guard, position store and journal.
     */
    SignalExecutor(const StrategyProfile& profile, bool live, int maxOrders, ExecutionContext context);

    /**
     * @param signal  Strategy output for the bar about to be executed.
     * @param price   Current price the order would fill near.
     * @param balance Freshly fetched account balance (source of truth for holdings).
     */
    Decision execute(Signal signal, double price, const AccountBalance& balance);

    [[nodiscard]] int                     ordersSent() const { return ordersSent_; }
    [[nodiscard]] const TradeJournal&     journal() const { return *ctx_.journal; }
    [[nodiscard]] const RiskGuard&        risk() const { return *ctx_.risk; }
    [[nodiscard]] const PositionStore&    positions() const { return *ctx_.positions; }
    [[nodiscard]] const notify::Telegram& notifier() const { return notify_; }

   private:
    /** @brief Why a new entry is not allowed right now (window, cooldown); empty if it is. */
    [[nodiscard]] std::string entryBlockReason(const PositionState& state) const;

    const StrategyProfile& profile_;
    bool                   live_;
    int                    maxOrders_;
    int                    ordersSent_ = 0;
    ExecutionContext       ctx_;
    notify::Telegram       notify_;
    std::string            mode_;
};

}  // namespace trade
