#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace trade {

/**
 * @brief Per-ticker state the broker does not keep for us.
 *
 * KIS reports what is held and at what average price, which covers a plain stop
 * loss. It cannot answer "how high did this go since I bought it", "when did I
 * last sell this", or "how many tranches of the plan have filled" — so trailing
 * stops, re-entry cooldowns and scaled entries need this alongside the balance.
 */
struct PositionState {
    int64_t entryTs       = 0;    ///< unix seconds when a position was first observed
    double  peakPrice     = 0.0;  ///< highest price seen while holding (trailing stop reference)
    int     entryTranches = 0;    ///< buy tranches already sent for the current position
    int     exitTranches  = 0;    ///< sell tranches already sent for the current position
    int64_t lastExitTs    = 0;    ///< unix seconds when the position last went flat (cooldown reference)
};

/**
 * @brief Local position state, persisted so a restart does not forget a peak.
 *
 * The broker balance stays the source of truth for *whether* a position exists;
 * this only carries the history around it. sync() reconciles the two, so a
 * position closed by hand outside the system is noticed on the next cycle rather
 * than leaving a stale peak that would fire a trailing stop on the next entry.
 */
class PositionStore {
   public:
    /**
     * @param path State file. Empty (default) resolves to <project-root>/data/positions.json.
     */
    explicit PositionStore(const std::string& path = "");

    /**
     * @brief Reconcile one ticker against the broker and return its current state.
     *
     * Going from flat to held starts a position; going from held to flat records
     * the exit time and clears the entry state. While held, the peak is raised.
     *
     * @param ticker  Ticker to reconcile.
     * @param heldQty Shares the broker reports, 0 when flat.
     * @param price   Current price, used to track the peak.
     */
    const PositionState& sync(const std::string& ticker, int64_t heldQty, double price);

    /** @brief Record that a buy tranche was sent. */
    void recordEntryTranche(const std::string& ticker);

    /** @brief Record that a sell tranche was sent. */
    void recordExitTranche(const std::string& ticker);

    [[nodiscard]] const PositionState& get(const std::string& ticker) const;
    [[nodiscard]] const std::string&   path() const { return path_; }

   private:
    void load();
    void save() const;

    std::string                          path_;
    std::map<std::string, PositionState> states_;
    PositionState                        empty_;
};

}  // namespace trade
