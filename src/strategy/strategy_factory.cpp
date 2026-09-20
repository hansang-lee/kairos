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
#include "ichimoku_trend.hpp"
#include "keltner_breakout.hpp"
#include "ma_slope_trend.hpp"
#include "macd_strategy.hpp"
#include "mfi_reversal.hpp"
#include "obv_trend.hpp"
#include "psar_trend.hpp"
#include "regime_rsi.hpp"
#include "rsi_strategy.hpp"
#include "sma_crossover.hpp"
#include "squeeze_breakout.hpp"
#include "stochastic_reversal.hpp"
#include "supertrend_follow.hpp"
#include "volume_breakout.hpp"
#include "williams_r_strategy.hpp"

namespace {

/**
 * @brief Warn about parameter keys the strategy does not read.
 *
 * nlohmann's params.value(key, fallback) returns the fallback for a missing key,
 * so a mistyped parameter is silently ignored and the strategy runs on defaults
 * — which looks like the parameter having no effect rather than like an error.
 * That cost a whole parameter sweep before it was noticed, so unknown keys are
 * now reported.
 */
void warnUnknownParams(const nlohmann::json& params, const std::string& type, const std::vector<std::string>& known) {
    if (!params.is_object()) {
        return;
    }
    for (const auto& [key, value] : params.items()) {
        if (std::find(known.begin(), known.end(), key) == known.end()) {
            std::cerr << "StrategyProfile: '" << type << "' does not use parameter '" << key
                      << "' — it will be ignored." << std::endl;
        }
    }
}

}  // namespace

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
        warnUnknownParams(params, type, {"period", "std_dev", "std_devs"});
        const std::size_t period = params.value("period", 20);
        // Both spellings are accepted: "std_dev" is what the original config used,
        // "std_devs" is what every other band strategy and the docs use.
        const double numStdDev =
            params.contains("std_devs") ? params.value("std_devs", 2.0) : params.value("std_dev", 2.0);
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

    if (type == "regime_rsi" || type == "regime_filter_rsi") {
        const std::size_t regimePeriod = params.value("regime_period", 200);
        const std::size_t rsiPeriod    = params.value("rsi_period", 14);
        const double      oversold     = params.value("oversold", 35.0);
        const double      exitLevel    = params.value("exit_level", 65.0);
        return std::make_unique<RegimeRsi>(regimePeriod, rsiPeriod, oversold, exitLevel);
    }

    if (type == "volume_breakout") {
        const std::size_t period       = params.value("period", 20);
        const std::size_t volumePeriod = params.value("volume_period", 20);
        const double      volumeRatio  = params.value("volume_ratio", 1.5);
        return std::make_unique<VolumeBreakout>(period, volumePeriod, volumeRatio);
    }

    if (type == "squeeze_breakout" || type == "squeeze") {
        const std::size_t period          = params.value("period", 20);
        const double      stdDevs         = params.value("std_devs", 2.0);
        const std::size_t squeezeLookback = params.value("squeeze_lookback", 60);
        const double      squeezePercent  = params.value("squeeze_percent", 0.25);
        return std::make_unique<SqueezeBreakout>(period, stdDevs, squeezeLookback, squeezePercent);
    }

    if (type == "ichimoku_trend" || type == "ichimoku") {
        const std::size_t conversion = params.value("conversion", 9);
        const std::size_t base       = params.value("base", 26);
        const std::size_t spanB      = params.value("span_b", 52);
        return std::make_unique<IchimokuTrend>(conversion, base, spanB);
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
        p.id       = item.value("id", 0);
        p.name     = item.value("name", "");
        p.ticker   = item.value("ticker", "");
        p.market   = item.value("market", "KRX");
        p.type     = item.value("type", "");
        p.category = item.value("category", "");
        // Percentages are clamped at load rather than trusted. A negative stop loss
        // inverts its own comparison — price <= avg * (1 - (-5)/100) is
        // price <= avg * 1.05 — so the position sells the moment it is opened, and
        // nothing downstream would report that as anything but a working stop.
        auto clampPct = [&](const char* key, double fallback, double lo, double hi) {
            const double raw = item.value(key, fallback);
            if (raw < lo || raw > hi) {
                std::cerr << "StrategyProfile #" << p.id << ": " << key << "=" << raw << " is outside [" << lo << ", "
                          << hi << "]; clamped." << std::endl;
            }
            return std::min(hi, std::max(lo, raw));
        };

        p.positionPct     = clampPct("position_pct", 1.0, 0.0, 1.0);
        p.stopLossPct     = clampPct("stop_loss_pct", 0.0, 0.0, 100.0);
        p.takeProfitPct   = clampPct("take_profit_pct", 0.0, 0.0, 1000.0);
        p.trailingStopPct = clampPct("trailing_stop_pct", 0.0, 0.0, 100.0);
        p.cooldownMinutes = std::max(0, item.value("cooldown_minutes", 0));
        p.entryTranches   = std::max(1, item.value("entry_tranches", 1));
        p.exitTranches    = std::max(1, item.value("exit_tranches", 1));
        if (item.contains("trade_window") && item["trade_window"].is_object()) {
            p.tradeStart = item["trade_window"].value("start", "");
            p.tradeEnd   = item["trade_window"].value("end", "");
        }
        p.description = item.value("description", "");
        p.enabled     = item.value("enabled", true);

        if (item.contains("params") && item["params"].is_object()) {
            p.params = item["params"];
        }
        cfg.profiles_.push_back(p);
    }

    return cfg;
}
