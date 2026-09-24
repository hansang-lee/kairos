#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "portfolio/portfolio_engine.hpp"
#include "portfolio/strategies.hpp"
#include "test_framework.hpp"

namespace {

std::shared_ptr<StockInfo> series(const std::string& ticker, const std::vector<double>& closes,
                                  int64_t startTs = 1600000000, int64_t step = 86400) {
    auto s    = std::make_shared<StockInfo>();
    s->ticker = ticker;
    for (std::size_t i = 0; i < closes.size(); ++i) {
        s->timestamps.push_back(startTs + static_cast<int64_t>(i) * step);
        s->open.push_back(closes[i]);
        s->high.push_back(closes[i]);
        s->low.push_back(closes[i]);
        s->close.push_back(closes[i]);
        s->volume.push_back(1000);
    }
    return s;
}

portfolio::PortfolioConfigBt frictionless(int rebalance = 1) {
    portfolio::PortfolioConfigBt c;
    c.commissionRate     = 0.0;
    c.slippagePct        = 0.0;
    c.sellTaxRate        = 0.0;
    c.rebalanceEveryBars = rebalance;
    c.minWeightChange    = 0.0;
    return c;
}

/** A constant-price series: nothing the market does can be mistaken for a fee. */
std::shared_ptr<StockInfo> flat(const std::string& ticker, double price, std::size_t bars,
                                int64_t startTs = 1600000000) {
    return series(ticker, std::vector<double>(bars, price), startTs);
}

/**
 * Frictionless, and only ever rebalancing on the first bar.
 *
 * The expense tests need a position that is bought once and then left alone: any
 * later rebalance would move value between assets and blur which asset paid what.
 */
portfolio::PortfolioConfigBt buyOnceAndHold() {
    return frictionless(1000000);
}

/**
 * Holds the weights it was handed, from the very first bar.
 *
 * Test-only, so that "fully invested from bar 0 to the end" is an exact
 * statement rather than something inferred from a strategy's warmup and ranking.
 */
class FixedWeights: public portfolio::IPortfolioStrategy {
   public:
    explicit FixedWeights(std::vector<double> weights)
        : weights_(std::move(weights)) {}

    [[nodiscard]] std::string name() const override { return "Fixed weights (test)"; }
    void                      init(const portfolio::PortfolioData&) override {}
    [[nodiscard]] std::size_t warmupPeriod() const override { return 0; }

    [[nodiscard]] std::vector<double> targetWeights(const portfolio::PortfolioData&, std::size_t) override {
        return weights_;
    }

   private:
    std::vector<double> weights_;
};

}  // namespace

/* ------------------------------- alignment ------------------------------- */

TEST(portfolio, assets_on_different_calendars_share_one_timeline) {
    // One asset trades every day, the other every second day — a local holiday.
    auto daily = series("A", {100, 101, 102, 103});
    auto gappy = series("B", {200, 202}, 1600000000, 2 * 86400);

    const auto d = portfolio::PortfolioData::align({daily, gappy});

    // The union keeps all four days; an intersection would have thrown half away.
    CHECK_EQ(d.barCount(), std::size_t{4});
    CHECK_EQ(d.assetCount(), std::size_t{2});
    // B's price is carried forward on the days it did not trade.
    CHECK_NEAR(d.close[1][1], 200.0, 1e-9);
    CHECK_NEAR(d.close[1][2], 202.0, 1e-9);
}

TEST(portfolio, bars_before_an_asset_exists_are_marked_unavailable) {
    // B lists two days late. Its price must not be back-filled into a period when
    // it could not have been bought.
    auto early = series("A", {100, 101, 102, 103});
    auto late  = series("B", {200, 201}, 1600000000 + 2 * 86400);

    const auto d = portfolio::PortfolioData::align({early, late});
    CHECK(!d.available[1][0]);
    CHECK(!d.available[1][1]);
    CHECK(d.available[1][2]);
}

TEST(portfolio, an_empty_universe_produces_an_empty_dataset) {
    const auto d = portfolio::PortfolioData::align({});
    CHECK_EQ(d.assetCount(), std::size_t{0});
    CHECK_EQ(d.barCount(), std::size_t{0});
}

/* --------------------------------- engine --------------------------------- */

TEST(portfolio, equal_weight_tracks_buy_and_hold_when_trading_is_free) {
    // Two assets rising at different rates. Rebalancing sells the winner to buy the
    // laggard, so the results differ — but both must be in the same region, not
    // orders of magnitude apart, which is what a sizing bug would produce.
    auto a = series("A", std::vector<double>(200, 0.0));
    auto b = series("B", std::vector<double>(200, 0.0));
    for (std::size_t i = 0; i < 200; ++i) {
        a->close[i] = a->open[i] = a->high[i] = a->low[i] = 100.0 * std::pow(1.001, static_cast<double>(i));
        b->close[i] = b->open[i] = b->high[i] = b->low[i] = 100.0 * std::pow(1.0005, static_cast<double>(i));
    }
    const auto d = portfolio::PortfolioData::align({a, b});

    const auto                 bh = portfolio::buyAndHold(d, frictionless(), 1000000.0);
    portfolio::EqualWeight     ew;
    portfolio::PortfolioEngine engine(1000000.0);
    const auto                 rebalanced = engine.run(ew, d, frictionless(21));

    CHECK(bh.totalReturnPct > 0.0);
    CHECK(rebalanced.totalReturnPct > 0.0);
    CHECK_MSG(std::fabs(rebalanced.totalReturnPct - bh.totalReturnPct) < 10.0,
              "rebalanced " + std::to_string(rebalanced.totalReturnPct) + "% vs buy-and-hold "
                  + std::to_string(bh.totalReturnPct) + "%");
}

TEST(portfolio, costs_are_charged_and_reduce_the_result) {
    auto a = series("A", std::vector<double>(300, 0.0));
    auto b = series("B", std::vector<double>(300, 0.0));
    for (std::size_t i = 0; i < 300; ++i) {
        const double wave = 100.0 + 20.0 * std::sin(static_cast<double>(i) / 10.0);
        a->close[i] = a->open[i] = a->high[i] = a->low[i] = wave;
        b->close[i] = b->open[i] = b->high[i] = b->low[i] = 200.0 - wave;
    }
    const auto d = portfolio::PortfolioData::align({a, b});

    portfolio::EqualWeight     free1, charged1;
    portfolio::PortfolioEngine engine(1000000.0);

    auto costly           = frictionless(5);
    costly.commissionRate = 0.001;
    costly.sellTaxRate    = 0.002;

    const auto cheap = engine.run(free1, d, frictionless(5));
    const auto dear  = engine.run(charged1, d, costly);

    CHECK_NEAR(cheap.totalCosts, 0.0, 1e-6);
    CHECK_MSG(dear.totalCosts > 0.0, "costs were never charged");
    CHECK_MSG(dear.totalReturnPct < cheap.totalReturnPct, "charging costs did not reduce the result");
}

TEST(portfolio, a_strategy_holding_nothing_stays_in_cash) {
    // Momentum with a floor nothing clears: every asset is falling.
    auto a = series("A", std::vector<double>(200, 0.0));
    for (std::size_t i = 0; i < 200; ++i) {
        a->close[i] = a->open[i] = a->high[i] = a->low[i] = 100.0 * std::pow(0.995, static_cast<double>(i));
    }
    const auto d = portfolio::PortfolioData::align({a});

    portfolio::MomentumRotation rotation(1, 60, 0.0);  // must be up over 60 bars to hold
    portfolio::PortfolioEngine  engine(1000000.0);
    const auto                  r = engine.run(rotation, d, frictionless(5));

    // Cash through a 63% decline: the account must be essentially untouched.
    CHECK_MSG(r.totalReturnPct > -1.0,
              "stayed invested through a decline it was told to sit out: " + std::to_string(r.totalReturnPct) + "%");
}

/* ------------------------------- weighting ------------------------------- */

TEST(portfolio, momentum_holds_the_strongest_and_leaves_the_rest) {
    auto strong = series("S", std::vector<double>(200, 0.0));
    auto weak   = series("W", std::vector<double>(200, 0.0));
    for (std::size_t i = 0; i < 200; ++i) {
        strong->close[i] = strong->open[i] = strong->high[i] = strong->low[i] =
            100.0 * std::pow(1.002, static_cast<double>(i));
        weak->close[i] = weak->open[i] = weak->high[i] = weak->low[i] =
            100.0 * std::pow(1.0001, static_cast<double>(i));
    }
    const auto d = portfolio::PortfolioData::align({strong, weak});

    portfolio::MomentumRotation rotation(1, 60, 0.0);
    rotation.init(d);
    const auto w = rotation.targetWeights(d, 150);

    CHECK_MSG(w[0] > w[1], "the faster riser was not the one held");
    CHECK_NEAR(w[1], 0.0, 1e-9);
}

TEST(portfolio, risk_parity_gives_the_quieter_asset_the_larger_share) {
    auto calm = series("C", std::vector<double>(200, 0.0));
    auto wild = series("V", std::vector<double>(200, 0.0));
    for (std::size_t i = 0; i < 200; ++i) {
        calm->close[i] = calm->open[i] = calm->high[i] = calm->low[i] = 100.0 + 0.5 * std::sin(static_cast<double>(i));
        wild->close[i] = wild->open[i] = wild->high[i] = wild->low[i] = 100.0 + 20.0 * std::sin(static_cast<double>(i));
    }
    const auto d = portfolio::PortfolioData::align({calm, wild});

    portfolio::RiskParity rp(60, 1.0);
    rp.init(d);
    const auto w = rp.targetWeights(d, 150);

    // The whole point: equal weight would let the wild asset supply all the movement.
    CHECK_MSG(w[0] > w[1], "the calmer asset did not get the larger weight");
}

TEST(portfolio, risk_parity_respects_its_per_asset_cap) {
    auto calm = series("C", std::vector<double>(200, 0.0));
    auto wild = series("V", std::vector<double>(200, 0.0));
    for (std::size_t i = 0; i < 200; ++i) {
        calm->close[i] = calm->open[i] = calm->high[i] = calm->low[i] = 100.0 + 0.01 * std::sin(static_cast<double>(i));
        wild->close[i] = wild->open[i] = wild->high[i] = wild->low[i] = 100.0 + 30.0 * std::sin(static_cast<double>(i));
    }
    const auto d = portfolio::PortfolioData::align({calm, wild});

    portfolio::RiskParity rp(60, 0.4);
    rp.init(d);
    const auto w = rp.targetWeights(d, 150);

    // Without a cap, an almost-flat series takes essentially the whole portfolio.
    for (const double x : w) {
        CHECK_MSG(x <= 0.4 + 1e-9, "weight " + std::to_string(x) + " exceeded the 0.4 cap");
    }
}

TEST(portfolio, weights_never_exceed_the_account) {
    auto       a = series("A", std::vector<double>(200, 100.0));
    auto       b = series("B", std::vector<double>(200, 100.0));
    auto       c = series("C", std::vector<double>(200, 100.0));
    const auto d = portfolio::PortfolioData::align({a, b, c});

    portfolio::EqualWeight ew;
    ew.init(d);
    const auto w = ew.targetWeights(d, 150);

    double sum = 0.0;
    for (const double x : w) {
        sum += x;
    }
    // Leverage is not modelled, so a target above 1.0 would silently mean borrowing.
    CHECK_MSG(sum <= 1.0 + 1e-9, "weights summed to " + std::to_string(sum));
}

/* ----------------------------- expense ratios ----------------------------- */

TEST(portfolio, expense_ratio_is_looked_up_per_asset_and_falls_back_past_the_end) {
    portfolio::PortfolioConfigBt c;
    c.expenseRatios       = {0.0015, 0.0079};
    c.defaultExpenseRatio = 0.0030;

    CHECK_NEAR(portfolio::expenseRatioFor(c, 0), 0.0015, 1e-12);
    CHECK_NEAR(portfolio::expenseRatioFor(c, 1), 0.0079, 1e-12);
    // Past the end is the common case: a universe grows, the config does not.
    CHECK_NEAR(portfolio::expenseRatioFor(c, 2), 0.0030, 1e-12);
    CHECK_NEAR(portfolio::expenseRatioFor(c, 99), 0.0030, 1e-12);
}

TEST(portfolio, an_empty_expense_ratio_list_charges_the_default_to_every_asset) {
    portfolio::PortfolioConfigBt c;
    c.defaultExpenseRatio = 0.005;
    CHECK(c.expenseRatios.empty());
    CHECK_NEAR(portfolio::expenseRatioFor(c, 0), 0.005, 1e-12);
    CHECK_NEAR(portfolio::expenseRatioFor(c, 7), 0.005, 1e-12);

    // And an untouched config costs nothing, so adding the field changed no result.
    const portfolio::PortfolioConfigBt untouched;
    CHECK_NEAR(portfolio::expenseRatioFor(untouched, 0), 0.0, 1e-12);
    CHECK_NEAR(portfolio::expenseRatioFor(untouched, 3), 0.0, 1e-12);
}

TEST(portfolio, a_negative_expense_ratio_is_clamped_rather_than_paid_out_as_a_rebate) {
    portfolio::PortfolioConfigBt c;
    c.expenseRatios       = {-0.01, 0.002};
    c.defaultExpenseRatio = -0.05;

    // A negative rate is a typo in a config. Honouring it would hand the account
    // free money every bar and make the fee look like alpha.
    CHECK_NEAR(portfolio::expenseRatioFor(c, 0), 0.0, 1e-12);
    CHECK_NEAR(portfolio::expenseRatioFor(c, 1), 0.002, 1e-12);
    CHECK_NEAR(portfolio::expenseRatioFor(c, 5), 0.0, 1e-12);
}

TEST(portfolio, a_zero_expense_ratio_leaves_every_existing_number_untouched) {
    auto a = series("A", std::vector<double>(300, 0.0));
    auto b = series("B", std::vector<double>(300, 0.0));
    for (std::size_t i = 0; i < 300; ++i) {
        const double wave = 100.0 + 20.0 * std::sin(static_cast<double>(i) / 10.0);
        a->close[i] = a->open[i] = a->high[i] = a->low[i] = wave;
        b->close[i] = b->open[i] = b->high[i] = b->low[i] = 200.0 - wave;
    }
    const auto d = portfolio::PortfolioData::align({a, b});

    const portfolio::PortfolioConfigBt untouched;  // fee fields left at their defaults
    portfolio::PortfolioConfigBt       explicitZero = untouched;
    explicitZero.expenseRatios.clear();
    explicitZero.defaultExpenseRatio = 0.0;
    explicitZero.barsPerYear         = 252.0;

    portfolio::EqualWeight     s1, s2;
    portfolio::PortfolioEngine engine(1000000.0);
    const auto                 base = engine.run(s1, d, untouched);
    const auto                 zero = engine.run(s2, d, explicitZero);

    // The regression guard: a fee of zero must be arithmetically invisible, not
    // "close enough". Anything that rounds, reorders or re-derives equity on the
    // way through the new code shows up here.
    CHECK_EQ(base.totalFees, 0.0);
    CHECK_EQ(zero.totalFees, 0.0);
    CHECK_EQ(base.finalCapital, zero.finalCapital);
    CHECK_EQ(base.totalCosts, zero.totalCosts);
    CHECK_EQ(base.orders, zero.orders);
    CHECK_EQ(base.rebalances, zero.rebalances);
    CHECK_EQ(base.equityCurve.size(), zero.equityCurve.size());
    for (std::size_t i = 0; i < base.equityCurve.size(); ++i) {
        CHECK_EQ(base.equityCurve[i], zero.equityCurve[i]);
    }
    // Bar 0 is never charged, so the curve still opens at the account's full size.
    CHECK_NEAR(base.equityCurve.front(), 1000000.0, 1e-9);
}

TEST(portfolio, the_expense_haircut_compounds_to_exactly_the_configured_annual_rate) {
    constexpr std::size_t bars    = 252;
    constexpr double      rate    = 0.10;  // 10% a year: far larger than any rounding
    const double          initial = 1000000.0;

    const auto d = portfolio::PortfolioData::align({flat("A", 100.0, bars)});

    auto cfg                = buyOnceAndHold();
    cfg.expenseRatios       = {rate};
    cfg.defaultExpenseRatio = 0.0;
    cfg.barsPerYear         = 252.0;

    FixedWeights               fullyInvested({1.0});
    portfolio::PortfolioEngine engine(initial);
    const auto                 r = engine.run(fullyInvested, d, cfg);

    // Flat price, no commission, no slippage, no tax, one entry and no further
    // trades: the fee is the only thing in the run that can move the account.
    // One haircut for each bar after the entry bar.
    const double expected = initial * std::pow(1.0 - rate / cfg.barsPerYear, static_cast<double>(bars - 1));

    CHECK_EQ(r.totalCosts, 0.0);
    CHECK_NEAR(r.finalCapital, expected, initial * 1e-9);
    CHECK_NEAR(r.equityCurve.back(), expected, initial * 1e-9);
    // Reported fees must be the value actually given up, not a separate estimate.
    CHECK_NEAR(r.totalFees, initial - expected, initial * 1e-9);
    CHECK_MSG(r.totalFees > initial * 0.09,
              "a 10%/yr ratio over a year surrendered only " + std::to_string(r.totalFees));
    CHECK_MSG(r.totalFees < initial * 0.11,
              "a 10%/yr ratio over a year surrendered " + std::to_string(r.totalFees) + " — proration is wrong");
}

TEST(portfolio, each_asset_pays_its_own_expense_ratio_not_a_shared_one) {
    constexpr std::size_t bars      = 252;
    constexpr double      cheapRate = 0.001;
    constexpr double      dearRate  = 0.10;
    const double          initial   = 1000000.0;

    const auto d = portfolio::PortfolioData::align({flat("CHEAP", 100.0, bars), flat("DEAR", 50.0, bars)});
    // The lookup is positional, so the order the test assumes has to be the order
    // align produced.
    CHECK_EQ(d.tickers[0], std::string("CHEAP"));
    CHECK_EQ(d.tickers[1], std::string("DEAR"));

    auto cfg                = buyOnceAndHold();
    cfg.expenseRatios       = {cheapRate, dearRate};
    cfg.defaultExpenseRatio = 0.0;

    // Each sleeve alone, because a result reports one number and two fees charged
    // inside it cannot otherwise be told apart.
    FixedWeights               onlyCheap({1.0, 0.0});
    FixedWeights               onlyDear({0.0, 1.0});
    portfolio::PortfolioEngine engine(initial);
    const auto                 cheap = engine.run(onlyCheap, d, cfg);
    const auto                 dear  = engine.run(onlyDear, d, cfg);

    const double n        = static_cast<double>(bars - 1);
    const double expCheap = initial * std::pow(1.0 - cheapRate / 252.0, n);
    const double expDear  = initial * std::pow(1.0 - dearRate / 252.0, n);

    CHECK_MSG(cheap.finalCapital > dear.finalCapital,
              "the cheaper fund did not end up worth more: " + std::to_string(cheap.finalCapital) + " vs "
                  + std::to_string(dear.finalCapital));
    CHECK_NEAR(cheap.finalCapital, expCheap, initial * 1e-9);
    CHECK_NEAR(dear.finalCapital, expDear, initial * 1e-9);
    // Not just "more", but more by the amount the two rates differ by.
    CHECK_NEAR(cheap.finalCapital / dear.finalCapital,
               std::pow((1.0 - cheapRate / 252.0) / (1.0 - dearRate / 252.0), n), 1e-9);
    CHECK_MSG(dear.totalFees > cheap.totalFees * 50.0, "the expensive fund did not pay a proportionally larger fee");

    // And both sleeves inside one account, each still on its own rate: an
    // implementation charging one shared rate lands halfway between these.
    FixedWeights both({0.5, 0.5});
    const auto   mixed = engine.run(both, d, cfg);
    CHECK_NEAR(mixed.finalCapital, 0.5 * expCheap + 0.5 * expDear, initial * 1e-7);
}

TEST(portfolio, a_strategy_holding_nothing_pays_no_management_fee) {
    const auto d = portfolio::PortfolioData::align({flat("A", 100.0, 252), flat("B", 100.0, 252)});

    auto cfg                = frictionless(1);
    cfg.expenseRatios       = {0.50, 0.50};  // 50% a year: impossible to miss if charged
    cfg.defaultExpenseRatio = 0.50;

    FixedWeights               nothing({0.0, 0.0});
    portfolio::PortfolioEngine engine(1000000.0);
    const auto                 r = engine.run(nothing, d, cfg);

    // A fee charged against the account rather than against the holdings would
    // bill a portfolio sitting entirely in cash.
    CHECK_EQ(r.totalFees, 0.0);
    CHECK_EQ(r.totalCosts, 0.0);
    CHECK_NEAR(r.finalCapital, 1000000.0, 1e-6);
}

/* -------------------------- benchmark cost model -------------------------- */

TEST(portfolio, buy_and_hold_pays_a_round_trip_and_keeps_its_curve_mark_to_market) {
    constexpr std::size_t bars    = 60;
    const double          initial = 1000000.0;
    const auto            d       = portfolio::PortfolioData::align({flat("A", 100.0, bars), flat("B", 250.0, bars)});

    portfolio::PortfolioConfigBt cfg;
    cfg.commissionRate      = 0.001;
    cfg.slippagePct         = 0.0005;
    cfg.sellTaxRate         = 0.002;
    cfg.expenseRatios       = {};
    cfg.defaultExpenseRatio = 0.0;  // isolate the trading costs from the fee

    const auto r = portfolio::buyAndHold(d, cfg, initial);

    // Flat prices, so the entire difference is the cost of getting in and out.
    const double entered  = initial / ((1.0 + cfg.slippagePct) * (1.0 + cfg.commissionRate));
    const double expected = entered * (1.0 - cfg.slippagePct) * (1.0 - cfg.commissionRate) * (1.0 - cfg.sellTaxRate);

    CHECK_MSG(r.finalCapital < initial, "a round trip through a flat market cost the benchmark nothing");
    CHECK_MSG(r.totalCosts > 0.0, "the benchmark never recorded a cost");
    CHECK_EQ(r.totalFees, 0.0);
    // Tight enough that dropping any one of commission, slippage or tax fails it:
    // the smallest of the three is 0.05% of the account, twenty times this band.
    CHECK_NEAR(r.finalCapital, expected, initial * 1e-4);
    CHECK_NEAR(r.totalCosts, initial - expected, initial * 1e-4);

    // The curve values the position at the market, so only finalCapital carries
    // the exit haircut; baking it into the last bar would understate the drawdown
    // of every strategy compared against this curve.
    CHECK_NEAR(r.equityCurve.back(), entered, initial * 1e-4);
    CHECK_MSG(r.equityCurve.back() > r.finalCapital, "the exit cost was baked into the equity curve");
}

TEST(portfolio, buy_and_hold_with_costs_disabled_gives_back_exactly_what_it_started_with) {
    const auto   d       = portfolio::PortfolioData::align({flat("A", 100.0, 120), flat("B", 33.0, 120)});
    const double initial = 1000000.0;

    const auto r = portfolio::buyAndHold(d, frictionless(), initial);

    // The cost model must not leak when it is switched off: a flat market with no
    // commission, slippage, tax or fee is a zero-return round trip.
    CHECK_NEAR(r.finalCapital, initial, initial * 1e-9);
    CHECK_NEAR(r.equityCurve.front(), initial, initial * 1e-9);
    CHECK_NEAR(r.equityCurve.back(), initial, initial * 1e-9);
    CHECK_NEAR(r.totalCosts, 0.0, 1e-6);
    CHECK_EQ(r.totalFees, 0.0);
    CHECK_NEAR(r.totalReturnPct, 0.0, 1e-7);
}

TEST(portfolio, buy_and_hold_holds_an_unlisted_asset_slice_as_cash_rather_than_losing_it) {
    constexpr std::size_t bars  = 120;
    auto                  early = flat("A", 100.0, bars);
    auto                  late  = flat("B", 200.0, bars - 40, 1600000000 + 40 * 86400);
    const auto            d     = portfolio::PortfolioData::align({early, late});

    CHECK_EQ(d.assetCount(), std::size_t{2});
    CHECK_EQ(d.barCount(), bars);
    CHECK(!d.available[1][0]);

    const double initial = 1000000.0;
    const auto   r       = portfolio::buyAndHold(d, frictionless(), initial);

    // Half the account has nothing to buy on day one. Sized as a zero share count
    // with no cash standing behind it, the benchmark silently begins the run at
    // half size and then loses to every strategy for a reason that is arithmetic.
    CHECK_NEAR(r.equityCurve.front(), initial, initial * 1e-9);
    CHECK_NEAR(r.finalCapital, initial, initial * 1e-9);
    CHECK_NEAR(r.totalReturnPct, 0.0, 1e-7);
}
