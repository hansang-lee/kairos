#include <cmath>
#include <memory>

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

    const auto                 bh = portfolio::buyAndHold(d, 1000000.0);
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
