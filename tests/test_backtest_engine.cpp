#include "backtest/backtest_engine.hpp"
#include "strategy/istrategy.hpp"
#include "test_framework.hpp"

namespace {

/** Emits a scripted signal sequence, so the engine is tested rather than a strategy. */
class ScriptedStrategy: public IStrategy {
   public:
    explicit ScriptedStrategy(std::vector<Signal> script)
        : script_(std::move(script)) {}

    std::string name() const override { return "Scripted"; }
    void        init(const StockInfo&) override {}
    std::size_t warmupPeriod() const override { return 0; }
    Signal      evaluate(const StockInfo&, std::size_t index) override {
        return index < script_.size() ? script_[index] : Signal::HOLD;
    }

   private:
    std::vector<Signal> script_;
};

StockInfo makeBars(const std::vector<double>& closes, double lowFactor = 1.0) {
    StockInfo s;
    s.ticker   = "TEST";
    s.currency = "KRW";
    for (std::size_t i = 0; i < closes.size(); ++i) {
        s.timestamps.push_back(static_cast<int64_t>(1600000000 + i * 86400));
        s.open.push_back(closes[i]);
        s.high.push_back(closes[i]);
        s.low.push_back(closes[i] * lowFactor);
        s.close.push_back(closes[i]);
        s.volume.push_back(1000);
    }
    return s;
}

BacktestConfig frictionless() {
    BacktestConfig c;
    c.commissionRate = 0.0;
    c.slippagePct    = 0.0;
    c.sellTaxRate    = 0.0;
    c.positionPct    = 1.0;
    return c;
}

}  // namespace

TEST(backtest, buy_then_sell_captures_the_move) {
    const auto bars = makeBars({100, 110, 120, 130});
    // evaluate(i) decides the order filled at bar i: buy at bar 1 (110), sell at bar 3 (130).
    ScriptedStrategy strat({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::SELL});
    BacktestEngine   engine(10000.0);
    const auto       r = engine.run(strat, bars, frictionless());

    CHECK_EQ(r.trades.size(), std::size_t{1});
    CHECK_NEAR(r.trades[0].buyPrice, 110.0, 1e-9);
    CHECK_NEAR(r.trades[0].sellPrice, 130.0, 1e-9);
    CHECK_NEAR(r.totalReturnPct, (130.0 / 110.0 - 1.0) * 100.0, 1e-6);
}

TEST(backtest, commission_and_slippage_reduce_the_result) {
    const auto       bars = makeBars({100, 110, 120, 130});
    ScriptedStrategy a({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::SELL});
    ScriptedStrategy b({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::SELL});

    BacktestEngine engine(10000.0);
    const auto     clean = engine.run(a, bars, frictionless());

    auto costly            = frictionless();
    costly.commissionRate  = 0.001;
    costly.slippagePct     = 0.001;
    const auto withCosts   = engine.run(b, bars, costly);

    CHECK(withCosts.totalReturnPct < clean.totalReturnPct);
}

TEST(backtest, sell_tax_is_charged_only_on_exit) {
    const auto       bars = makeBars({100, 110, 120, 130});
    ScriptedStrategy a({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::SELL});
    ScriptedStrategy b({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::SELL});

    BacktestEngine engine(10000.0);
    const auto     noTax = engine.run(a, bars, frictionless());

    auto taxed        = frictionless();
    taxed.sellTaxRate = 0.002;
    const auto withTax = engine.run(b, bars, taxed);

    // One round trip, tax on the sell only: the drag is one 0.2% bite, not two.
    const double expected = noTax.finalCapital * (1.0 - 0.002);
    CHECK_NEAR(withTax.finalCapital, expected, expected * 1e-6);
}

TEST(backtest, market_cost_presets_match_the_published_rates) {
    const auto krx = BacktestConfig::forMarket("KRX");
    CHECK_NEAR(krx.sellTaxRate, 0.0020, 1e-12);      // 증권거래세 + 농특세, 2026
    CHECK(krx.commissionRate < 0.0005);              // online commission, well under a bp

    const auto us = BacktestConfig::forMarket("US");
    CHECK_NEAR(us.commissionRate, 0.0025, 1e-12);    // 0.25% per side
    CHECK(us.sellTaxRate < krx.sellTaxRate);         // SEC fee is tiny next to the KRX tax
    // The round trip is what decides whether frequent trading is viable at all.
    CHECK(us.commissionRate * 2 > krx.commissionRate * 2 + krx.sellTaxRate);
}

TEST(backtest, stop_loss_exits_on_the_same_bar) {
    // Low dips 10% below the close on every bar, so a 5% stop must fire.
    const auto       bars = makeBars({100, 100, 100, 100}, 0.90);
    ScriptedStrategy strat({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::HOLD});

    auto cfg        = frictionless();
    cfg.stopLossPct = 5.0;

    BacktestEngine engine(10000.0);
    const auto     r = engine.run(strat, bars, cfg);

    CHECK_EQ(r.trades.size(), std::size_t{1});
    CHECK_MSG(r.trades[0].stoppedOut, "trade should be marked as stopped out");
    CHECK_NEAR(r.trades[0].sellPrice, 95.0, 1e-9);  // exits at the stop, not the low
}

TEST(backtest, idle_cash_stays_in_equity_when_position_is_partial) {
    // A prior bug dropped idle cash from the equity curve, reporting a -55%
    // drawdown for a strategy whose worst trade lost 4%.
    const auto       bars = makeBars({100, 50, 50, 50});
    ScriptedStrategy strat({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::HOLD});

    auto cfg        = frictionless();
    cfg.positionPct = 0.10;  // only a tenth is exposed

    BacktestEngine engine(10000.0);
    const auto     r = engine.run(strat, bars, cfg);

    // Price halved with 10% committed, so the account cannot be down more than ~10%.
    CHECK_MSG(r.maxDrawdownPct > -15.0, "drawdown " << r.maxDrawdownPct << " exceeds the committed fraction");
}

TEST(backtest, no_signals_means_no_trades_and_no_change) {
    const auto       bars = makeBars({100, 110, 120, 130});
    ScriptedStrategy strat({Signal::HOLD, Signal::HOLD, Signal::HOLD, Signal::HOLD});
    BacktestEngine   engine(10000.0);
    const auto       r = engine.run(strat, bars, frictionless());

    CHECK(r.trades.empty());
    CHECK_NEAR(r.totalReturnPct, 0.0, 1e-9);
    CHECK_NEAR(r.finalCapital, 10000.0, 1e-9);
}

TEST(backtest, empty_series_does_not_crash) {
    StockInfo        empty;
    ScriptedStrategy strat({});
    BacktestEngine   engine(10000.0);
    const auto       r = engine.run(strat, empty, frictionless());
    CHECK(r.trades.empty());
}

TEST(backtest, open_position_is_closed_at_the_end) {
    const auto       bars = makeBars({100, 110, 120, 130});
    ScriptedStrategy strat({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::HOLD});
    BacktestEngine   engine(10000.0);
    const auto       r = engine.run(strat, bars, frictionless());

    // Otherwise a strategy that never sells would report no result at all.
    CHECK_EQ(r.trades.size(), std::size_t{1});
    CHECK_NEAR(r.trades[0].sellPrice, 130.0, 1e-9);
}
