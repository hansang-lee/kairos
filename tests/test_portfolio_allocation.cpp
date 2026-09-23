#include <cmath>
#include <cstddef>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "portfolio/portfolio_engine.hpp"
#include "portfolio/strategies.hpp"
#include "test_framework.hpp"

/*
 * Allocation rules that group the universe, and the filter that lets one stand aside.
 *
 * These exist because equal weight allocates to however many ways a bet happens to
 * be listed rather than to the bets, so what they must get right is the split
 * itself: which assets share a class, what happens to a class nobody can trade
 * yet, and whether an asset the rule has excluded really goes to cash instead of
 * quietly funding the others.
 */
namespace {

std::shared_ptr<StockInfo> aSeries(const std::string& ticker, const std::vector<double>& closes,
                                   int64_t startTs = 1600000000) {
    auto s    = std::make_shared<StockInfo>();
    s->ticker = ticker;
    for (std::size_t i = 0; i < closes.size(); ++i) {
        s->timestamps.push_back(startTs + static_cast<int64_t>(i) * 86400);
        s->open.push_back(closes[i]);
        s->high.push_back(closes[i]);
        s->low.push_back(closes[i]);
        s->close.push_back(closes[i]);
        s->volume.push_back(1000);
    }
    return s;
}

/** A series that alternates by `swing` percent, so its volatility is a known quantity. */
std::shared_ptr<StockInfo> wobbling(const std::string& ticker, double base, double swingPct, std::size_t bars,
                                    int64_t startTs = 1600000000) {
    std::vector<double> closes;
    closes.reserve(bars);
    for (std::size_t i = 0; i < bars; ++i) {
        closes.push_back(i % 2 == 0 ? base : base * (1.0 + swingPct / 100.0));
    }
    return aSeries(ticker, closes, startTs);
}

double sum(const std::vector<double>& v) {
    return std::accumulate(v.begin(), v.end(), 0.0);
}

/** Hands back whatever it was told to, so a filter can be tested on its own. */
class Says: public portfolio::IPortfolioStrategy {
   public:
    explicit Says(std::vector<double> w)
        : w_(std::move(w)) {}

    [[nodiscard]] std::string name() const override { return "Says"; }
    void                      init(const portfolio::PortfolioData&) override {}
    [[nodiscard]] std::size_t warmupPeriod() const override { return 3; }

    [[nodiscard]] std::vector<double> targetWeights(const portfolio::PortfolioData&, std::size_t) override {
        return w_;
    }

   private:
    std::vector<double> w_;
};

}  // namespace

/* ------------------------------ GroupParity ------------------------------ */

TEST(allocation, each_class_gets_an_equal_share_however_many_ways_it_is_listed) {
    // Three listings of one bet and one of another: the point of the rule is that
    // the first three share a half between them rather than taking three quarters.
    const auto d = portfolio::PortfolioData::align({aSeries("A1", {100, 100, 100, 100}),
                                                    aSeries("A2", {100, 100, 100, 100}),
                                                    aSeries("A3", {100, 100, 100, 100}),
                                                    aSeries("B1", {100, 100, 100, 100})});

    portfolio::GroupParity s({"EQ", "EQ", "EQ", "BOND"});
    s.init(d);
    const auto w = s.targetWeights(d, 3);

    CHECK_NEAR(w[0], 1.0 / 6.0, 1e-9);
    CHECK_NEAR(w[1], 1.0 / 6.0, 1e-9);
    CHECK_NEAR(w[2], 1.0 / 6.0, 1e-9);
    CHECK_NEAR(w[3], 0.5, 1e-9);
    CHECK_NEAR(sum(w), 1.0, 1e-9);
}

TEST(allocation, a_class_nobody_can_trade_yet_hands_its_share_to_the_classes_that_exist) {
    // The bond fund lists on the last bar; before that the book is all equity, and
    // leaving half in cash would be a different decision than the rule makes.
    const auto d = portfolio::PortfolioData::align(
        {aSeries("A", {100, 100, 100, 100}), aSeries("B", {100}, 1600000000 + 3 * 86400)});

    portfolio::GroupParity s({"EQ", "BOND"});
    s.init(d);

    const auto early = s.targetWeights(d, 2);
    CHECK_NEAR(early[0], 1.0, 1e-9);
    CHECK_NEAR(early[1], 0.0, 1e-9);

    // Once it is there, it is half the book.
    const auto late = s.targetWeights(d, 4);
    CHECK_NEAR(late[0], 0.5, 1e-9);
    CHECK_NEAR(late[1], 0.5, 1e-9);
}

TEST(allocation, an_untagged_universe_is_refused_rather_than_guessed_at) {
    const auto d = portfolio::PortfolioData::align({aSeries("A", {100, 100, 100}), aSeries("B", {100, 100, 100})});

    // Fewer labels than assets: the rule cannot know what the last one is.
    portfolio::GroupParity s({"EQ"});
    s.init(d);
    const auto w = s.targetWeights(d, 2);

    // Allocating anyway would report a diversification the book does not have.
    CHECK_NEAR(sum(w), 0.0, 1e-9);
}

TEST(allocation, weighting_by_volatility_inside_a_class_leaves_the_class_split_alone) {
    // One class holds a quiet asset and a wild one; the other holds a single asset.
    // Whatever happens between the two inside the first class, the classes must
    // still be half and half — that split is the decision the rule exists to make.
    const auto d = portfolio::PortfolioData::align({wobbling("QUIET", 100.0, 1.0, 80),
                                                    wobbling("WILD", 100.0, 8.0, 80),
                                                    wobbling("BOND", 100.0, 1.0, 80)});

    portfolio::GroupParity s({"EQ", "EQ", "BOND"}, true, 20);
    s.init(d);
    const auto w = s.targetWeights(d, 60);

    CHECK_NEAR(w[0] + w[1], 0.5, 1e-9);
    CHECK_NEAR(w[2], 0.5, 1e-9);

    // And inside the class, the quiet one carries more of it.
    CHECK(w[0] > w[1]);
    // Roughly eight times the swing should mean roughly an eighth of the weight.
    CHECK(w[0] / w[1] > 5.0);
    CHECK(w[0] / w[1] < 12.0);
}

TEST(allocation, the_equal_weighted_variant_needs_no_history_and_the_vol_one_does) {
    portfolio::GroupParity plain({"EQ", "BOND"});
    portfolio::GroupParity vol({"EQ", "BOND"}, true, 63);

    CHECK_EQ(plain.warmupPeriod(), std::size_t{1});
    CHECK(vol.warmupPeriod() > std::size_t{63});
}

TEST(allocation, the_allocation_cannot_see_the_bar_it_is_made_for) {
    const std::vector<double> quiet(40, 100.0);
    std::vector<double>       wild(40, 100.0);

    const auto before = portfolio::PortfolioData::align({aSeries("A", quiet), aSeries("B", wild)});

    // Now make the very last bar violent. A rule reading it would change its mind.
    wild.back()      = 1000.0;
    const auto after = portfolio::PortfolioData::align({aSeries("A", quiet), aSeries("B", wild)});

    portfolio::GroupParity s1({"EQ", "BOND"}, true, 20);
    portfolio::GroupParity s2({"EQ", "BOND"}, true, 20);
    s1.init(before);
    s2.init(after);

    const auto w1 = s1.targetWeights(before, 39);
    const auto w2 = s2.targetWeights(after, 39);
    for (std::size_t i = 0; i < w1.size(); ++i) {
        CHECK_NEAR(w1[i], w2[i], 1e-12);
    }
}

TEST(allocation, a_configured_class_split_is_honoured_exactly) {
    const auto d = portfolio::PortfolioData::align({aSeries("E1", {100, 100, 100, 100}),
                                                    aSeries("E2", {100, 100, 100, 100}),
                                                    aSeries("B1", {100, 100, 100, 100})});

    // 20% equity is the split the frontier peaks at; the rule must actually put 20%
    // there rather than approximately 20%.
    portfolio::GroupParity s({"EQ", "EQ", "BOND"}, false, 63, {{"EQ", 20.0}, {"BOND", 80.0}});
    s.init(d);
    const auto w = s.targetWeights(d, 3);

    CHECK_NEAR(w[0], 0.10, 1e-9);
    CHECK_NEAR(w[1], 0.10, 1e-9);
    CHECK_NEAR(w[2], 0.80, 1e-9);
    CHECK_NEAR(sum(w), 1.0, 1e-9);
}

TEST(allocation, configured_shares_are_renormalised_over_the_classes_that_exist) {
    // The bond fund lists on the last bar. Before it does, an 80% bond target has
    // nowhere to go, and holding 80% cash is not the decision the rule made.
    const auto d = portfolio::PortfolioData::align(
        {aSeries("E", {100, 100, 100, 100}), aSeries("B", {100}, 1600000000 + 3 * 86400)});

    portfolio::GroupParity s({"EQ", "BOND"}, false, 63, {{"EQ", 20.0}, {"BOND", 80.0}});
    s.init(d);

    CHECK_NEAR(s.targetWeights(d, 2)[0], 1.0, 1e-9);

    const auto late = s.targetWeights(d, 4);
    CHECK_NEAR(late[0], 0.20, 1e-9);
    CHECK_NEAR(late[1], 0.80, 1e-9);
}

TEST(allocation, a_class_the_split_does_not_mention_is_left_out_rather_than_defaulted) {
    const auto d = portfolio::PortfolioData::align({aSeries("E", {100, 100, 100}),
                                                    aSeries("B", {100, 100, 100}),
                                                    aSeries("G", {100, 100, 100})});

    // Naming two classes and not the third means the third gets nothing; silently
    // giving it an equal share would make an omission look like a decision.
    portfolio::GroupParity s({"EQ", "BOND", "GOLD"}, false, 63, {{"EQ", 50.0}, {"BOND", 50.0}});
    s.init(d);
    const auto w = s.targetWeights(d, 2);

    CHECK_NEAR(w[0], 0.5, 1e-9);
    CHECK_NEAR(w[1], 0.5, 1e-9);
    CHECK_NEAR(w[2], 0.0, 1e-9);
}

TEST(allocation, an_empty_split_is_the_even_one_and_changes_nothing) {
    const auto d = portfolio::PortfolioData::align({aSeries("E1", {100, 100, 100}),
                                                    aSeries("E2", {100, 100, 100}),
                                                    aSeries("B", {100, 100, 100})});

    portfolio::GroupParity plain({"EQ", "EQ", "BOND"});
    portfolio::GroupParity spelled({"EQ", "EQ", "BOND"}, false, 63, {{"EQ", 1.0}, {"BOND", 1.0}});
    plain.init(d);
    spelled.init(d);

    const auto a = plain.targetWeights(d, 2);
    const auto b = spelled.targetWeights(d, 2);
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK_NEAR(a[i], b[i], 1e-12);
    }
}

/* ------------------------- AbsoluteMomentumFilter ------------------------- */

TEST(allocation, an_asset_below_its_own_trailing_return_is_dropped_to_cash) {
    // A rises over the window, B falls. Both were wanted at a quarter each.
    std::vector<double> up, down;
    for (std::size_t i = 0; i < 40; ++i) {
        up.push_back(100.0 + static_cast<double>(i));
        down.push_back(100.0 - static_cast<double>(i));
    }
    const auto d = portfolio::PortfolioData::align({aSeries("UP", up), aSeries("DOWN", down)});

    portfolio::AbsoluteMomentumFilter s(std::make_unique<Says>(std::vector<double>{0.25, 0.25}), 20, 0.0);
    s.init(d);
    const auto w = s.targetWeights(d, 39);

    CHECK_NEAR(w[0], 0.25, 1e-9);
    // Zeroed, and NOT handed to the one that passed: redistributing it would
    // reinvest the money the rule just decided not to risk.
    CHECK_NEAR(w[1], 0.0, 1e-9);
    CHECK_NEAR(sum(w), 0.25, 1e-9);
}

TEST(allocation, an_asset_without_enough_history_fails_the_test_rather_than_passing_by_default) {
    std::vector<double> up;
    for (std::size_t i = 0; i < 40; ++i) {
        up.push_back(100.0 + static_cast<double>(i));
    }
    const auto d = portfolio::PortfolioData::align({aSeries("UP", up), aSeries("NEW", {100.0, 101.0})});

    portfolio::AbsoluteMomentumFilter s(std::make_unique<Says>(std::vector<double>{0.5, 0.5}), 20, 0.0);
    s.init(d);
    const auto w = s.targetWeights(d, 39);

    CHECK_NEAR(w[0], 0.5, 1e-9);
    // Not knowing whether something has been falling is not a reason to hold it.
    CHECK_NEAR(w[1], 0.0, 1e-9);
}

TEST(allocation, the_filter_waits_for_whichever_of_it_and_the_rule_needs_longer) {
    portfolio::AbsoluteMomentumFilter slowFilter(std::make_unique<Says>(std::vector<double>{1.0}), 252, 0.0);
    portfolio::AbsoluteMomentumFilter fastFilter(std::make_unique<Says>(std::vector<double>{1.0}), 1, 0.0);

    CHECK_EQ(slowFilter.warmupPeriod(), std::size_t{253});
    CHECK_EQ(fastFilter.warmupPeriod(), std::size_t{3});  // the wrapped rule's own warmup wins
    CHECK(slowFilter.name().find("Says") != std::string::npos);
}

TEST(allocation, a_rising_asset_keeps_exactly_the_weight_the_rule_asked_for) {
    std::vector<double> up;
    for (std::size_t i = 0; i < 40; ++i) {
        up.push_back(100.0 + static_cast<double>(i));
    }
    const auto d = portfolio::PortfolioData::align({aSeries("UP", up)});

    portfolio::AbsoluteMomentumFilter s(std::make_unique<Says>(std::vector<double>{0.37}), 20, 0.0);
    s.init(d);
    CHECK_NEAR(s.targetWeights(d, 39)[0], 0.37, 1e-12);
}

/* --------------------------- MovingAverageFilter -------------------------- */

TEST(allocation, an_asset_below_its_own_moving_average_is_dropped_to_cash) {
    std::vector<double> up, down;
    for (std::size_t i = 0; i < 60; ++i) {
        up.push_back(100.0 + static_cast<double>(i));
        down.push_back(160.0 - static_cast<double>(i));
    }
    const auto d = portfolio::PortfolioData::align({aSeries("UP", up), aSeries("DOWN", down)});

    portfolio::MovingAverageFilter s(std::make_unique<Says>(std::vector<double>{0.5, 0.5}), 20);
    s.init(d);
    const auto w = s.targetWeights(d, 59);

    CHECK_NEAR(w[0], 0.5, 1e-9);
    CHECK_NEAR(w[1], 0.0, 1e-9);
    // Left in cash, not handed to the one that passed.
    CHECK_NEAR(sum(w), 0.5, 1e-9);
}

TEST(allocation, the_trend_test_reads_the_average_of_the_bars_before_the_one_it_trades) {
    std::vector<double> flat(60, 100.0);
    const auto          before = portfolio::PortfolioData::align({aSeries("A", flat)});

    // Make the very last bar soar. A rule reading it would suddenly be above.
    flat.back()      = 10000.0;
    const auto after = portfolio::PortfolioData::align({aSeries("A", flat)});

    portfolio::MovingAverageFilter s1(std::make_unique<Says>(std::vector<double>{1.0}), 20);
    portfolio::MovingAverageFilter s2(std::make_unique<Says>(std::vector<double>{1.0}), 20);
    s1.init(before);
    s2.init(after);
    CHECK_NEAR(s1.targetWeights(before, 59)[0], s2.targetWeights(after, 59)[0], 1e-12);
}

TEST(allocation, the_filter_can_be_restricted_so_the_defensive_sleeve_is_always_held) {
    std::vector<double> falling;
    for (std::size_t i = 0; i < 60; ++i) {
        falling.push_back(160.0 - static_cast<double>(i));
    }
    const auto d = portfolio::PortfolioData::align({aSeries("STOCK", falling), aSeries("BOND", falling)});

    // Both are falling, but only the equity sleeve is in scope: a bond fund is held
    // precisely so that it is there while equity falls.
    portfolio::MovingAverageFilter s(std::make_unique<Says>(std::vector<double>{0.5, 0.5}), 20,
                                     std::vector<std::string>{"EQ", "BOND"}, std::vector<std::string>{"EQ"});
    s.init(d);
    const auto w = s.targetWeights(d, 59);

    CHECK_NEAR(w[0], 0.0, 1e-9);
    CHECK_NEAR(w[1], 0.5, 1e-9);
}

TEST(allocation, without_enough_history_the_trend_is_unknown_and_the_asset_is_not_held) {
    const auto d = portfolio::PortfolioData::align({aSeries("A", {100, 101, 102, 103, 104})});

    portfolio::MovingAverageFilter s(std::make_unique<Says>(std::vector<double>{1.0}), 200);
    s.init(d);
    // Rising, but over four bars against a 200-bar average: not knowing is not a
    // reason to assume the trend is up.
    CHECK_NEAR(s.targetWeights(d, 4)[0], 0.0, 1e-9);
    CHECK(s.warmupPeriod() > std::size_t{200});
}

/* ------------------------- through the engine ---------------------------- */

TEST(allocation, group_parity_run_end_to_end_stays_fully_invested_and_balanced) {
    const auto d = portfolio::PortfolioData::align({wobbling("A1", 100.0, 2.0, 200),
                                                    wobbling("A2", 100.0, 2.0, 200),
                                                    wobbling("A3", 100.0, 2.0, 200),
                                                    wobbling("B1", 100.0, 2.0, 200)});

    portfolio::PortfolioConfigBt cfg;
    cfg.commissionRate     = 0.0;
    cfg.slippagePct        = 0.0;
    cfg.sellTaxRate        = 0.0;
    cfg.rebalanceEveryBars = 21;

    portfolio::GroupParity     s({"EQ", "EQ", "EQ", "BOND"});
    portfolio::PortfolioEngine engine(10000000.0);
    const auto                 r = engine.run(s, d, cfg);

    CHECK(r.orders > std::size_t{0});
    CHECK(!r.ruined);
    // Prices only oscillate around their starting level, so a rule that keeps the
    // book balanced ends roughly where it began. A rule that let one class drift
    // away would not.
    CHECK(r.finalCapital > 9000000.0);
    CHECK(r.finalCapital < 11000000.0);
}
