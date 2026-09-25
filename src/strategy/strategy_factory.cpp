#include "strategy/strategy_factory.hpp"

#include "strategy/strategy_catalog.hpp"
#include <algorithm>
#include <filesystem>

#include <iostream>

#include "absolute_momentum.hpp"
#include "adx_trend.hpp"
#include "aroon_trend.hpp"
#include "bollinger_strategy.hpp"
#include "cci_reversal.hpp"
#include "common/util.hpp"
#include "donchian_breakout.hpp"
#include "dual_momentum.hpp"
#include "ichimoku_trend.hpp"
#include "keltner_breakout.hpp"
#include "ma_slope_trend.hpp"
#include "ma_timing.hpp"
#include "macd_strategy.hpp"
#include "mfi_reversal.hpp"
#include "obv_trend.hpp"
#include "psar_trend.hpp"
#include "regime_rsi.hpp"
#include "relative_momentum.hpp"
#include "rsi_strategy.hpp"
#include "sma_crossover.hpp"
#include "squeeze_breakout.hpp"
#include "stochastic_reversal.hpp"
#include "supertrend_follow.hpp"
#include "vol_target.hpp"
#include "volume_breakout.hpp"
#include "williams_r_strategy.hpp"

namespace {}  // namespace

std::unique_ptr<IStrategy> StrategyProfile::createStrategy(std::vector<std::string>* unused) const {
    const ParamReader pr(params);
    // Runs on every return path below, so no strategy type can be left out.
    struct Report {
        const ParamReader&        pr;
        const std::string&        type;
        std::vector<std::string>* out;
        ~Report() {
            if (!pr.touched()) {
                return;  // no type matched; the unknown-type message covers it
            }
            const auto keys = pr.unusedKeys();
            if (out != nullptr) {
                *out = keys;
            }
            for (const auto& k : keys) {
                std::cerr << "StrategyProfile: '" << type << "' does not use parameter '" << k
                          << "' — it will be ignored." << std::endl;
            }
        }
    } report{pr, type, unused};

    if (type == "rsi") {
        const std::size_t period     = pr.value("period", 14);
        const double      oversold   = pr.value("oversold", 30.0);
        const double      overbought = pr.value("overbought", 70.0);
        return std::make_unique<RsiStrategy>(period, oversold, overbought);
    }
    if (type == "macd") {
        const std::size_t fast   = pr.value("fast", 12);
        const std::size_t slow   = pr.value("slow", 26);
        const std::size_t signal = pr.value("signal", 9);
        return std::make_unique<MacdStrategy>(fast, slow, signal);
    }
    if (type == "bollinger") {
        const std::size_t period = pr.value("period", 20);
        // Both spellings are accepted: "std_dev" is what the original config used,
        // "std_devs" is what every other band strategy and the docs use.
        const double numStdDev = pr.contains("std_devs") ? pr.value("std_devs", 2.0) : pr.value("std_dev", 2.0);
        return std::make_unique<BollingerStrategy>(period, numStdDev);
    }
    if (type == "sma_crossover" || type == "sma") {
        const std::size_t shortWin = pr.value("short_window", 20);
        const std::size_t longWin  = pr.value("long_window", 50);
        return std::make_unique<SmaCrossover>(shortWin, longWin);
    }
    if (type == "stochastic_reversal" || type == "stochastic") {
        const std::size_t kPeriod    = pr.value("k_period", 14);
        const std::size_t dPeriod    = pr.value("d_period", 3);
        const double      oversold   = pr.value("oversold", 20.0);
        const double      overbought = pr.value("overbought", 80.0);
        return std::make_unique<StochasticReversal>(kPeriod, dPeriod, oversold, overbought);
    }
    if (type == "williams_r") {
        const std::size_t period     = pr.value("period", 14);
        const double      oversold   = pr.value("oversold", -80.0);
        const double      overbought = pr.value("overbought", -20.0);
        return std::make_unique<WilliamsRStrategy>(period, oversold, overbought);
    }
    if (type == "cci_reversal" || type == "cci") {
        const std::size_t period     = pr.value("period", 20);
        const double      oversold   = pr.value("oversold", -100.0);
        const double      overbought = pr.value("overbought", 100.0);
        return std::make_unique<CciReversal>(period, oversold, overbought);
    }
    if (type == "mfi_reversal" || type == "mfi") {
        const std::size_t period     = pr.value("period", 14);
        const double      oversold   = pr.value("oversold", 20.0);
        const double      overbought = pr.value("overbought", 80.0);
        return std::make_unique<MfiReversal>(period, oversold, overbought);
    }
    if (type == "adx_trend" || type == "adx") {
        const std::size_t period       = pr.value("period", 14);
        const double      adxThreshold = pr.value("adx_threshold", 25.0);
        return std::make_unique<AdxTrend>(period, adxThreshold);
    }
    if (type == "supertrend") {
        const std::size_t period     = pr.value("period", 10);
        const double      multiplier = pr.value("multiplier", 3.0);
        return std::make_unique<SuperTrendFollow>(period, multiplier);
    }
    if (type == "aroon_trend" || type == "aroon") {
        const std::size_t period            = pr.value("period", 25);
        const double      strengthThreshold = pr.value("strength_threshold", 70.0);
        return std::make_unique<AroonTrend>(period, strengthThreshold);
    }
    if (type == "psar_trend" || type == "psar") {
        const double afStep = pr.value("af_step", 0.02);
        const double afMax  = pr.value("af_max", 0.2);
        return std::make_unique<PsarTrend>(afStep, afMax);
    }
    if (type == "donchian_breakout" || type == "donchian") {
        const std::size_t period = pr.value("period", 20);
        return std::make_unique<DonchianBreakout>(period);
    }
    if (type == "obv_trend" || type == "obv") {
        const std::size_t smaPeriod = pr.value("sma_period", 20);
        return std::make_unique<ObvTrendConfirm>(smaPeriod);
    }
    if (type == "keltner_breakout" || type == "keltner") {
        const std::size_t emaPeriod  = pr.value("ema_period", 20);
        const std::size_t atrPeriod  = pr.value("atr_period", 10);
        const double      multiplier = pr.value("multiplier", 2.0);
        return std::make_unique<KeltnerBreakout>(emaPeriod, atrPeriod, multiplier);
    }
    if (type == "ma_slope_trend" || type == "slope_trend") {
        const std::size_t maPeriod       = pr.value("ma_period", 20);
        const std::size_t slopeWindow    = pr.value("slope_window", 10);
        const double      entryThreshold = pr.value("entry_threshold", 0.15);
        const double      exitThreshold  = pr.value("exit_threshold", -0.05);
        const std::size_t adxPeriod      = pr.value("adx_period", 14);
        const double      adxThreshold   = pr.value("adx_threshold", 20.0);
        return std::make_unique<MaSlopeTrend>(maPeriod, slopeWindow, entryThreshold, exitThreshold, adxPeriod,
                                              adxThreshold);
    }

    if (type == "regime_rsi" || type == "regime_filter_rsi") {
        const std::size_t regimePeriod = pr.value("regime_period", 200);
        const std::size_t rsiPeriod    = pr.value("rsi_period", 14);
        const double      oversold     = pr.value("oversold", 35.0);
        const double      exitLevel    = pr.value("exit_level", 65.0);
        return std::make_unique<RegimeRsi>(regimePeriod, rsiPeriod, oversold, exitLevel);
    }

    if (type == "volume_breakout") {
        const std::size_t period       = pr.value("period", 20);
        const std::size_t volumePeriod = pr.value("volume_period", 20);
        const double      volumeRatio  = pr.value("volume_ratio", 1.5);
        return std::make_unique<VolumeBreakout>(period, volumePeriod, volumeRatio);
    }

    if (type == "squeeze_breakout" || type == "squeeze") {
        const std::size_t period          = pr.value("period", 20);
        const double      stdDevs         = pr.value("std_devs", 2.0);
        const std::size_t squeezeLookback = pr.value("squeeze_lookback", 60);
        const double      squeezePercent  = pr.value("squeeze_percent", 0.25);
        return std::make_unique<SqueezeBreakout>(period, stdDevs, squeezeLookback, squeezePercent);
    }

    if (type == "ichimoku_trend" || type == "ichimoku") {
        const std::size_t conversion = pr.value("conversion", 9);
        const std::size_t base       = pr.value("base", 26);
        const std::size_t spanB      = pr.value("span_b", 52);
        return std::make_unique<IchimokuTrend>(conversion, base, spanB);
    }

    if (type == "ma_timing") {
        const std::size_t period    = pr.value("period", 200);
        const double      bufferPct = pr.value("buffer_pct", 0.0);
        return std::make_unique<MaTiming>(period, bufferPct);
    }

    if (type == "absolute_momentum") {
        const std::size_t lookback  = pr.value("lookback", 252);
        const double      threshold = pr.value("threshold", 0.0);
        return std::make_unique<AbsoluteMomentum>(lookback, threshold);
    }

    if (type == "dual_momentum") {
        const std::size_t maPeriod  = pr.value("ma_period", 200);
        const std::size_t lookback  = pr.value("lookback", 252);
        const double      threshold = pr.value("threshold", 0.0);
        return std::make_unique<DualMomentum>(maPeriod, lookback, threshold);
    }

    if (type == "relative_momentum") {
        const std::string reference = pr.value("reference", std::string("148070"));
        const std::size_t lookback  = pr.value("lookback", 126);
        const double      marginPct = pr.value("margin_pct", 0.0);
        const std::string cacheDir  = pr.value("cache_dir", std::string());
        return std::make_unique<RelativeMomentum>(reference, lookback, marginPct, cacheDir);
    }

    if (type == "vol_target") {
        const double      target = pr.value("target", 0.20);
        const double      cap    = pr.value("cap", 1.0);
        const std::size_t window = pr.value("window", 20);
        const double      band   = pr.value("band", 0.2);
        return std::make_unique<VolTarget>(target, cap, window, band);
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

PortfolioConfig PortfolioConfig::loadFromFile(const std::string& configPath, const std::string& catalogPath) {
    PortfolioConfig cfg;
    cfg.sourcePath_  = configPath;
    cfg.sourceMtime_ = fileMtime(configPath);
    const auto j     = util::loadJsonConfig(configPath);
    if (!j) {
        return cfg;
    }

    // "positions" is the current key; "strategies" is what the list was called when
    // each entry carried its own strategy definition.
    const char* listKey = j->contains("positions") ? "positions" : "strategies";
    if (!j->contains(listKey) || !(*j)[listKey].is_array()) {
        return cfg;
    }

    // Loaded lazily: a self-contained file with inline types needs no catalog, and
    // reading one that is absent would print a misleading error.
    bool            catalogLoaded = false;
    StrategyCatalog catalog;
    auto            resolve = [&](const std::string& id) -> const StrategyDef* {
        if (!catalogLoaded) {
            catalog       = StrategyCatalog::loadFromFile(catalogPath);
            catalogLoaded = true;
        }
        return catalog.find(id);
    };

    cfg.initialCapitalKrw_ = j->value("initial_capital_krw", 10000000.0);

    if (j->contains("risk") && (*j)["risk"].is_object()) {
        const auto& r                     = (*j)["risk"];
        cfg.riskLimits_.dailyLossLimitPct = r.value("daily_loss_limit_pct", 0.0);
        cfg.riskLimits_.maxOrdersPerDay   = r.value("max_orders_per_day", 0);
    }

    for (const auto& item : (*j)[listKey]) {
        StrategyProfile p;
        p.id     = item.value("id", 0);
        p.ticker = item.value("ticker", "");
        p.market = item.value("market", "KRX");

        // A position either references a catalog strategy or defines one inline.
        const std::string  strategyRef = item.value("strategy", "");
        const StrategyDef* def         = strategyRef.empty() ? nullptr : resolve(strategyRef);
        if (!strategyRef.empty() && def == nullptr) {
            // Not a default: a mistyped id quietly falling back to some other
            // strategy would only be noticed from the trades it produced.
            std::cerr << "PortfolioConfig: position #" << p.id << " references strategy '" << strategyRef
                      << "', which is not in " << catalog.path() << ". It will not trade." << std::endl;
            cfg.unresolved_.push_back(strategyRef);
            continue;
        }

        p.type     = def ? def->type : item.value("type", "");
        p.category = item.value("category", def ? def->category : "");
        p.name     = item.value("name", def ? (def->name + " — " + p.ticker) : std::string());
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
        } else if (def) {
            p.params = def->params;
        }
        if (p.description.empty() && def) {
            p.description = def->description;
        }
        cfg.profiles_.push_back(p);
    }

    return cfg;
}

std::vector<std::string> ParamReader::unusedKeys() const {
    std::vector<std::string> out;
    if (!params_.is_object()) {
        return out;
    }
    for (const auto& [key, value] : params_.items()) {
        if (seen_.count(key) == 0) {
            out.push_back(key);
        }
    }
    return out;
}
