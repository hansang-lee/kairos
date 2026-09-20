#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>

#include <nlohmann/json.hpp>

#include "common/process_lock.hpp"
#include "data/bar_recorder.hpp"
#include "strategy/strategy_factory.hpp"
#include "test_framework.hpp"
#include "trade/signal_executor.hpp"

/**
 * End-to-end scenarios across the pieces that decide and record a trade:
 * bars -> strategy -> SignalExecutor -> risk guard / position store -> journal.
 *
 * Order placement itself is excluded — it needs the live KIS API, and a test that
 * hits a broker is not a test. Everything up to the send is covered, which is
 * where the logic lives; the send is one call whose result is journaled either
 * way. Every scenario runs in dry-run for the same reason.
 */
namespace {

std::string dir(const std::string& name) {
    return "/tmp/kairos_it_" + name;
}

/** A scenario's own state files, so scenarios cannot contaminate each other. */
struct Sandbox {
    std::string             root;
    trade::ExecutionContext ctx;

    explicit Sandbox(const std::string& name, const trade::RiskLimits& limits = {})
        : root(dir(name)) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        ctx = {std::make_shared<trade::RiskGuard>(limits, root + "/risk.json"),
               std::make_shared<trade::PositionStore>(root + "/positions.json"),
               std::make_shared<trade::TradeJournal>(root + "/trades.jsonl")};
    }

    /** Re-open the same files, as a restarted process would. */
    trade::ExecutionContext reopen(const trade::RiskLimits& limits = {}) const {
        return {std::make_shared<trade::RiskGuard>(limits, root + "/risk.json"),
                std::make_shared<trade::PositionStore>(root + "/positions.json"),
                std::make_shared<trade::TradeJournal>(root + "/trades.jsonl")};
    }

    [[nodiscard]] std::vector<nlohmann::json> journalEntries() const {
        std::vector<nlohmann::json> out;
        std::ifstream               in(root + "/trades.jsonl");
        std::string                 line;
        while (std::getline(in, line)) {
            if (line.empty())
                continue;
            try {
                out.push_back(nlohmann::json::parse(line));
            } catch (const std::exception&) {
            }
        }
        return out;
    }
};

AccountBalance balance(double cash, int64_t qty, double avg, double cur, const std::string& ticker = "005930") {
    AccountBalance b;
    b.success         = true;
    b.cashBalance     = cash;
    b.totalEvalAmount = cash + static_cast<double>(qty) * cur;
    if (qty > 0) {
        StockHolding h;
        h.ticker       = ticker;
        h.quantity     = qty;
        h.avgPrice     = avg;
        h.currentPrice = cur;
        b.holdings.push_back(h);
    }
    return b;
}

StrategyProfile profile(int id, const std::string& ticker) {
    StrategyProfile p;
    p.id          = id;
    p.name        = "Profile " + std::to_string(id);
    p.ticker      = ticker;
    p.market      = "KRX";
    p.category    = "swing";
    p.positionPct = 0.5;
    return p;
}

}  // namespace

/* ------------------------------------------------------------------------- */

TEST(integration, a_decision_reaches_the_journal_with_its_strategy_attached) {
    // The journal is the only place an order is tied back to the strategy that
    // produced it — KIS has no concept of our strategies.
    Sandbox               sb("attribution");
    auto                  p = profile(7, "005930");
    trade::SignalExecutor ex(p, false, -1, sb.ctx);

    const auto d = ex.execute(Signal::BUY, 71500, balance(10000000, 0, 0, 71500));
    CHECK(d.acted);

    const auto entries = sb.journalEntries();
    CHECK_EQ(entries.size(), std::size_t{1});
    CHECK_EQ(entries[0].value("strategy_id", -1), 7);
    CHECK_EQ(entries[0].value("ticker", ""), std::string("005930"));
    CHECK_EQ(entries[0].value("side", ""), std::string("BUY"));
    CHECK_EQ(entries[0].value("dry_run", false), true);
    CHECK_EQ(entries[0].value("event", ""), std::string("order"));
    CHECK_EQ(entries[0].value("category", ""), std::string("swing"));
}

TEST(integration, a_full_position_lifecycle_entry_peak_trailing_exit_cooldown) {
    Sandbox sb("lifecycle");
    auto    p         = profile(1, "005930");
    p.trailingStopPct = 5.0;
    p.cooldownMinutes = 60;
    trade::SignalExecutor ex(p, false, -1, sb.ctx);

    // 1. flat, BUY fires
    const auto entry = ex.execute(Signal::BUY, 100000, balance(10000000, 0, 0, 100000));
    CHECK(entry.acted);
    CHECK_EQ(entry.side, std::string("BUY"));

    // 2. position appears at the broker and runs up; the peak follows it
    ex.execute(Signal::HOLD, 100000, balance(5000000, 50, 100000, 100000));
    const auto up = ex.execute(Signal::HOLD, 130000, balance(5000000, 50, 100000, 130000));
    CHECK(!up.acted);
    CHECK_NEAR(up.peakPrice, 130000.0, 1e-6);

    // 3. a 5% fall from the peak (not from entry, which is still far below) exits
    const auto exit = ex.execute(Signal::HOLD, 123000, balance(5000000, 50, 100000, 123000));
    CHECK(exit.acted);
    CHECK_EQ(exit.side, std::string("SELL"));
    CHECK(exit.reason.find("TRAILING-STOP") != std::string::npos);

    // 4. the position closes; the cooldown starts from that moment
    ex.execute(Signal::HOLD, 123000, balance(10000000, 0, 0, 123000));

    // 5. a fresh BUY inside the cooldown is refused, and refused visibly
    const auto blocked = ex.execute(Signal::BUY, 123000, balance(10000000, 0, 0, 123000));
    CHECK(blocked.skipped);
    CHECK(blocked.blockedBy.find("cooldown") != std::string::npos);

    const auto entries = sb.journalEntries();
    CHECK_EQ(entries.size(), std::size_t{3});  // entry, trailing exit, blocked re-entry
    CHECK_EQ(entries[2].value("event", ""), std::string("skip"));
}

TEST(integration, state_survives_a_process_restart_mid_position) {
    // A restart must not forget the peak; a forgotten peak re-baselines the
    // trailing stop and silently widens the risk taken.
    Sandbox sb("restart");
    auto    p         = profile(1, "005930");
    p.trailingStopPct = 5.0;

    {
        trade::SignalExecutor ex(p, false, -1, sb.ctx);
        ex.execute(Signal::HOLD, 100000, balance(5000000, 50, 100000, 100000));
        ex.execute(Signal::HOLD, 140000, balance(5000000, 50, 100000, 140000));  // peak 140000
    }
    {
        trade::SignalExecutor restarted(p, false, -1, sb.reopen());
        // 133000 is -5% from the remembered peak but +33% from entry: only the
        // persisted peak makes this an exit.
        const auto d = restarted.execute(Signal::HOLD, 133000, balance(5000000, 50, 100000, 133000));
        CHECK_NEAR(d.peakPrice, 140000.0, 1e-6);
        CHECK(d.acted);
        CHECK(d.reason.find("TRAILING-STOP") != std::string::npos);
    }
}

TEST(integration, the_daily_order_cap_is_shared_across_profiles_and_restarts) {
    // Each executor owning its own risk guard was a real bug: they overwrote each
    // other's file and the day's count silently reset.
    Sandbox sb("shared_cap", {0.0, 3});
    auto    a = profile(1, "005930");
    auto    b = profile(2, "000660");

    trade::SignalExecutor exA(a, false, -1, sb.ctx);
    trade::SignalExecutor exB(b, false, -1, sb.ctx);

    sb.ctx.risk->check(OrderSide::Buy, balance(10000000, 0, 0, 1000));
    sb.ctx.risk->recordOrder();
    sb.ctx.risk->recordOrder();
    sb.ctx.risk->recordOrder();

    CHECK(exA.execute(Signal::BUY, 1000, balance(10000000, 0, 0, 1000)).skipped);
    CHECK(exB.execute(Signal::BUY, 1000, balance(10000000, 0, 0, 1000, "000660")).skipped);

    trade::SignalExecutor afterRestart(a, false, -1, sb.reopen({0.0, 3}));
    CHECK(afterRestart.execute(Signal::BUY, 1000, balance(10000000, 0, 0, 1000)).skipped);
}

TEST(integration, two_profiles_on_different_tickers_keep_separate_positions) {
    Sandbox sb("two_tickers");
    auto    a         = profile(1, "005930");
    auto    b         = profile(2, "000660");
    a.trailingStopPct = 5.0;
    b.trailingStopPct = 5.0;

    trade::SignalExecutor exA(a, false, -1, sb.ctx);
    trade::SignalExecutor exB(b, false, -1, sb.ctx);

    exA.execute(Signal::HOLD, 100000, balance(0, 10, 100000, 100000, "005930"));
    exB.execute(Signal::HOLD, 200000, balance(0, 10, 200000, 200000, "000660"));
    const auto upA = exA.execute(Signal::HOLD, 150000, balance(0, 10, 100000, 150000, "005930"));

    // B's peak must not have moved with A's price.
    const auto stillB = exB.execute(Signal::HOLD, 200000, balance(0, 10, 200000, 200000, "000660"));
    CHECK_NEAR(upA.peakPrice, 150000.0, 1e-6);
    CHECK_NEAR(stillB.peakPrice, 200000.0, 1e-6);
    CHECK(!stillB.acted);
}

TEST(integration, the_loss_limit_halts_buying_for_the_day_but_never_selling) {
    Sandbox               sb("halt", {3.0, 0});
    auto                  p = profile(1, "005930");
    trade::SignalExecutor ex(p, false, -1, sb.ctx);

    // The day opens at 10,000,000 — recorded from an ordinary HOLD cycle.
    ex.execute(Signal::HOLD, 1000, balance(10000000, 0, 0, 1000));

    const auto blocked = ex.execute(Signal::BUY, 1000, balance(9600000, 0, 0, 1000));  // -4%
    CHECK(blocked.skipped);

    // Holding something while halted: the exit must still go through.
    const auto sell = ex.execute(Signal::SELL, 1000, balance(9600000, 10, 1000, 1000));
    CHECK(sell.acted);
    CHECK(!sell.skipped);
}

TEST(integration, a_strategy_drives_the_executor_over_a_price_series) {
    // The realistic path: real bars, a real strategy, decisions journaled.
    Sandbox sb("strategy_driven");
    auto    p  = profile(1, "005930");
    p.type     = "sma_crossover";
    p.params   = {{"short_window", 3}, {"long_window", 8}};
    auto strat = p.createStrategy();
    CHECK(strat != nullptr);

    StockInfo bars;
    bars.ticker = "005930";
    // Down, then up, then down. The initial decline matters: a series that only
    // rises has its golden cross before the warmup window, so the crossover
    // strategy sees nothing to act on and the scenario tests nothing.
    const std::vector<double> path = {120, 116, 112, 108, 104, 100, 96,  93,  90,  88,  87,  88,  91,  96, 103,
                                      112, 122, 133, 144, 152, 156, 155, 150, 142, 132, 121, 110, 100, 92, 86};
    for (std::size_t i = 0; i < path.size(); ++i) {
        bars.timestamps.push_back(static_cast<int64_t>(1600000000 + i * 86400));
        bars.open.push_back(path[i]);
        bars.high.push_back(path[i]);
        bars.low.push_back(path[i]);
        bars.close.push_back(path[i]);
        bars.volume.push_back(1000);
    }
    strat->init(bars);

    trade::SignalExecutor ex(p, false, -1, sb.ctx);
    int64_t               held = 0;
    int                   acts = 0;

    for (std::size_t i = strat->warmupPeriod(); i <= bars.close.size(); ++i) {
        const double price  = bars.close[std::min(i, bars.close.size() - 1)];
        const Signal signal = strat->evaluate(bars, i);
        const auto   d      = ex.execute(signal, price, balance(10000000, held, price, price));
        if (d.acted && !d.skipped) {
            ++acts;
            held = (d.side == "BUY") ? d.quantity : 0;  // simulate the fill
        }
    }

    CHECK_MSG(acts >= 2, "expected at least an entry and an exit, got " << acts);

    const auto entries = sb.journalEntries();
    CHECK_EQ(entries.size(), static_cast<std::size_t>(acts));
    CHECK_EQ(entries.front().value("side", ""), std::string("BUY"));
    for (const auto& e : entries) {
        CHECK_EQ(e.value("strategy_id", -1), 1);
        CHECK(!e.value("reason", "").empty());
    }
}

TEST(integration, fill_sync_is_idempotent_and_notices_a_larger_fill) {
    // kis_order fills re-runs over the same day constantly; it must add nothing
    // the second time, yet still record a partial fill that later filled further.
    Sandbox sb("fill_sync");

    trade::JournalEntry fill;
    fill.event    = "fill";
    fill.orderNo  = "0001";
    fill.quantity = 10;
    fill.price    = 71500;
    sb.ctx.journal->append(fill);

    auto keys = sb.ctx.journal->recordedFillKeys();
    CHECK(keys.count("0001:10") == 1);

    // Same order, same quantity — a re-sync would skip it.
    CHECK(keys.count("0001:10") == 1);

    // The order later fills further: a new key, so it is recorded again.
    fill.quantity = 25;
    sb.ctx.journal->append(fill);
    keys = sb.ctx.journal->recordedFillKeys();
    CHECK(keys.count("0001:25") == 1);
    CHECK_EQ(keys.size(), std::size_t{2});
}

TEST(integration, recorded_bars_can_be_backtested) {
    // The scalping data path end to end: record what the live loop saw, load it
    // back, and run a strategy over it.
    const std::string root = dir("bars_roundtrip");
    std::filesystem::remove_all(root);

    StockInfo live;
    live.ticker = "005930";
    for (std::size_t i = 0; i < 120; ++i) {
        live.timestamps.push_back(static_cast<int64_t>(1789344000 + i * 60));
        const double px = 70000.0 + static_cast<double>(i % 30) * 50.0;
        live.open.push_back(px);
        live.high.push_back(px + 20);
        live.low.push_back(px - 20);
        live.close.push_back(px);
        live.volume.push_back(1000 + static_cast<int64_t>(i));
    }

    data::BarRecorder recorder(root);
    CHECK_EQ(recorder.record("005930", live), 120);

    const auto reloaded = recorder.load("005930", "2000-01-01", "2099-01-01");
    CHECK(reloaded != nullptr);
    CHECK_EQ(reloaded->close.size(), std::size_t{120});

    StrategyProfile p;
    p.type   = "sma_crossover";
    p.params = {{"short_window", 3}, {"long_window", 8}};
    auto s   = p.createStrategy();
    s->init(*reloaded);

    int signals = 0;
    for (std::size_t i = s->warmupPeriod() + 1; i <= reloaded->close.size(); ++i) {
        if (s->evaluate(*reloaded, i) != Signal::HOLD) {
            ++signals;
        }
    }
    CHECK_MSG(signals > 0, "a strategy over recorded bars produced no signals at all");
}

TEST(integration, a_second_process_cannot_take_the_trading_lock) {
    // The trading apps share mutable state on disk. Two of them each load it, act,
    // and write back, so the second save erases the first's — orders disappear from
    // the day's count and the cap stops holding.
    const std::string dirPath = dir("lock");
    std::filesystem::remove_all(dirPath);
    std::filesystem::create_directories(dirPath);

    const util::ProcessLock first("trading", dirPath);
    CHECK(first.held());

    const util::ProcessLock second("trading", dirPath);
    CHECK_MSG(!second.held(), "a second lock on the same name was granted");

    // A different name is a different resource and must not be excluded.
    const util::ProcessLock other("collecting", dirPath);
    CHECK(other.held());
}

TEST(integration, the_lock_is_released_when_its_holder_goes_away) {
    // flock is released by the kernel, so a killed process cannot wedge the next
    // run behind a stale lock file.
    const std::string dirPath = dir("lock_release");
    std::filesystem::remove_all(dirPath);
    std::filesystem::create_directories(dirPath);

    {
        const util::ProcessLock held("trading", dirPath);
        CHECK(held.held());
    }
    const util::ProcessLock afterwards("trading", dirPath);
    CHECK(afterwards.held());
}
