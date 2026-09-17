#pragma once

#include <cstddef>
#include <string>

#include "stock_info.hpp"

enum class Signal {
    BUY,
    SELL,
    HOLD,
};

/**
 * @brief Abstract interface for investment strategies.
 *
 * All strategies must implement evaluate() which returns a BUY/SELL/HOLD
 * signal at a given data index. The strategy is responsible for managing
 * its own internal state (e.g., cached indicator values).
 */
struct IStrategy {
    virtual ~IStrategy() = default;

    /**
     * @brief Strategy display name.
     */
    [[nodiscard]] virtual std::string name() const = 0;

    /**
     * @brief Initialize strategy with stock data (e.g., precompute indicators).
     * @param data Historical stock data.
     */
    virtual void init(const StockInfo& data) = 0;

    /**
     * @brief Minimum number of data points required before the strategy
     *        can produce meaningful signals.
     */
    [[nodiscard]] virtual std::size_t warmupPeriod() const = 0;

    /**
     * @brief Evaluate the strategy at a given time index.
     * @param data  Historical stock data.
     * @param index Current time step index (0-based).
     * @return Signal — BUY, SELL, or HOLD
     */
    [[nodiscard]] virtual Signal evaluate(const StockInfo& data, std::size_t index) = 0;
};
