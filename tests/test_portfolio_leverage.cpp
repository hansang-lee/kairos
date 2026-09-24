#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "portfolio/portfolio_engine.hpp"
#include "portfolio/strategies.hpp"
#include "test_framework.hpp"

/*
 * Borrowing, its interest, and the two ways a levered account ends badly.
 *
 * Kept apart from test_portfolio.cpp because these tests all share one setup that
 * the others must not use: a position bought once at the start and never adjusted,
 * so that what happens to the account afterwards is the market and the loan
 * rather than the rebalancing rule.
 */
namespace {

std::shared_ptr<StockInfo> lvSeries(const std::string& ticker, const std::vector<double>& closes) {
    auto s    = std::make_shared<StockInfo>();
    s->ticker = ticker;
    for (std::size_t i = 0; i < closes.size(); ++i) {
        s->timestamps.push_back(1600000000 + static_cast<int64_t>(i) * 86400);
        s->open.push_back(closes[i]);
        s->high.push_back(closes[i]);
        s->low.push_back(closes[i]);
        s->close.push_back(closes[i]);
        s->volume.push_back(1000);
    }
    return s;
}

/** Frictionless, and rebalancing only on the first bar it is allowed to. */
portfolio::PortfolioConfigBt onceOnly() {
    portfolio::PortfolioConfigBt c;
    c.commissionRate     = 0.0;
    c.slippagePct        = 0.0;
    c.sellTaxRate        = 0.0;
    c.rebalanceEveryBars = 1000000;
    c.minWeightChange    = 0.0;
    c.minPositionDrift   = 0.0;
    return c;
}

class LvFixed: public portfolio::IPortfolioStrategy {
   public:
    explicit LvFixed(std::vector<double> weights)
        : weights_(std::move(weights)) {}

    [[nodiscard]] std::string name() const override { return "Fixed weights (leverage test)"; }
    void                      init(const portfolio::PortfolioData&) override {}
    [[nodiscard]] std::size_t warmupPeriod() const override { return 0; }

    [[nodiscard]] std::vector<double> targetWeights(const portfolio::PortfolioData&, std::size_t) override {
        return weights_;
    }

   private:
    std::vector<double> weights_;
};

}  // namespace

/* --------------------------- the unlevered case --------------------------- */

TEST(leverage, an_unlevered_config_is_untouched_by_the_borrowing_code) {
    const auto d =
        portfolio::PortfolioData::align({lvSeries("A", {100, 110, 90, 120, 105}), lvSeries("B", {50, 52, 48, 55, 51})});

    portfolio::PortfolioEngine engine(1000000.0);

    auto       cfg = onceOnly();
    LvFixed    s1({0.5, 0.5});
    const auto plain = engine.run(s1, d, cfg);

    // Spelling out the defaults must not change a single number, or every result
    // recorded before borrowing existed is no longer comparable.
    cfg.maxLeverage        = 1.0;
    cfg.marginRateAnnual   = 0.08;  // deliberately large: it must never be charged
    cfg.marginCallLeverage = 1.5;
    LvFixed    s2({0.5, 0.5});
    const auto levered = engine.run(s2, d, cfg);

    CHECK_EQ(levered.finalCapital, plain.finalCapital);
    CHECK_EQ(levered.totalInterest, 0.0);
    CHECK_EQ(levered.marginCalls, std::size_t{0});
    CHECK(!levered.ruined);
    CHECK_EQ(levered.equityCurve.size(), plain.equityCurve.size());
    for (std::size_t i = 0; i < plain.equityCurve.size(); ++i) {
        CHECK_EQ(levered.equityCurve[i], plain.equityCurve[i]);
    }
}

TEST(leverage, an_account_that_never_borrows_pays_no_interest) {
    const auto d         = portfolio::PortfolioData::align({lvSeries("A", std::vector<double>(60, 100.0))});
    auto       cfg       = onceOnly();
    cfg.maxLeverage      = 2.0;
    cfg.marginRateAnnual = 0.10;

    // Half invested, so cash stays positive for the whole run.
    LvFixed                    s({0.5});
    portfolio::PortfolioEngine engine(1000000.0);
    const auto                 r = engine.run(s, d, cfg);

    CHECK_EQ(r.totalInterest, 0.0);
    CHECK_NEAR(r.finalCapital, 1000000.0, 1e-6);
}

/* -------------------------------- interest -------------------------------- */

TEST(leverage, interest_is_charged_on_the_borrowed_balance_at_the_stated_annual_rate) {
    const std::size_t bars = 253;  // one year of bars, plus the bar that buys
    const auto        d    = portfolio::PortfolioData::align({lvSeries("A", std::vector<double>(bars, 100.0))});

    auto cfg             = onceOnly();
    cfg.maxLeverage      = 2.0;
    cfg.marginRateAnnual = 0.10;

    LvFixed                    s({2.0});  // fully levered: borrow the account again
    portfolio::PortfolioEngine engine(1000000.0);
    const auto                 r = engine.run(s, d, cfg);

    // Bought at bar 0 with 1,000,000 of equity and 1,000,000 borrowed, then charged
    // for 252 bars. The balance compounds, so the drag is slightly above a flat
    // 10% of the loan, and equity shrinking does not shrink the loan.
    CHECK(r.totalInterest > 100000.0);
    CHECK(r.totalInterest < 106000.0);

    // Prices never moved, so every won of the loss is interest.
    CHECK_NEAR(1000000.0 - r.finalCapital, r.totalInterest, 1.0);
}

TEST(leverage, a_higher_rate_costs_proportionally_more) {
    const auto d = portfolio::PortfolioData::align({lvSeries("A", std::vector<double>(60, 100.0))});

    auto cfg        = onceOnly();
    cfg.maxLeverage = 2.0;

    portfolio::PortfolioEngine engine(1000000.0);

    cfg.marginRateAnnual = 0.05;
    LvFixed    a({2.0});
    const auto cheap = engine.run(a, d, cfg);

    cfg.marginRateAnnual = 0.10;
    LvFixed    b({2.0});
    const auto dear = engine.run(b, d, cfg);

    CHECK(cheap.totalInterest > 0.0);
    // Not exactly 2x: the dearer loan eats equity faster, and interest compounds on
    // a balance that the cheaper one leaves smaller.
    CHECK(dear.totalInterest > cheap.totalInterest * 1.99);
    CHECK(dear.totalInterest < cheap.totalInterest * 2.02);
}

/* ------------------------------- the ceiling ------------------------------- */

TEST(leverage, gross_exposure_never_exceeds_the_configured_ceiling) {
    const auto d = portfolio::PortfolioData::align(
        {lvSeries("A", {100, 101, 102, 103, 104, 105}), lvSeries("B", {50, 50, 51, 52, 52, 53})});

    auto cfg               = onceOnly();
    cfg.maxLeverage        = 2.0;
    cfg.rebalanceEveryBars = 1;

    // Asks for five times the account; the engine must trim it to two.
    auto                       inner = std::make_unique<portfolio::EqualWeight>();
    portfolio::Levered         s(std::move(inner), 5.0);
    portfolio::PortfolioEngine engine(1000000.0);
    const auto                 r = engine.run(s, d, cfg);

    CHECK(!r.ruined);
    // Equity is what is left after the loan, so exposure above the ceiling would
    // show up as a final capital far beyond twice the market's move.
    const double marketMove = 105.0 / 100.0;
    CHECK(r.finalCapital < 1000000.0 * (1.0 + 2.05 * (marketMove - 1.0)));
}

TEST(leverage, the_wrapper_scales_weights_and_keeps_the_rule_it_wraps) {
    const auto d = portfolio::PortfolioData::align({lvSeries("A", {100, 100, 100}), lvSeries("B", {100, 100, 100})});

    auto               inner = std::make_unique<portfolio::EqualWeight>();
    const std::size_t  warm  = inner->warmupPeriod();
    portfolio::Levered s(std::move(inner), 2.0);

    s.init(d);
    CHECK_EQ(s.warmupPeriod(), warm);
    CHECK(s.name().find("x2.0") != std::string::npos);

    const auto w = s.targetWeights(d, 2);
    CHECK_EQ(w.size(), std::size_t{2});
    // EqualWeight gives 0.5 each here, so twice that is the whole account twice over.
    CHECK_NEAR(w[0] + w[1], 2.0, 1e-9);
}

/* -------------------------- calls and wipeouts --------------------------- */

TEST(leverage, a_fall_past_the_call_level_forces_the_position_back_to_the_ceiling) {
    // Bought at 100 with 2x exposure, then a 20% fall: exposure rises to 2.67x,
    // past a 2.2x call level, and the broker sells into it.
    const auto d = portfolio::PortfolioData::align({lvSeries("A", {100, 100, 80, 80, 80})});

    auto cfg               = onceOnly();
    cfg.maxLeverage        = 2.0;
    cfg.marginCallLeverage = 2.2;

    LvFixed                    s({2.0});
    portfolio::PortfolioEngine engine(1000000.0);
    const auto                 called = engine.run(s, d, cfg);

    CHECK(called.marginCalls >= std::size_t{1});
    CHECK(!called.ruined);

    // The same fall without a broker watching: the position is never cut, so the
    // subsequent flat bars leave more equity. That difference is what a call costs.
    cfg.marginCallLeverage = 0.0;
    LvFixed    s2({2.0});
    const auto uncalled = engine.run(s2, d, cfg);
    CHECK_EQ(uncalled.marginCalls, std::size_t{0});
    CHECK(uncalled.finalCapital >= called.finalCapital);
}

TEST(leverage, an_account_wiped_out_stops_there_rather_than_recovering_on_paper) {
    // 2x exposure into a 60% fall: the loan is larger than what is left.
    const auto d = portfolio::PortfolioData::align({lvSeries("A", {100, 100, 40, 200, 400})});

    auto cfg        = onceOnly();
    cfg.maxLeverage = 2.0;

    LvFixed                    s({2.0});
    portfolio::PortfolioEngine engine(1000000.0);
    const auto                 r = engine.run(s, d, cfg);

    CHECK(r.ruined);
    CHECK_EQ(r.finalCapital, 0.0);
    // The rebound to 400 must not resurrect the account.
    CHECK_EQ(r.equityCurve.size(), d.barCount());
    CHECK_EQ(r.equityCurve.back(), 0.0);
    CHECK_NEAR(r.maxDrawdownPct, -100.0, 1e-9);
}

TEST(leverage, the_same_fall_unlevered_survives_it) {
    const auto d = portfolio::PortfolioData::align({lvSeries("A", {100, 100, 40, 200, 400})});

    LvFixed                    s({1.0});
    portfolio::PortfolioEngine engine(1000000.0);
    const auto                 r = engine.run(s, d, onceOnly());

    CHECK(!r.ruined);
    // Unlevered, the rebound is kept: this is the asymmetry leverage introduces.
    CHECK(r.finalCapital > 3000000.0);
}
