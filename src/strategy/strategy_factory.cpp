#include "strategy/strategy_factory.hpp"
#include <algorithm>
#include <filesystem>

#include <iostream>

#include "adx_trend.hpp"
#include "aroon_trend.hpp"
#include "bollinger_strategy.hpp"
#include "cci_reversal.hpp"
#include "common/util.hpp"
#include "donchian_breakout.hpp"
#include "keltner_breakout.hpp"
#include "ma_slope_trend.hpp"
#include "macd_strategy.hpp"
#include "mfi_reversal.hpp"
#include "obv_trend.hpp"
#include "psar_trend.hpp"
#include "rsi_strategy.hpp"
#include "sma_crossover.hpp"
#include "stochastic_reversal.hpp"
#include "supertrend_follow.hpp"
#include "williams_r_strategy.hpp"

std::unique_ptr<IStrategy> StrategyProfile::createStrategy() const {
    if (type == "rsi") {
        const std::size_t period     = params.value("period", 14);
        const double      oversold   = params.value("oversold", 30.0);
        const double      overbought = params.value("overbought", 70.0);
        return std::make_unique<RsiStrategy>(period, oversold, overbought);
    }
    if (type == "macd") {
        const std::size_t fast   = params.value("fast", 12);
        const std::size_t slow   = params.value("slow", 26);
        const std::size_t signal = params.value("signal", 9);
        return std::make_unique<MacdStrategy>(fast, slow, signal);
    }
    if (type == "bollinger") {
        const std::size_t period    = params.value("period", 20);
        const double      numStdDev = params.value("std_dev", 2.0);
        return std::make_unique<BollingerStrategy>(period, numStdDev);
    }
    if (type == "sma_crossover" || type == "sma") {
        const std::size_t shortWin = params.value("short_window", 20);
        const std::size_t longWin  = params.value("long_window", 50);
        return std::make_unique<SmaCrossover>(shortWin, longWin);
    }
    if (type == "stochastic_reversal" || type == "stochastic") {
        const std::size_t kPeriod    = params.value("k_period", 14);
        const std::size_t dPeriod    = params.value("d_period", 3);
        const double      oversold   = params.value("oversold", 20.0);
        const double      overbought = params.value("overbought", 80.0);
        return std::make_unique<StochasticReversal>(kPeriod, dPeriod, oversold, overbought);
    }
    if (type == "williams_r") {
        const std::size_t period     = params.value("period", 14);
        const double      oversold   = params.value("oversold", -80.0);
        const double      overbought = params.value("overbought", -20.0);
        return std::make_unique<WilliamsRStrategy>(period, oversold, overbought);
    }
    if (type == "cci_reversal" || type == "cci") {
        const std::size_t period     = params.value("period", 20);
        const double      oversold   = params.value("oversold", -100.0);
        const double      overbought = params.value("overbought", 100.0);
        return std::make_unique<CciReversal>(period, oversold, overbought);
    }
    if (type == "mfi_reversal" || type == "mfi") {
        const std::size_t period     = params.value("period", 14);
        const double      oversold   = params.value("oversold", 20.0);
        const double      overbought = params.value("overbought", 80.0);
        return std::make_unique<MfiReversal>(period, oversold, overbought);
    }
    if (type == "adx_trend" || type == "adx") {
        const std::size_t period       = params.value("period", 14);
        const double      adxThreshold = params.value("adx_threshold", 25.0);
        return std::make_unique<AdxTrend>(period, adxThreshold);
    }
    if (type == "supertrend") {
        const std::size_t period     = params.value("period", 10);
        const double      multiplier = params.value("multiplier", 3.0);
        return std::make_unique<SuperTrendFollow>(period, multiplier);
    }
    if (type == "aroon_trend" || type == "aroon") {
        const std::size_t period            = params.value("period", 25);
        const double      strengthThreshold = params.value("strength_threshold", 70.0);
        return std::make_unique<AroonTrend>(period, strengthThreshold);
    }
    if (type == "psar_trend" || type == "psar") {
        const double afStep = params.value("af_step", 0.02);
        const double afMax  = params.value("af_max", 0.2);
        return std::make_unique<PsarTrend>(afStep, afMax);
    }
    if (type == "donchian_breakout" || type == "donchian") {
        const std::size_t period = params.value("period", 20);
        return std::make_unique<DonchianBreakout>(period);
    }
    if (type == "obv_trend" || type == "obv") {
        const std::size_t smaPeriod = params.value("sma_period", 20);
        return std::make_unique<ObvTrendConfirm>(smaPeriod);
    }
    if (type == "keltner_breakout" || type == "keltner") {
        const std::size_t emaPeriod  = params.value("ema_period", 20);
        const std::size_t atrPeriod  = params.value("atr_period", 10);
        const double      multiplier = params.value("multiplier", 2.0);
        return std::make_unique<KeltnerBreakout>(emaPeriod, atrPeriod, multiplier);
    }
    if (type == "ma_slope_trend" || type == "slope_trend") {
        const std::size_t maPeriod       = params.value("ma_period", 20);
        const std::size_t slopeWindow    = params.value("slope_window", 10);
        const double      entryThreshold = params.value("entry_threshold", 0.15);
        const double      exitThreshold  = params.value("exit_threshold", -0.05);
        const std::size_t adxPeriod      = params.value("adx_period", 14);
        const double      adxThreshold   = params.value("adx_threshold", 20.0);
        return std::make_unique<MaSlopeTrend>(maPeriod, slopeWindow, entryThreshold, exitThreshold, adxPeriod,
                                              adxThreshold);
    }

    std::cerr << "StrategyProfile: Unknown strategy type '" << type << "'" << std::endl;
    return nullptr;
}

namespace {

/** Last-write time of a file as unix seconds; 0 when it cannot be read. */
std::int64_t fileMtime(const std::string& path) {
    std::error_code ec;
    const auto      t = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return 0;
    }
    return static_cast<std::int64_t>(t.time_since_epoch().count());
}

}  // namespace

bool PortfolioConfig::sourceChanged() const {
    if (sourcePath_.empty() || sourceMtime_ == 0) {
        return false;  // nothing to compare against; never claim a spurious change
    }
    const std::int64_t now = fileMtime(sourcePath_);
    return now != 0 && now != sourceMtime_;
}

PortfolioConfig PortfolioConfig::loadFromFile(const std::string& configPath) {
    PortfolioConfig cfg;
    cfg.sourcePath_  = configPath;
    cfg.sourceMtime_ = fileMtime(configPath);
    const auto j     = util::loadJsonConfig(configPath);
    if (!j || !j->contains("strategies") || !(*j)["strategies"].is_array()) {
        return cfg;
    }

    cfg.initialCapitalKrw_ = j->value("initial_capital_krw", 10000000.0);

    if (j->contains("risk") && (*j)["risk"].is_object()) {
        const auto& r                     = (*j)["risk"];
        cfg.riskLimits_.dailyLossLimitPct = r.value("daily_loss_limit_pct", 0.0);
        cfg.riskLimits_.maxOrdersPerDay   = r.value("max_orders_per_day", 0);
    }

    for (const auto& item : (*j)["strategies"]) {
        StrategyProfile p;
        p.id              = item.value("id", 0);
        p.name            = item.value("name", "");
        p.ticker          = item.value("ticker", "");
        p.market          = item.value("market", "KRX");
        p.type            = item.value("type", "");
        p.category        = item.value("category", "");
        p.positionPct     = item.value("position_pct", 1.0);
        p.stopLossPct     = item.value("stop_loss_pct", 0.0);
        p.takeProfitPct   = item.value("take_profit_pct", 0.0);
        p.trailingStopPct = item.value("trailing_stop_pct", 0.0);
        p.cooldownMinutes = item.value("cooldown_minutes", 0);
        p.entryTranches   = std::max(1, item.value("entry_tranches", 1));
        p.exitTranches    = std::max(1, item.value("exit_tranches", 1));
        if (item.contains("trade_window") && item["trade_window"].is_object()) {
            p.tradeStart = item["trade_window"].value("start", "");
            p.tradeEnd   = item["trade_window"].value("end", "");
        }
        p.description = item.value("description", "");

        if (item.contains("params") && item["params"].is_object()) {
            p.params = item["params"];
        }
        cfg.profiles_.push_back(p);
    }

    return cfg;
}
