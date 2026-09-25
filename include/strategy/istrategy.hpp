#pragma once

#include <cstddef>
#include <optional>
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

    /**
     * @brief How much of the sleeve to hold at bar `index`, as a fraction, if this
     *        strategy thinks in exposure rather than in signals.
     *
     * A buy/sell signal says "all or nothing". Volatility targeting says "0.7 today,
     * 0.4 tomorrow", and forcing that through BUY/SELL would lose the number that is
     * the whole strategy. A strategy that returns a value here is sized to it by the
     * backtest engine and the executor, and its evaluate() is not consulted for
     * direction. The same index convention holds: bars up to index-1 only.
     *
     * Returns nothing for a signal strategy, which is every strategy that existed
     * before this hook. Values above 1.0 mean leverage, which the single-instrument
     * paths clamp to 1.0 until a levered leg exists.
     */
    [[nodiscard]] virtual std::optional<double> targetExposure(const StockInfo& data, std::size_t index) {
        (void)data;
        (void)index;
        return std::nullopt;
    }
};
