#include <cstdio>
#include <fstream>

#include <nlohmann/json.hpp>

#include "test_framework.hpp"
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
trade::ExecutionContext freshContext(const trade::RiskLimits& limits, const std::string& tag) {
    removeFile(tmp(tag + "_pos.json"));
    removeFile(tmp(tag + "_risk.json"));
    removeFile(tmp(tag + "_journal.jsonl"));
    return {std::make_shared<trade::RiskGuard>(limits, tmp(tag + "_risk.json")),
            std::make_shared<trade::PositionStore>(tmp(tag + "_pos.json")),
            std::make_shared<trade::TradeJournal>(tmp(tag + "_journal.jsonl"))};
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

    g.check(OrderSide::Buy, balance(10000000, 0, 0, 0));                // baseline
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
        g.check(OrderSide::Buy, balance(10000000, 0, 0, 0));
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
    g.check(OrderSide::Buy, balance(10000000, 0, 0, 0));
    CHECK(!g.check(OrderSide::Buy, balance(9000000, 0, 0, 0)).allowed);
}

TEST(risk, a_failed_balance_fetch_is_not_read_as_a_breach) {
    const std::string path = tmp("risk_nobalance.json");
    removeFile(path);
    trade::RiskGuard g({3.0, 20}, path);
    g.check(OrderSide::Buy, balance(10000000, 0, 0, 0));

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
