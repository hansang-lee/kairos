#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "portfolio/portfolio_data.hpp"

namespace portfolio {

/**
 * @brief A strategy that decides how to split one account across several assets.
 *
 * Distinct from IStrategy, which answers buy/sell/hold for one ticker in
 * isolation and therefore cannot express "hold the strongest three of these
 * thirty" or "weight each by the inverse of its volatility". Those need every
 * asset in view at once, and they allocate rather than signal.
 */
struct IPortfolioStrategy {
    virtual ~IPortfolioStrategy() = default;

    [[nodiscard]] virtual std::string name() const = 0;

    virtual void init(const PortfolioData& data) = 0;

    /** @brief Bars needed before the first meaningful allocation. */
    [[nodiscard]] virtual std::size_t warmupPeriod() const = 0;

    /**
     * @brief Target weights for the bar at `index`, one per asset.
     *
     * Same index convention as IStrategy: weights for bar `index` may only use
     * prices up to `index - 1`, so an allocation is never made with a price it
     * could not have known.
     *
     * Weights are fractions of account equity and need not sum to 1 — the
     * remainder is held as cash, which is how these strategies express "stay
     * out". A sum above 1 would be leverage, which is not modelled: the engine
     * normalises such a target down to a gross exposure of 1, keeping the relative
     * weights and capping only the total.
     */
    [[nodiscard]] virtual std::vector<double> targetWeights(const PortfolioData& data, std::size_t index) = 0;
};

}  // namespace portfolio
