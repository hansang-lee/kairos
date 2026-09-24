#include <cstdio>
#include <fstream>

#include <nlohmann/json.hpp>

#include "broker/ibroker.hpp"
#include "notify/bot_commands.hpp"
#include "test_framework.hpp"
#include "trade/fill_reconciler.hpp"
#include "trade/position_store.hpp"
#include "trade/risk_guard.hpp"
#include "trade/schedule_state.hpp"
#include "trade/signal_executor.hpp"
#include "trade/trade_journal.hpp"

namespace {

std::string tmp(const std::string& name) {
    return "/tmp/kairos_test_" + name;
}

void removeFile(const std::string& path) {
    std::remove(path.c_str());
}

AccountBalance balance(double cash, int64_t qty, double avg, double cur) {
    AccountBalance b;
    b.success         = true;
    b.cashBalance     = cash;
    b.totalEvalAmount = cash + static_cast<double>(qty) * cur;
    if (qty > 0) {
        StockHolding h;
        h.ticker       = "005930";
        h.quantity     = qty;
        h.avgPrice     = avg;
        h.currentPrice = cur;
        b.holdings.push_back(h);
    }
    return b;
}

StrategyProfile profile() {
    StrategyProfile p;
    p.id          = 99;
    p.name        = "Test";
    p.ticker      = "005930";
    p.market      = "KRX";
    p.positionPct = 1.0;
    return p;
}

/** An executor whose state files are per-test, so tests cannot leak into each other. */
/**
 * A broker that records what it was asked and answers with whatever it was told
 * to. This is the seam the executor's live branch had been missing: until it
 * existed that branch had never run under a test at all.
 */
class FakeBroker: public IBroker {
   public:
    struct Placed {
        OrderSide   side;
        std::string ticker;
        int64_t     quantity;
        double      price;
    };
    std::vector<Placed> placed;
    OrderResult         next;  ///< what the next placeOrder returns
    std::string         modeName = "paper";

    FakeBroker() {
        next.success = true;
        next.orderNo = "FAKE-0001";
    }

    OrderResult placeOrder(OrderSide side, const std::string& ticker, int64_t quantity, double price) override {
        placed.push_back({side, ticker, quantity, price});
        return next;
    }
    AccountBalance getBalance() override { return {}; }
    FillHistory    getDailyFills(const std::string&, const std::string&, bool) override { return {}; }
    std::string    mode() const override { return modeName; }
};

std::shared_ptr<FakeBroker> fakeBroker(const trade::ExecutionContext& ctx) {
    return std::dynamic_pointer_cast<FakeBroker>(ctx.broker);
}

trade::ExecutionContext freshContext(const trade::RiskLimits& limits, const std::string& tag) {
    removeFile(tmp(tag + "_pos.json"));
    removeFile(tmp(tag + "_risk.json"));
    removeFile(tmp(tag + "_journal.jsonl"));
    return {std::make_shared<trade::RiskGuard>(limits, tmp(tag + "_risk.json")),
            std::make_shared<trade::PositionStore>(tmp(tag + "_pos.json")),
            std::make_shared<trade::TradeJournal>(tmp(tag + "_journal.jsonl")), std::make_shared<FakeBroker>()};
}

/** The last journal line, parsed. */
nlohmann::json lastJournalLine(const trade::TradeJournal& journal) {
    std::ifstream in(journal.path());
    std::string   line, last;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            last = line;
        }
    }
    return last.empty() ? nlohmann::json::object() : nlohmann::json::parse(last);
}

}  // namespace

/* ------------------------------- TradeJournal ------------------------------- */

TEST(journal, append_writes_one_line_per_entry) {
    const std::string path = tmp("journal_basic.jsonl");
    removeFile(path);

    const trade::TradeJournal j(path);
    trade::JournalEntry       e;
    e.mode     = "paper";
    e.ticker   = "005930";
    e.side     = "BUY";
    e.quantity = 10;
    e.price    = 71500;
    CHECK(j.append(e));
    CHECK(j.append(e));

    std::ifstream in(path);
    int           lines = 0;
    std::string   line;
    while (std::getline(in, line)) {
        if (!line.empty())
            ++lines;
    }
    CHECK_EQ(lines, 2);
}

TEST(journal, fill_keys_ignore_orders_and_survive_torn_lines) {
    const std::string path = tmp("journal_keys.jsonl");
    removeFile(path);

    const trade::TradeJournal j(path);
    trade::JournalEntry       order;
    order.event    = "order";
    order.orderNo  = "A1";
    order.quantity = 5;
    j.append(order);

    trade::JournalEntry fill;
    fill.event    = "fill";
    fill.orderNo  = "B2";
    fill.quantity = 7;
    j.append(fill);

    // A process killed mid-write leaves a partial line; it must not hide the rest.
    std::ofstream(path, std::ios::app) << "{\"event\":\"fi";

    const auto keys = j.recordedFillKeys();
    CHECK_EQ(keys.size(), std::size_t{1});
    CHECK(keys.count("B2:7") == 1);
    CHECK(keys.count("A1:5") == 0);
}

TEST(journal, missing_file_yields_no_keys_rather_than_failing) {
    const trade::TradeJournal j(tmp("journal_absent.jsonl"));
    removeFile(tmp("journal_absent.jsonl"));
    CHECK(j.recordedFillKeys().empty());
}

/* -------------------------------- RiskGuard -------------------------------- */

TEST(risk, buy_allowed_on_a_fresh_day_and_opening_equity_recorded) {
    const std::string path = tmp("risk_fresh.json");
    removeFile(path);
    trade::RiskGuard g({3.0, 20}, path);

    CHECK(g.check(OrderSide::Buy, balance(10000000, 0, 0, 0)).allowed);
    CHECK_NEAR(g.openingEquity(), 10000000.0, 1e-6);
}

TEST(risk, daily_loss_limit_blocks_buys_but_never_sells) {
    const std::string path = tmp("risk_loss.json");
    removeFile(path);
    trade::RiskGuard g({3.0, 0}, path);

    (void)g.check(OrderSide::Buy, balance(10000000, 0, 0, 0));          // baseline
    CHECK(g.check(OrderSide::Buy, balance(9710000, 0, 0, 0)).allowed);  // -2.9%, inside

    const auto blocked = g.check(OrderSide::Buy, balance(9650000, 0, 0, 0));  // -3.5%
    CHECK(!blocked.allowed);
    CHECK(!blocked.reason.empty());

    // The point of the limit is to stop new risk, never to trap an open position.
    CHECK(g.check(OrderSide::Sell, balance(9650000, 0, 0, 0)).allowed);
}

TEST(risk, order_cap_counts_across_restarts) {
    const std::string path = tmp("risk_cap.json");
    removeFile(path);
    {
        trade::RiskGuard g({0.0, 3}, path);
        (void)g.check(OrderSide::Buy, balance(10000000, 0, 0, 0));
        g.recordOrder();
        g.recordOrder();
        CHECK(g.check(OrderSide::Buy, balance(10000000, 0, 0, 0)).allowed);
        g.recordOrder();
    }
    {
        trade::RiskGuard reopened({0.0, 3}, path);  // a restarted process
        CHECK_EQ(reopened.ordersToday(), 3);
        CHECK(!reopened.check(OrderSide::Buy, balance(10000000, 0, 0, 0)).allowed);
        CHECK(reopened.check(OrderSide::Sell, balance(10000000, 0, 0, 0)).allowed);
    }
}

TEST(risk, stale_day_state_is_discarded) {
    const std::string path = tmp("risk_stale.json");
    std::ofstream(path, std::ios::trunc) << R"({"date":"2020-01-01","opening_equity":5000000.0,"orders":99})" << "\n";

    trade::RiskGuard g({3.0, 20}, path);
    CHECK_EQ(g.ordersToday(), 0);
    CHECK(g.check(OrderSide::Buy, balance(10000000, 0, 0, 0)).allowed);
    CHECK_NEAR(g.openingEquity(), 10000000.0, 1e-6);
}

TEST(risk, corrupt_state_does_not_disable_the_limits) {
    const std::string path = tmp("risk_corrupt.json");
    std::ofstream(path, std::ios::trunc) << "{not json" << "\n";

    trade::RiskGuard g({3.0, 20}, path);
    (void)g.check(OrderSide::Buy, balance(10000000, 0, 0, 0));
    CHECK(!g.check(OrderSide::Buy, balance(9000000, 0, 0, 0)).allowed);
}

TEST(risk, a_failed_balance_fetch_is_not_read_as_a_breach) {
    const std::string path = tmp("risk_nobalance.json");
    removeFile(path);
    trade::RiskGuard g({3.0, 20}, path);
    (void)g.check(OrderSide::Buy, balance(10000000, 0, 0, 0));

    AccountBalance failed;  // success == false
    CHECK(g.check(OrderSide::Buy, failed).allowed);
}

/* ------------------------------ PositionStore ------------------------------ */

TEST(positions, entry_tracks_the_peak_and_exit_records_the_time) {
    const std::string path = tmp("pos_peak.json");
    removeFile(path);
    trade::PositionStore store(path);

    store.sync("005930", 10, 100000);
    CHECK_NEAR(store.get("005930").peakPrice, 100000.0, 1e-6);

    store.sync("005930", 10, 120000);
    CHECK_NEAR(store.get("005930").peakPrice, 120000.0, 1e-6);

    store.sync("005930", 10, 110000);  // a pullback must not lower the peak
    CHECK_NEAR(store.get("005930").peakPrice, 120000.0, 1e-6);

    store.sync("005930", 0, 110000);  // position closed
    CHECK(store.get("005930").lastExitTs > 0);
    CHECK_NEAR(store.get("005930").peakPrice, 0.0, 1e-6);
    CHECK_EQ(store.get("005930").entryTs, int64_t{0});
}

TEST(positions, state_survives_a_restart) {
    const std::string path = tmp("pos_restart.json");
    removeFile(path);
    {
        trade::PositionStore store(path);
        store.sync("005930", 10, 100000);
        store.sync("005930", 10, 130000);
        store.recordEntryTranche("005930");
    }
    {
        trade::PositionStore reopened(path);
        CHECK_NEAR(reopened.get("005930").peakPrice, 130000.0, 1e-6);
        CHECK_EQ(reopened.get("005930").entryTranches, 1);
    }
}

TEST(positions, tickers_are_independent) {
    const std::string path = tmp("pos_multi.json");
    removeFile(path);
    trade::PositionStore store(path);

    store.sync("005930", 10, 100000);
    store.sync("000660", 5, 200000);
    store.sync("005930", 10, 150000);

    CHECK_NEAR(store.get("005930").peakPrice, 150000.0, 1e-6);
    CHECK_NEAR(store.get("000660").peakPrice, 200000.0, 1e-6);
}

TEST(positions, a_position_closed_outside_the_system_is_noticed) {
    const std::string path = tmp("pos_external.json");
    removeFile(path);
    trade::PositionStore store(path);

    store.sync("005930", 10, 100000);
    store.sync("005930", 10, 200000);  // peak climbs
    store.sync("005930", 0, 200000);   // sold by hand at the broker
    // A stale peak here would fire a trailing stop the instant we re-entered.
    store.sync("005930", 10, 120000);
    CHECK_NEAR(store.get("005930").peakPrice, 120000.0, 1e-6);
}

/* ---------------------------- Fill reconciler ---------------------------- */

namespace {

Fill aFill(const std::string& orderNo, const std::string& ticker, int64_t filled, double avg,
           OrderSide side = OrderSide::Buy) {
    Fill f;
    f.orderNo   = orderNo;
    f.ticker    = ticker;
    f.side      = side;
    f.orderQty  = filled;
    f.filledQty = filled;
    f.avgPrice  = avg;
    f.name      = "TEST";
    return f;
}

trade::JournalEntry anOrder(const std::string& orderNo, int id, const std::string& strategy) {
    trade::JournalEntry e;
    e.event      = "order";
    e.mode       = "paper";
    e.orderNo    = orderNo;
    e.strategyId = id;
    e.strategy   = strategy;
    e.category   = "daily";
    e.reason     = "signal BUY";
    e.ticker     = "133690";
    e.side       = "BUY";
    e.quantity   = 17;
    e.price      = 184985.0;
    e.success    = true;
    return e;
}

}  // namespace

TEST(fills, a_fill_is_recorded_once_and_carries_the_strategy_that_ordered_it) {
    const std::string path = tmp("fills_basic.jsonl");
    removeFile(path);
    const trade::TradeJournal journal(path);
    journal.append(anOrder("0001", 50, "Aroon Trend 25/70"));

    const auto r = trade::reconcileFills(journal, {aFill("0001", "133690", 17, 185100.0)}, "paper");
    CHECK_EQ(r.recorded, std::size_t{1});

    // Running it again must write nothing: KIS reports the same fill every time it
    // is asked about that day.
    const auto again = trade::reconcileFills(journal, {aFill("0001", "133690", 17, 185100.0)}, "paper");
    CHECK_EQ(again.recorded, std::size_t{0});
    CHECK_EQ(again.alreadyKnown, std::size_t{1});

    // The strategy travels across from the order, which is the only place it exists.
    const auto keys = journal.recordedFillKeys();
    CHECK(keys.count("0001:17") > 0);
}

TEST(fills, an_order_that_fills_further_is_recorded_again_at_the_larger_quantity) {
    const std::string path = tmp("fills_partial.jsonl");
    removeFile(path);
    const trade::TradeJournal journal(path);
    journal.append(anOrder("0002", 51, "Aroon Trend 25/70"));

    CHECK_EQ(trade::reconcileFills(journal, {aFill("0002", "069500", 10, 113000.0)}, "paper").recorded, std::size_t{1});
    // The rest fills later in the session.
    CHECK_EQ(trade::reconcileFills(journal, {aFill("0002", "069500", 29, 113145.0)}, "paper").recorded, std::size_t{1});

    const auto keys = journal.recordedFillKeys();
    CHECK(keys.count("0002:10") > 0);
    CHECK(keys.count("0002:29") > 0);
}

TEST(fills, an_accepted_but_unfilled_order_is_not_a_fill) {
    const std::string path = tmp("fills_unfilled.jsonl");
    removeFile(path);
    const trade::TradeJournal journal(path);

    Fill pending        = aFill("0003", "133690", 0, 0.0);
    pending.orderQty    = 17;
    Fill cancelled      = aFill("0004", "133690", 17, 185000.0);
    cancelled.cancelled = true;

    const auto r = trade::reconcileFills(journal, {pending, cancelled}, "paper");
    CHECK_EQ(r.recorded, std::size_t{0});
    CHECK_EQ(r.ignored, std::size_t{2});
    // Recording either would put a price in the journal that nobody paid.
    CHECK(journal.recordedFillKeys().empty());
}

TEST(fills, a_trade_made_by_hand_is_still_recorded_with_no_strategy_attached) {
    const std::string path = tmp("fills_manual.jsonl");
    removeFile(path);
    const trade::TradeJournal journal(path);

    const auto r = trade::reconcileFills(journal, {aFill("9999", "005930", 3, 71000.0, OrderSide::Sell)}, "paper");
    CHECK_EQ(r.recorded, std::size_t{1});
    // A gap in the log is harder to explain later than a row with no strategy on it.
    CHECK(journal.recordedFillKeys().count("9999:3") > 0);
}

TEST(fills, a_dry_run_order_leaves_nothing_to_reconcile_against) {
    const std::string path = tmp("fills_dryrun.jsonl");
    removeFile(path);
    const trade::TradeJournal journal(path);

    auto dry    = anOrder("", 50, "Aroon Trend 25/70");
    dry.dryRun  = true;
    dry.success = false;
    journal.append(dry);

    // No order number, so it must not become a key that swallows a real fill later.
    CHECK(journal.orderContexts().empty());
}

/* ----------------------------- SignalExecutor ----------------------------- */

TEST(executor, stop_loss_fires_and_sells_everything) {
    auto p         = profile();
    p.stopLossPct  = 3.0;
    p.exitTranches = 3;  // a forced exit must ignore staging
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_stop"));

    const auto d = ex.execute(Signal::HOLD, 97000, balance(0, 90, 100000, 97000));
    CHECK(d.acted);
    CHECK_EQ(d.side, std::string("SELL"));
    CHECK_EQ(d.quantity, int64_t{90});
    CHECK(d.reason.find("STOP-LOSS") != std::string::npos);
}

TEST(executor, take_profit_fires_above_target_and_not_below) {
    auto p          = profile();
    p.takeProfitPct = 5.0;
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_tp"));

    CHECK(!ex.execute(Signal::HOLD, 104000, balance(0, 10, 100000, 104000)).acted);
    const auto d = ex.execute(Signal::HOLD, 105000, balance(0, 10, 100000, 105000));
    CHECK(d.acted);
    CHECK(d.reason.find("TAKE-PROFIT") != std::string::npos);
}

TEST(executor, trailing_stop_uses_the_peak_not_the_entry) {
    auto p            = profile();
    p.trailingStopPct = 2.0;
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_trail"));

    ex.execute(Signal::HOLD, 100000, balance(0, 10, 100000, 100000));
    const auto up = ex.execute(Signal::HOLD, 110000, balance(0, 10, 100000, 110000));
    CHECK(!up.acted);
    CHECK_NEAR(up.peakPrice, 110000.0, 1e-6);

    CHECK(!ex.execute(Signal::HOLD, 108500, balance(0, 10, 100000, 108500)).acted);     // -1.4%
    const auto out = ex.execute(Signal::HOLD, 107800, balance(0, 10, 100000, 107800));  // -2.0%
    CHECK(out.acted);
    CHECK(out.reason.find("TRAILING-STOP") != std::string::npos);
}

TEST(executor, strategy_sell_is_staged_across_exit_tranches) {
    auto p         = profile();
    p.exitTranches = 3;
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_exit_tranche"));

    const auto d = ex.execute(Signal::SELL, 100000, balance(0, 90, 100000, 100000));
    CHECK_EQ(d.quantity, int64_t{30});
}

TEST(executor, entry_tranches_split_the_position) {
    auto p          = profile();
    p.entryTranches = 2;
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_entry_tranche"));

    const auto d = ex.execute(Signal::BUY, 1000, balance(100000, 0, 0, 1000));
    CHECK_EQ(d.quantity, int64_t{50});
}

TEST(executor, later_entry_tranches_do_not_wait_for_another_buy_signal) {
    // A crossover strategy says BUY once and HOLD forever after. If tranche two
    // needed a second BUY, a three-tranche plan would sit at a third of its size
    // until the next crossover, which may be months away.
    auto p          = profile();
    p.entryTranches = 3;
    auto ctx        = freshContext({}, "exec_tranche_continue");

    trade::SignalExecutor ex(p, false, -1, ctx);
    const auto            first = ex.execute(Signal::BUY, 1000, balance(300000, 0, 0, 1000));
    CHECK_EQ(first.side, std::string{"BUY"});
    CHECK(first.quantity > int64_t{0});
    ctx.positions->recordEntryTranche("005930");

    // Next session: still no new signal, but the plan is not filled.
    const auto second = ex.execute(Signal::HOLD, 1000, balance(200000, 100, 100000, 1000));
    CHECK_EQ(second.side, std::string{"BUY"});
    CHECK(second.quantity > int64_t{0});
}

TEST(executor, a_holding_this_strategy_did_not_open_draws_no_tranche_on_hold) {
    // Bought by hand at the broker: no entry tranche was ever recorded, so a HOLD
    // must not be read as permission to keep adding to it.
    auto p          = profile();
    p.entryTranches = 3;
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_tranche_manual"));

    const auto d = ex.execute(Signal::HOLD, 1000, balance(200000, 100, 100000, 1000));
    CHECK(!d.acted);
    CHECK(d.side.empty());
}

TEST(executor, a_sell_signal_stops_the_entry_plan_rather_than_continuing_it) {
    auto p          = profile();
    p.entryTranches = 3;
    auto ctx        = freshContext({}, "exec_tranche_sell");

    trade::SignalExecutor ex(p, false, -1, ctx);
    ex.execute(Signal::BUY, 1000, balance(300000, 0, 0, 1000));
    ctx.positions->recordEntryTranche("005930");

    const auto d = ex.execute(Signal::SELL, 1000, balance(200000, 100, 100000, 1000));
    CHECK_EQ(d.side, std::string{"SELL"});
}

TEST(executor, cooldown_blocks_reentry_but_not_exits) {
    auto p            = profile();
    p.cooldownMinutes = 30;
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_cooldown"));

    ex.execute(Signal::HOLD, 1000, balance(0, 10, 1000, 1000));   // holding
    ex.execute(Signal::HOLD, 1000, balance(100000, 0, 0, 1000));  // went flat

    const auto blocked = ex.execute(Signal::BUY, 1000, balance(100000, 0, 0, 1000));
    CHECK(blocked.skipped);
    CHECK(blocked.blockedBy.find("cooldown") != std::string::npos);
}

TEST(executor, trade_window_blocks_entry_but_never_exit) {
    auto p       = profile();
    p.tradeStart = "2350";
    p.tradeEnd   = "2359";
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_window"));

    const auto buy = ex.execute(Signal::BUY, 1000, balance(100000, 0, 0, 1000));
    CHECK(buy.skipped);
    CHECK(buy.blockedBy.find("window") != std::string::npos);

    const auto sell = ex.execute(Signal::SELL, 1000, balance(0, 10, 1000, 1000));
    CHECK(sell.acted);
    CHECK(!sell.skipped);
}

TEST(executor, a_failed_balance_fetch_skips_the_round) {
    auto                  p = profile();
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_nobalance"));

    AccountBalance failed;
    // Reading a failed fetch as "flat" would re-buy a position already held.
    const auto d = ex.execute(Signal::BUY, 1000, failed);
    CHECK(!d.acted);
}

/* ------------------------- the live branch, at last ------------------------- */

TEST(executor, a_live_buy_sends_one_order_and_records_everything_it_should) {
    auto                  ctx  = freshContext({}, "exec_live_buy");
    auto                  fake = fakeBroker(ctx);
    auto                  p    = profile();
    trade::SignalExecutor ex(p, true, -1, ctx);

    const auto d = ex.execute(Signal::BUY, 1000, balance(100000, 0, 0, 1000));

    CHECK(d.sent);
    CHECK(d.order.success);
    CHECK_EQ(fake->placed.size(), std::size_t{1});
    CHECK_EQ(fake->placed[0].ticker, std::string{"005930"});
    CHECK_EQ(fake->placed[0].quantity, d.quantity);
    CHECK(fake->placed[0].side == OrderSide::Buy);
    CHECK_NEAR(fake->placed[0].price, 0.0, 1e-9);  // market order

    // Every downstream record the order should leave behind.
    CHECK_EQ(ctx.positions->get("005930").entryTranches, 1);
    CHECK_EQ(ex.ordersSent(), 1);
    const auto j = lastJournalLine(*ctx.journal);
    CHECK_EQ(j.value("event", ""), std::string{"order"});
    CHECK_EQ(j.value("order_no", ""), std::string{"FAKE-0001"});
    CHECK(j.value("success", false));
    CHECK(!j.value("dry_run", true));
    CHECK_EQ(j.value("mode", ""), std::string{"paper"});
}

TEST(executor, a_live_sell_sends_the_whole_holding_and_records_an_exit) {
    auto                  ctx  = freshContext({}, "exec_live_sell");
    auto                  fake = fakeBroker(ctx);
    auto                  p    = profile();
    trade::SignalExecutor ex(p, true, -1, ctx);

    const auto d = ex.execute(Signal::SELL, 1000, balance(0, 90, 900, 1000));

    CHECK(d.sent);
    CHECK_EQ(fake->placed.size(), std::size_t{1});
    CHECK(fake->placed[0].side == OrderSide::Sell);
    CHECK_EQ(fake->placed[0].quantity, int64_t{90});
    CHECK_EQ(ctx.positions->get("005930").exitTranches, 1);
}

TEST(executor, a_rejected_order_is_journaled_as_failed_and_opens_no_position) {
    auto ctx           = freshContext({}, "exec_live_reject");
    auto fake          = fakeBroker(ctx);
    fake->next.success = false;
    fake->next.orderNo.clear();
    fake->next.message      = "주문가능금액을 초과했습니다";
    auto                  p = profile();
    trade::SignalExecutor ex(p, true, -1, ctx);

    const auto d = ex.execute(Signal::BUY, 1000, balance(100000, 0, 0, 1000));

    CHECK(d.sent);
    CHECK(!d.order.success);
    // The broker said no, so nothing was opened — a tranche recorded here would
    // make the next HOLD buy the remainder of a position that does not exist.
    CHECK_EQ(ctx.positions->get("005930").entryTranches, 0);
    // And nothing moved, so neither cap is spent: twenty rejections in an outage
    // must not lock the account out of the rest of the day.
    CHECK_EQ(ctx.risk->ordersToday(), 0);
    CHECK_EQ(ex.ordersSent(), 0);
    const auto j = lastJournalLine(*ctx.journal);
    CHECK_EQ(j.value("event", ""), std::string{"order"});
    CHECK(!j.value("success", true));
    CHECK(j.value("message", "").find("초과") != std::string::npos);
}

TEST(executor, a_lost_response_is_journaled_as_unknown_not_as_a_rejection) {
    auto ctx                 = freshContext({}, "exec_live_unknown");
    auto fake                = fakeBroker(ctx);
    fake->next.success       = false;
    fake->next.indeterminate = true;
    fake->next.orderNo.clear();
    auto                  p = profile();
    trade::SignalExecutor ex(p, true, -1, ctx);

    ex.execute(Signal::BUY, 1000, balance(100000, 0, 0, 1000));

    // The order may exist at the broker. The journal must say so in a way a later
    // reconciliation can find, and must not open a position it cannot confirm.
    CHECK_EQ(lastJournalLine(*ctx.journal).value("event", ""), std::string{"order_unknown"});
    CHECK_EQ(ctx.positions->get("005930").entryTranches, 0);
    // But it is counted against the caps: an order that may have filled is treated
    // as one that did until the reconciliation says otherwise.
    CHECK_EQ(ctx.risk->ordersToday(), 1);
    CHECK_EQ(ex.ordersSent(), 1);
}

TEST(executor, the_session_cap_stops_the_call_before_it_reaches_the_broker) {
    auto ctx  = freshContext({}, "exec_live_cap");
    auto fake = fakeBroker(ctx);
    auto p    = profile();
    // Three tranches, so the second BUY genuinely wants to send another order —
    // with one tranche the position is simply complete and the cap is never asked.
    p.entryTranches = 3;
    trade::SignalExecutor ex(p, true, 1, ctx);

    ex.execute(Signal::BUY, 1000, balance(100000, 0, 0, 1000));
    const auto second = ex.execute(Signal::BUY, 1000, balance(100000, 0, 0, 1000));

    CHECK_EQ(fake->placed.size(), std::size_t{1});
    CHECK(second.skipped);
    CHECK(!second.sent);
}

TEST(executor, a_dry_run_never_touches_the_broker) {
    auto                  ctx  = freshContext({}, "exec_dry_broker");
    auto                  fake = fakeBroker(ctx);
    auto                  p    = profile();
    trade::SignalExecutor ex(p, false, -1, ctx);

    const auto d = ex.execute(Signal::BUY, 1000, balance(100000, 0, 0, 1000));

    CHECK(d.acted);
    CHECK(!d.sent);
    CHECK(fake->placed.empty());
}

TEST(executor, a_live_executor_with_no_broker_refuses_rather_than_pretending) {
    auto ctx = freshContext({}, "exec_live_nobroker");
    ctx.broker.reset();
    auto                  p = profile();
    trade::SignalExecutor ex(p, true, -1, ctx);

    const auto d = ex.execute(Signal::BUY, 1000, balance(100000, 0, 0, 1000));

    CHECK(!d.sent);
    CHECK(d.skipped);
    CHECK_EQ(d.blockedBy, std::string{"no broker configured"});
}

TEST(executor, the_journal_mode_comes_from_the_broker) {
    auto ctx                = freshContext({}, "exec_mode");
    auto fake               = fakeBroker(ctx);
    fake->modeName          = "live";
    auto                  p = profile();
    trade::SignalExecutor ex(p, true, -1, ctx);

    ex.execute(Signal::BUY, 1000, balance(100000, 0, 0, 1000));
    CHECK_EQ(lastJournalLine(*ctx.journal).value("mode", ""), std::string{"live"});
}

TEST(executor, dry_run_decides_but_never_sends) {
    auto                  p = profile();
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_dryrun"));

    const auto d = ex.execute(Signal::BUY, 1000, balance(100000, 0, 0, 1000));
    CHECK(d.acted);
    CHECK(!d.sent);
    CHECK_EQ(ex.ordersSent(), 0);
}

TEST(executor, risk_limits_reach_the_decision) {
    auto                  p   = profile();
    auto                  ctx = freshContext({3.0, 0}, "exec_risk");
    trade::SignalExecutor ex(p, false, -1, ctx);

    // A HOLD round still records the day's opening equity, which is the point:
    // the limit must measure from the start of the day, not from the first signal.
    ex.execute(Signal::HOLD, 1000, balance(10000000, 0, 0, 1000));
    const auto blocked = ex.execute(Signal::BUY, 1000, balance(9000000, 0, 0, 1000));
    CHECK(blocked.skipped);
    CHECK(!blocked.blockedBy.empty());
}

TEST(executor, no_signal_and_no_position_does_nothing) {
    auto                  p = profile();
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_idle"));
    const auto            d = ex.execute(Signal::HOLD, 1000, balance(100000, 0, 0, 1000));
    CHECK(!d.acted);
    CHECK(!d.skipped);
}

TEST(executor, sell_signal_with_no_position_does_nothing) {
    auto                  p = profile();
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_sell_flat"));
    const auto            d = ex.execute(Signal::SELL, 1000, balance(100000, 0, 0, 1000));
    CHECK(!d.acted);
}

TEST(executor, buy_signal_while_fully_entered_does_nothing) {
    auto                  p = profile();  // entryTranches defaults to 1
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_buy_held"));

    ex.execute(Signal::BUY, 1000, balance(100000, 0, 0, 1000));
    // Not live, so no tranche was recorded; the position itself must not trigger a second buy
    // once the plan is filled. Simulate the filled plan by holding.
    const auto d = ex.execute(Signal::HOLD, 1000, balance(0, 100, 1000, 1000));
    CHECK(!d.acted);
}

TEST(risk, a_once_daily_process_is_still_protected_from_an_overnight_fall) {
    // daily_trade runs once, at 15:15. If the only baseline is the first balance
    // that run sees, the limit compares that balance against itself and can never
    // fire — the account-level safety net would be inert for the very process
    // meant to run unattended. The previous session's equity has to carry over.
    const std::string path = tmp("risk_overnight.json");
    removeFile(path);

    {
        trade::RiskGuard yesterday({3.0, 0}, path);
        yesterday.observe(balance(10000000, 0, 0, 0));
    }

    // Simulate the next day by rewriting the stored date, leaving the equity.
    {
        std::ifstream  in(path);
        nlohmann::json j;
        in >> j;
        j["date"] = "2000-01-01";  // any prior date
        std::ofstream(path, std::ios::trunc) << j.dump(2) << "\n";
    }

    trade::RiskGuard today({3.0, 0}, path);
    // The account opens 5% below where it was last seen.
    const auto verdict = today.check(OrderSide::Buy, balance(9500000, 0, 0, 0));
    CHECK_MSG(!verdict.allowed, "a 5% fall since the previous session should block new buying, got allowed");
    CHECK(today.check(OrderSide::Sell, balance(9500000, 0, 0, 0)).allowed);
}

TEST(executor, a_buy_is_skipped_when_the_allocation_cannot_afford_one_share) {
    // max(1, alloc / price) forced a quantity of 1 even when the allocation could
    // not cover a single share, so the order either committed far more than
    // position_pct or was rejected by the broker for insufficient funds.
    auto p        = profile();
    p.positionPct = 0.2;
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_unaffordable"));

    // 20% of 300,000 is 60,000 — less than one 260,000 share.
    const auto d = ex.execute(Signal::BUY, 260000, balance(300000, 0, 0, 260000));
    CHECK_MSG(!d.sent, "an unaffordable buy must not be sent");
    CHECK_MSG(d.quantity == 0 || !d.acted, "expected no order, got quantity " + std::to_string(d.quantity));
}

TEST(executor, sizing_is_a_share_of_equity_not_of_leftover_cash) {
    // Three profiles at 0.33 must each aim for a third of the account. Sizing from
    // remaining cash gave 33%, 22% and 15% depending on the order they ran in, which
    // is not what a backtest of three equal sleeves measured.
    auto p        = profile();
    p.positionPct = 0.33;
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_equity_sizing"));

    // Half the account is already tied up elsewhere, so cash is 5,000,000 of a
    // 10,000,000 account. A third of the account is 3,300,000 → 33 shares at 100,000.
    AccountBalance b;
    b.success         = true;
    b.cashBalance     = 5000000;
    b.totalEvalAmount = 10000000;

    const auto d = ex.execute(Signal::BUY, 100000, b);
    CHECK(d.acted);
    CHECK_EQ(d.quantity, int64_t{33});
}

TEST(executor, sizing_never_exceeds_the_cash_actually_available) {
    auto p        = profile();
    p.positionPct = 0.5;
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_cash_bound"));

    // Half of a 10,000,000 account is 5,000,000, but only 1,000,000 is in cash.
    AccountBalance b;
    b.success         = true;
    b.cashBalance     = 1000000;
    b.totalEvalAmount = 10000000;

    const auto d = ex.execute(Signal::BUY, 100000, b);
    CHECK_MSG(d.quantity <= 10, "ordered " + std::to_string(d.quantity) + " shares on 1,000,000 of cash");
}

TEST(executor, an_existing_holding_counts_toward_the_target) {
    auto p        = profile();
    p.positionPct = 0.5;
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_topup"));

    // Target is 5,000,000; 40 shares at 100,000 is already 4,000,000 of it, so only
    // 1,000,000 of room remains — 10 shares, not 50.
    const auto d = ex.execute(Signal::BUY, 100000, balance(6000000, 40, 100000, 100000));
    if (d.acted) {
        CHECK_MSG(d.quantity <= 10, "topped up by " + std::to_string(d.quantity) + " ignoring what is held");
    }
}

TEST(executor, position_pct_is_respected_rather_than_rounded_up) {
    auto p        = profile();
    p.positionPct = 0.2;
    trade::SignalExecutor ex(p, false, -1, freshContext({}, "exec_sizing"));

    // 20% of 10,000,000 is 2,000,000 → 7 shares at 260,000, not 8.
    const auto d = ex.execute(Signal::BUY, 260000, balance(10000000, 0, 0, 260000));
    CHECK(d.acted);
    CHECK_EQ(d.quantity, int64_t{7});
    CHECK_MSG(static_cast<double>(d.quantity) * 260000 <= 10000000 * 0.2, "sizing exceeded position_pct");
}

/* ----------------------------- ScheduleState ----------------------------- */

TEST(schedule, a_daily_profile_runs_once_and_not_again_that_day) {
    CHECK(trade::isDailyProfileDue("", "2026-09-21", 1515, 1515));
    CHECK(!trade::isDailyProfileDue("2026-09-21", "2026-09-21", 1520, 1515));
    // A new day makes it due again.
    CHECK(trade::isDailyProfileDue("2026-09-21", "2026-09-22", 1515, 1515));
}

TEST(schedule, a_daily_profile_is_not_due_before_its_time) {
    CHECK(!trade::isDailyProfileDue("", "2026-09-21", 1000, 1515));
    CHECK(!trade::isDailyProfileDue("", "2026-09-21", 1514, 1515));
    CHECK(trade::isDailyProfileDue("", "2026-09-21", 1515, 1515));
}

TEST(schedule, being_late_still_counts_as_due) {
    // The reason this is a daemon and not a wall-clock timer: a process that was
    // busy or restarting at 15:15 must still act, not skip the day silently.
    CHECK(trade::isDailyProfileDue("", "2026-09-21", 1529, 1515));
}

TEST(schedule, evaluation_dates_survive_a_restart) {
    const std::string path = tmp("schedule.json");
    removeFile(path);
    {
        trade::ScheduleState s(path);
        CHECK_EQ(s.lastEvaluated(30), std::string(""));
        s.markEvaluated(30, "2026-09-21");
        s.markEvaluated(31, "2026-09-21");
    }
    {
        trade::ScheduleState reopened(path);
        // Without this a restart would re-evaluate and could re-enter a position
        // the process had just exited.
        CHECK_EQ(reopened.lastEvaluated(30), std::string("2026-09-21"));
        CHECK_EQ(reopened.lastEvaluated(31), std::string("2026-09-21"));
        CHECK_EQ(reopened.lastEvaluated(99), std::string(""));
    }
}

TEST(schedule, a_corrupt_state_file_does_not_stop_the_process) {
    const std::string path = tmp("schedule_corrupt.json");
    std::ofstream(path, std::ios::trunc) << "{not json";

    trade::ScheduleState s(path);
    CHECK_EQ(s.lastEvaluated(30), std::string(""));  // costs one duplicate evaluation, no more
    s.markEvaluated(30, "2026-09-21");
    CHECK_EQ(s.lastEvaluated(30), std::string("2026-09-21"));
}

TEST(schedule, profiles_are_tracked_independently) {
    const std::string path = tmp("schedule_multi.json");
    removeFile(path);
    trade::ScheduleState s(path);

    s.markEvaluated(30, "2026-09-21");
    CHECK(!trade::isDailyProfileDue(s.lastEvaluated(30), "2026-09-21", 1600, 1515));
    // #31 has not run; it must still be due.
    CHECK(trade::isDailyProfileDue(s.lastEvaluated(31), "2026-09-21", 1600, 1515));
}

/* ------------------------------- Bot commands ------------------------------ */

TEST(bot, only_the_configured_chat_is_answered) {
    CHECK(notify::isAuthorised("123456789", "123456789"));
    CHECK(!notify::isAuthorised("987654321", "123456789"));
    // An unconfigured bot answering everyone would be worse than answering nobody.
    CHECK(!notify::isAuthorised("123456789", ""));
    CHECK(!notify::isAuthorised("", ""));
}

TEST(bot, a_command_is_read_without_its_botname_suffix_or_arguments) {
    CHECK_EQ(notify::commandOf("/status"), std::string{"/status"});
    // Telegram appends the bot's name in a group chat.
    CHECK_EQ(notify::commandOf("/status@kairos_bot"), std::string{"/status"});
    CHECK_EQ(notify::commandOf("/Trades 20"), std::string{"/trades"});
    CHECK_EQ(notify::commandOf(""), std::string{""});
    CHECK_EQ(notify::commandOf("hello there"), std::string{"hello"});
}

TEST(bot, status_reports_the_split_between_stock_and_cash) {
    notify::BotSnapshot s;
    s.ok             = true;
    s.initialCapital = 10000000.0;
    s.cashBalance    = 4000000.0;
    s.totalEval      = 11000000.0;
    StockHolding h;
    h.ticker     = "133690";
    h.name       = "TIGER 미국나스닥100";
    h.quantity   = 38;
    h.avgPrice   = 180000.0;
    h.evalAmount = 7000000.0;
    s.holdings.push_back(h);

    const auto text = notify::formatStatus(s);
    CHECK(text.find("11,000,000") != std::string::npos);  // thousands separators, read on a phone
    CHECK(text.find("+10.00%") != std::string::npos);     // return against principal
    CHECK(text.find("63.6%") != std::string::npos);       // 7M of 11M in stock
    CHECK(text.find("36.4%") != std::string::npos);       // and the rest in cash
}

TEST(bot, a_failed_balance_fetch_says_so_rather_than_reporting_zero) {
    notify::BotSnapshot s;
    s.ok      = false;
    s.message = "token expired";
    // Printing a zero balance would read as a wiped-out account.
    CHECK(notify::formatStatus(s).find("token expired") != std::string::npos);
    CHECK(notify::formatPositions(s).find("token expired") != std::string::npos);
}

TEST(bot, an_empty_account_and_an_empty_journal_say_which_is_which) {
    notify::BotSnapshot s;
    s.ok = true;
    CHECK_EQ(notify::formatPositions(s), std::string{"보유 종목 없음"});
    CHECK_EQ(notify::formatTrades({}, 10), std::string{"거래 기록 없음"});
    CHECK_EQ(notify::formatSignals({}), std::string{"활성 전략 없음"});
}

TEST(bot, trades_are_listed_newest_first_and_capped) {
    std::vector<notify::BotTrade> t;
    for (int i = 0; i < 5; ++i) {
        notify::BotTrade x;
        x.time     = "1" + std::to_string(i) + ":00";
        x.event    = "order";
        x.ticker   = "00000" + std::to_string(i);
        x.side     = "BUY";
        x.quantity = 1;
        t.push_back(x);
    }
    const auto text = notify::formatTrades(t, 2);
    // The last thing that happened is what someone checking their phone wants first.
    CHECK(text.find("000004") < text.find("000003"));
    CHECK(text.find("000002") == std::string::npos);
}
