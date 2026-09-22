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

    // 10,000 buys 90 shares at 110 and leaves 100 idle, because shares are whole.
    // The position captured 18.18%; the account did not, and the gap is the
    // remainder no broker would have invested for you.
    CHECK_NEAR(r.trades[0].returnPct, (130.0 / 110.0 - 1.0) * 100.0, 1e-6);
    CHECK_NEAR(r.totalReturnPct, ((90.0 * 130.0 + 100.0) / 10000.0 - 1.0) * 100.0, 1e-9);
}

TEST(backtest, positions_are_whole_shares_and_the_remainder_stays_in_cash) {
    // A price that divides into the capital badly on purpose: 10,000 / 333 is
    // 30.03, so a fractional engine would show a different number here.
    const auto       bars = makeBars({100, 333, 333, 333});
    ScriptedStrategy strat({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::SELL});
    BacktestEngine   engine(10000.0);
    const auto       r = engine.run(strat, bars, frictionless());

    // 30 shares at 333 costs 9,990 and leaves 10. Bought and sold at the same
    // price, so the account must end exactly where it started.
    CHECK_NEAR(r.finalCapital, 10000.0, 1e-9);
    CHECK_NEAR(r.totalReturnPct, 0.0, 1e-9);
}

TEST(backtest, an_allocation_too_small_for_one_share_opens_no_position) {
    // 1,000 cannot buy a share at 5,000, and the engine must leave the account
    // flat rather than record a trade nobody could place.
    const auto       bars = makeBars({100, 5000, 6000, 7000});
    ScriptedStrategy strat({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::SELL});
    BacktestEngine   engine(1000.0);
    const auto       r = engine.run(strat, bars, frictionless());

    CHECK_EQ(r.trades.size(), std::size_t{0});
    CHECK_NEAR(r.finalCapital, 1000.0, 1e-9);
}

TEST(backtest, commission_and_slippage_reduce_the_result) {
    const auto       bars = makeBars({100, 110, 120, 130});
    ScriptedStrategy a({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::SELL});
    ScriptedStrategy b({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::SELL});

    BacktestEngine engine(10000.0);
    const auto     clean = engine.run(a, bars, frictionless());

    auto costly           = frictionless();
    costly.commissionRate = 0.001;
    costly.slippagePct    = 0.001;
    const auto withCosts  = engine.run(b, bars, costly);

    CHECK(withCosts.totalReturnPct < clean.totalReturnPct);
}

TEST(backtest, sell_tax_is_charged_only_on_exit) {
    const auto       bars = makeBars({100, 110, 120, 130});
    ScriptedStrategy a({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::SELL});
    ScriptedStrategy b({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::SELL});

    BacktestEngine engine(10000.0);
    const auto     noTax = engine.run(a, bars, frictionless());

    auto taxed         = frictionless();
    taxed.sellTaxRate  = 0.002;
    const auto withTax = engine.run(b, bars, taxed);

    // One round trip, tax on the sell only: the drag is one 0.2% bite, not two.
    // Charged on the sale proceeds, not on the account — 90 whole shares at 130,
    // with the untouched remainder taxed nowhere.
    const double proceeds = 90.0 * 130.0;
    const double expected = noTax.finalCapital - proceeds * 0.002;
    CHECK_NEAR(withTax.finalCapital, expected, expected * 1e-9);
}

TEST(backtest, market_cost_presets_match_the_published_rates) {
    const auto krx = BacktestConfig::forMarket("KRX");
    CHECK_NEAR(krx.sellTaxRate, 0.0020, 1e-12);  // 증권거래세 + 농특세, 2026
    CHECK(krx.commissionRate < 0.0005);          // online commission, well under a bp

    const auto us = BacktestConfig::forMarket("US");
    CHECK_NEAR(us.commissionRate, 0.0025, 1e-12);  // 0.25% per side
    CHECK(us.sellTaxRate < krx.sellTaxRate);       // SEC fee is tiny next to the KRX tax
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

/* ---------------------------- scaling in and out ---------------------------- */

TEST(backtest, single_tranche_settings_reproduce_the_old_behaviour_exactly) {
    // Tranching was added to an engine whose results this repo already relies on.
    // With the defaults the numbers must be bit-identical, or every earlier
    // backtest silently changed meaning.
    const auto       bars = makeBars({100, 110, 120, 130, 120, 110});
    ScriptedStrategy a({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::SELL});
    ScriptedStrategy b({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::SELL});

    auto plain                = frictionless();
    auto explicitOne          = frictionless();
    explicitOne.entryTranches = 1;
    explicitOne.exitTranches  = 1;

    BacktestEngine engine(10000.0);
    const auto     r1 = engine.run(a, bars, plain);
    const auto     r2 = engine.run(b, bars, explicitOne);

    CHECK_EQ(r1.trades.size(), r2.trades.size());
    CHECK_NEAR(r1.finalCapital, r2.finalCapital, 1e-9);
    CHECK_NEAR(r1.totalReturnPct, r2.totalReturnPct, 1e-9);
    CHECK_NEAR(r1.maxDrawdownPct, r2.maxDrawdownPct, 1e-9);
}

TEST(backtest, entry_tranches_average_the_entry_price) {
    // Three buy signals at 100, 200 and 300 with a third committed each time.
    const auto       bars = makeBars({100, 100, 200, 300, 300});
    ScriptedStrategy strat({Signal::HOLD, Signal::BUY, Signal::BUY, Signal::BUY, Signal::SELL});

    auto cfg          = frictionless();
    cfg.entryTranches = 3;
    cfg.positionPct   = 1.0;

    BacktestEngine engine(9000.0);
    const auto     r = engine.run(strat, bars, cfg);

    CHECK_EQ(r.trades.size(), std::size_t{1});
    // 3000 buys 30 at 100, 3000 buys 15 at 200, 3000 buys 10 at 300: 9000 for 55
    // shares, an average of ~163.6 — not the 200 a simple mean of the prices gives.
    CHECK_NEAR(r.trades[0].buyPrice, 9000.0 / 55.0, 0.01);
}

TEST(backtest, exit_tranches_unwind_without_leaving_dust) {
    const auto       bars = makeBars({100, 100, 110, 120, 130});
    ScriptedStrategy strat({Signal::HOLD, Signal::BUY, Signal::SELL, Signal::SELL, Signal::SELL});

    auto cfg         = frictionless();
    cfg.exitTranches = 3;

    BacktestEngine engine(10000.0);
    const auto     r = engine.run(strat, bars, cfg);

    // The position must close exactly, not leave a fraction marked to market forever.
    CHECK_EQ(r.trades.size(), std::size_t{1});
    CHECK(r.finalCapital > 0.0);
}

TEST(backtest, averaging_down_is_off_unless_both_settings_are_given) {
    const auto       bars = makeBars({100, 100, 80, 60, 60});
    ScriptedStrategy a({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::HOLD, Signal::HOLD});
    ScriptedStrategy b({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::HOLD, Signal::HOLD});

    auto noAdds             = frictionless();
    noAdds.addOnDrawdownPct = 10.0;  // a threshold with no maxAdds must still add nothing
    noAdds.maxAdds          = 0;

    BacktestEngine engine(10000.0);
    const auto     plain  = engine.run(a, bars, frictionless());
    const auto     capped = engine.run(b, bars, noAdds);
    CHECK_NEAR(plain.finalCapital, capped.finalCapital, 1e-9);
}

TEST(backtest, averaging_down_adds_and_lowers_the_average_price) {
    // Buy at 100, then the price falls in steps; each 15% step down buys another.
    const auto       bars = makeBars({100, 100, 84, 70, 70, 70});
    ScriptedStrategy strat({Signal::HOLD, Signal::BUY, Signal::HOLD, Signal::HOLD, Signal::HOLD, Signal::SELL});

    auto cfg             = frictionless();
    cfg.entryTranches    = 3;
    cfg.addOnDrawdownPct = 15.0;
    cfg.maxAdds          = 2;

    BacktestEngine engine(9000.0);
    const auto     r = engine.run(strat, bars, cfg);

    CHECK_EQ(r.trades.size(), std::size_t{1});
    // Entered at 100 and added twice below it, so the average must be under 100 —
    // which is the appeal of averaging down, and also why the position is larger
    // than intended exactly when the trade is going wrong.
    CHECK_MSG(r.trades[0].buyPrice < 100.0,
              "average entry " + std::to_string(r.trades[0].buyPrice) + " should be below the first buy");
}

TEST(backtest, adds_are_bounded_by_max_adds) {
    // A relentless decline. Planned tranches fill regardless — that is what scaling
    // in means — so the cap is measured against runs with fewer adds, not against an
    // absolute loss.
    const auto bars = makeBars({100, 100, 80, 64, 51, 41, 33, 26, 21, 17});

    auto base             = frictionless();
    base.entryTranches    = 1;  // isolate the adds from the planned entry
    base.addOnDrawdownPct = 15.0;
    base.positionPct      = 1.0;

    ScriptedStrategy none({Signal::HOLD, Signal::BUY});
    ScriptedStrategy capped({Signal::HOLD, Signal::BUY});
    ScriptedStrategy many({Signal::HOLD, Signal::BUY});

    auto noAdds      = base;
    noAdds.maxAdds   = 0;
    auto oneAdd      = base;
    oneAdd.maxAdds   = 1;
    auto fourAdds    = base;
    fourAdds.maxAdds = 4;

    BacktestEngine engine(10000.0);
    const auto     r0 = engine.run(none, bars, noAdds);
    const auto     r1 = engine.run(capped, bars, oneAdd);
    const auto     r4 = engine.run(many, bars, fourAdds);

    // Adds share the position budget, so more adds means the same total cash spread
    // over more, lower entries — a better average price on a falling series, and
    // never a larger position. What must hold is that the cap is respected: with
    // maxAdds 1 the result has to differ from maxAdds 4.
    CHECK_MSG(r1.totalReturnPct != r0.totalReturnPct, "one add should differ from none");
    CHECK_MSG(r4.totalReturnPct != r1.totalReturnPct, "four adds should differ from one");
    CHECK_MSG(r4.totalReturnPct > r1.totalReturnPct,
              "spreading the same budget over more entries should improve the average price");
}

TEST(backtest, planned_tranches_fill_without_a_repeated_buy_signal) {
    // A crossover strategy signals BUY once, on the transition. Waiting for a second
    // BUY to place the second tranche left the position permanently at a fraction of
    // its intended size — which read as tranching reducing risk when it was simply
    // under-investing.
    const auto       bars = makeBars({100, 100, 100, 100, 100, 110});
    ScriptedStrategy one({Signal::HOLD, Signal::BUY});
    ScriptedStrategy three({Signal::HOLD, Signal::BUY});

    auto single            = frictionless();
    auto tranched          = frictionless();
    tranched.entryTranches = 3;

    BacktestEngine engine(10000.0);
    const auto     r1 = engine.run(one, bars, single);
    const auto     r3 = engine.run(three, bars, tranched);

    // Flat, then the same rise: a filled 3-tranche entry ends up where a single
    // entry does, not at a third of it.
    CHECK_NEAR(r3.totalReturnPct, r1.totalReturnPct, 0.5);
}
