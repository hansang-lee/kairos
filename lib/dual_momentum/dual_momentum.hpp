#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "indicator.hpp"
#include "strategy/istrategy.hpp"

/**
 * @brief Invested only when both the trend filter and the trailing return agree.
 *
 * Two weak signals that fail in different conditions. The moving average is slow
 * to leave a sharp drop but reliable through a grinding one; the trailing return
 * is the reverse. Requiring both is a deliberate trade of return for fewer
 * false starts, and exiting when *either* turns is what keeps the drawdown down.
 */
class DualMomentum: public IStrategy {
   public:
    /**
     * @param maPeriod  Trend filter length.
     * @param lookback  Bars of trailing return.
     * @param threshold Return, in percent, the lookback must exceed.
     */
    explicit DualMomentum(std::size_t maPeriod = 200, std::size_t lookback = 252, double threshold = 0.0);

    [[nodiscard]] std::string name() const override;
    void                      init(const StockInfo& data) override;
    [[nodiscard]] std::size_t warmupPeriod() const override;
    [[nodiscard]] Signal      evaluate(const StockInfo& data, std::size_t index) override;

   private:
    std::size_t         maPeriod_;
    std::size_t         lookback_;
    double              threshold_;
    std::vector<double> ma_;  ///< ma_[0] is at data index (maPeriod_ - 1)
};
