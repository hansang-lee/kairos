#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "broker/kis_trader.hpp"
#include "trade/trade_journal.hpp"

namespace trade {

/**
 * @brief Writing what actually filled back into the journal.
 *
 * The journal records an order at the moment it is decided, because that is the
 * only moment the reason exists. What it cannot know then is whether the order
 * filled, how much of it, and at what price — and without that the journal answers
 * "what did this strategy intend" but never "what did it get". Slippage, partial
 * fills and rejections all live in the gap between the two.
 *
 * Kept as a function over a list of fills rather than something that queries the
 * broker itself, so the matching logic can be tested without a network or an
 * account.
 */
struct ReconcileResult {
    std::size_t recorded     = 0;  ///< fills newly written to the journal
    std::size_t alreadyKnown = 0;  ///< fills the journal already carried
    std::size_t ignored      = 0;  ///< cancelled, or accepted with nothing filled yet
};

/**
 * @brief Append a "fill" entry for every fill the journal does not already hold.
 *
 * Idempotent: keyed on "<orderNo>:<filledQty>", so running it repeatedly over the
 * same day writes nothing new, while an order that fills further does get recorded
 * again at its larger quantity.
 *
 * Each fill is stamped with the strategy that ordered it, looked up by order number
 * from the journal's own order entries. A fill whose order this system did not place
 * — a trade made by hand at the broker — is still recorded, with no strategy
 * attached, because the account moved and the journal should say so.
 *
 * @param mode "paper" or "live", copied onto the entries.
 */
ReconcileResult reconcileFills(const TradeJournal& journal, const std::vector<Fill>& fills, const std::string& mode);

}  // namespace trade
