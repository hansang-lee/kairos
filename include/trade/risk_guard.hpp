#pragma once

#include <string>

#include "broker/kis_trader.hpp"

namespace trade {

/** Limits that stop a bad day from compounding. 0 / negative disables a limit. */
struct RiskLimits {
    /**
     * Halt buying once equity falls this far below its reference. The reference is
     * the higher of the day's opening equity and the previous session's last known
     * equity — see RiskGuard for why both are needed.
     */
    double dailyLossLimitPct = 0.0;
    int    maxOrdersPerDay   = 0;  ///< cap on orders sent per calendar day, across restarts
};

struct RiskVerdict {
    bool        allowed = true;
    std::string reason;  ///< why it was blocked, empty when allowed
};

/**
 * @brief Per-day trading limits, persisted so a process restart cannot reset them.
 *
 * The day's opening equity is recorded the first time the guard sees a balance,
 * and the loss limit is measured against it. State lives in data/risk_state.json
 * and rolls over automatically on the first call of a new KST day.
 *
 * Sells are never blocked. A daily loss limit that prevents closing a losing
 * position would do the opposite of what it exists for — only new exposure is
 * stopped.
 */
class RiskGuard {
   public:
    /**
     * @param limits Limits to enforce.
     * @param path   State file. Empty (default) resolves to <project-root>/data/risk_state.json.
     */
    explicit RiskGuard(const RiskLimits& limits, const std::string& path = "");

    /**
     * @brief Decide whether an order may be sent, recording the day's opening equity
     *        on first use.
     * @param side    Buy is subject to every limit; Sell only to none.
     * @param balance Freshly fetched balance — its total evaluation is the equity measure.
     */
    [[nodiscard]] RiskVerdict check(OrderSide side, const AccountBalance& balance);

    /**
     * @brief Record the day's opening equity from a balance, without judging an order.
     *
     * check() only runs when an order is being considered, which could be hours
     * into the session — the loss limit would then measure from an already-fallen
     * account. Every cycle calls this so the baseline is the first equity of the day.
     */
    void observe(const AccountBalance& balance);

    /** @brief Record that an order was actually sent, for the per-day cap. */
    void recordOrder();

    /** @brief Adopt new limits, e.g. after the config was edited while running. */
    void setLimits(const RiskLimits& limits) { limits_ = limits; }

    [[nodiscard]] const RiskLimits& limits() const { return limits_; }

    /** @brief Day's opening equity, 0 before the first observation. */
    [[nodiscard]] double openingEquity() const { return openingEquity_; }

    /** @brief Last equity seen on a previous day, 0 if none is recorded. */
    [[nodiscard]] double previousEquity() const { return previousEquity_; }

    /**
     * @brief Equity the loss limit measures against.
     *
     * The higher of the day's open and the previous session's close. A loop that
     * starts at 09:00 gets a meaningful intraday baseline from the former; a
     * once-a-day process gets a meaningful one from the latter, without which it
     * would be comparing its single balance against itself.
     */
    [[nodiscard]] double referenceEquity() const;

    /** @brief Orders sent today, including ones sent by earlier processes. */
    [[nodiscard]] int ordersToday() const { return ordersToday_; }

   private:
    void load();
    void save() const;

    RiskLimits  limits_;
    std::string path_;
    std::string date_;  ///< KST date the loaded state belongs to
    double      openingEquity_  = 0.0;
    double      previousEquity_ = 0.0;  ///< last equity seen on an earlier day
    double      lastEquity_     = 0.0;  ///< last equity seen at all, carried into tomorrow
    int         ordersToday_    = 0;
};

}  // namespace trade
