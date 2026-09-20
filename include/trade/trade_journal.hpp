#pragma once

#include <cstdint>
#include <set>
#include <string>

namespace trade {

/**
 * @brief One line of the trade journal.
 *
 * KIS knows what was ordered, but not *why* — it has no concept of our strategies.
 * The journal is the only place where an order is tied back to the profile that
 * produced it, so it must be written at decision time, including for dry runs.
 */
struct JournalEntry {
    ///< "order" (sent/dry-run), "skip" (suppressed), "fill" (confirmed by KIS),
    ///< "order_unknown" (sent, outcome never received — may or may not exist at the broker)
    std::string event = "order";
    std::string mode;            ///< "paper" or "live" (KIS account mode)
    bool        dryRun = false;  ///< true when no order was actually sent

    int         strategyId = -1;  ///< portfolio.json profile id
    std::string strategy;         ///< profile name
    std::string category;         ///< profile category (e.g. "scalp")

    std::string ticker;
    std::string side;  ///< "BUY" or "SELL"
    int64_t     quantity = 0;
    double      price    = 0.0;  ///< reference price at decision time (order) or fill average (fill)
    std::string reason;          ///< e.g. "signal BUY", "STOP-LOSS", "manual"

    std::string orderNo;          ///< KIS order number (ODNO), empty on dry runs
    bool        success = false;  ///< order accepted by KIS (always false for dry runs)
    std::string message;          ///< KIS message or local error description
};

/**
 * @brief Append-only JSONL trade log.
 *
 * Each append() writes exactly one line and flushes, so a killed process never
 * loses more than the entry it was in the middle of writing. The file lives
 * outside the repo history (data/ is gitignored) because it contains account
 * activity.
 */
class TradeJournal {
   public:
    /**
     * @param path Journal file path. Empty (default) resolves to <project-root>/data/trades.jsonl.
     */
    explicit TradeJournal(const std::string& path = "");

    /**
     * @brief Append one entry, stamping it with the current time.
     * @return false if the file could not be opened (never throws — a failed
     *         journal write must not abort a trading loop).
     */
    bool append(const JournalEntry& entry) const;

    /**
     * @brief Keys ("<orderNo>:<filledQty>") of the "fill" entries already recorded.
     *
     * Lets a fill sync run repeatedly over the same date without duplicating rows,
     * while still recording a partially filled order again once more of it fills.
     * Malformed lines are skipped rather than aborting the read.
     */
    [[nodiscard]] std::set<std::string> recordedFillKeys() const;

    [[nodiscard]] const std::string& path() const { return path_; }

   private:
    std::string path_;
};

}  // namespace trade
