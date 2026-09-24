#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "backtest/backtest_engine.hpp"
#include "broker/ibroker.hpp"
#include "strategy/strategy_factory.hpp"
#include "test_framework.hpp"
#include "trade/position_store.hpp"
#include "trade/risk_guard.hpp"
#include "trade/session.hpp"
#include "trade/signal_executor.hpp"
#include "trade/trade_journal.hpp"

/*
 * Does the trader do what the backtest said it would?
 *
 * The backtest engine and the live path are two implementations of one rule —
 * decide on bars up to i-1, fill at bar i — and they have already disagreed
 * twice: the engine filled later tranches on HOLD while the executor waited for a
 * second BUY that never came, and the engine's entry condition and the
 * executor's diverged. Both were found by reading. This finds the next one by
 * running.
 *
 * The live side is driven exactly as the trader drives it: for each day, the
 * series is truncated to what would have been fetched, trade::evaluationIndex
 * picks the bar, the strategy is re-initialised on that truncated series, and the
 * executor sizes against a synthesised balance and fills at the close through a
 * broker that records the fill. Then the entry and exit bars must match what
 * BacktestEngine produced on the whole series, trade for trade.
 */
namespace {

StockInfo walk(std::size_t n) {
    StockInfo s;
    s.ticker      = "TEST";
    double   px   = 100.0;
    uint64_t seed = 424242;
    auto     next = [&] {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>((seed >> 33) % 1000) / 1000.0;
    };
    for (std::size_t i = 0; i < n; ++i) {
        px *= 1.0 + (next() - 0.5) * 0.06;
        s.timestamps.push_back(static_cast<int64_t>(1700000000 + i * 86400));
        s.open.push_back(px);
        s.high.push_back(px * (1.0 + next() * 0.02));
        s.low.push_back(px * (1.0 - next() * 0.02));
        s.close.push_back(px);
        s.volume.push_back(1000);
    }
    return s;
}

/** The bars a trader would have had on day `upTo`, inclusive. */
StockInfo through(const StockInfo& src, std::size_t upTo) {
    StockInfo out;
    out.ticker   = src.ticker;
    const auto m = static_cast<std::ptrdiff_t>(upTo + 1);
    out.timestamps.assign(src.timestamps.begin(), src.timestamps.begin() + m);
    out.open.assign(src.open.begin(), src.open.begin() + m);
    out.high.assign(src.high.begin(), src.high.begin() + m);
    out.low.assign(src.low.begin(), src.low.begin() + m);
    out.close.assign(src.close.begin(), src.close.begin() + m);
    out.volume.assign(src.volume.begin(), src.volume.begin() + m);
    return out;
}

/** Fills every market order at the price the executor sized against. */
class FillingBroker: public IBroker {
   public:
    double  fillPrice = 0.0;  ///< set by the driver before each execute()
    int64_t seq       = 0;

    OrderResult placeOrder(OrderSide, const std::string&, int64_t, double) override {
        OrderResult r;
        r.success = true;
        r.orderNo = "SIM-" + std::to_string(++seq);
        return r;
    }
    AccountBalance getBalance() override { return {}; }
    FillHistory    getDailyFills(const std::string&, const std::string&, bool) override { return {}; }
    std::string    mode() const override { return "paper"; }
};

struct LiveRun {
    std::vector<std::pair<std::size_t, std::size_t>> trades;  ///< (buy bar, sell bar), closed ones only
    bool                                             openAtEnd = false;
    std::size_t                                      openedAt  = 0;  ///< buy bar of the position still open
};

/** Drive the executor day by day, the way the trader does, and record the fills. */
LiveRun driveLive(const StrategyProfile& profile, const StockInfo& full, const std::string& tag) {
    const std::string root = "/tmp/kairos_agree_" + tag;
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    auto                    broker = std::make_shared<FillingBroker>();
    trade::ExecutionContext ctx{std::make_shared<trade::RiskGuard>(trade::RiskLimits{}, root + "/risk.json"),
                                std::make_shared<trade::PositionStore>(root + "/positions.json"),
                                std::make_shared<trade::TradeJournal>(root + "/trades.jsonl"), broker};
    trade::SignalExecutor   ex(profile, true, -1, ctx);

    LiveRun     out;
    double      cash   = 10000000.0;
    int64_t     qty    = 0;
    double      avg    = 0.0;
    std::size_t buyBar = 0;
    const auto  warmup = profile.createStrategy()->warmupPeriod();

    for (std::size_t day = warmup; day < full.close.size(); ++day) {
        // Exactly the trader's inputs on that day: the bars it would have fetched,
        // the index the session picks for them, a strategy initialised on them.
        const StockInfo   have  = through(full, day);
        const std::size_t index = trade::evaluationIndex(have, trade::barDate(full.timestamps[day]));
        CHECK_EQ(index, day);  // today's bar is present, so it is evaluated at itself

        auto strat = profile.createStrategy();
        strat->init(have);
        const Signal signal = strat->evaluate(have, index);
        const double price  = have.close.back();

        AccountBalance bal;
        bal.success     = true;
        bal.cashBalance = cash;
        if (qty > 0) {
            StockHolding h;
            h.ticker       = profile.ticker;
            h.quantity     = qty;
            h.avgPrice     = avg;
            h.currentPrice = price;
            h.evalAmount   = static_cast<double>(qty) * price;
            bal.holdings.push_back(h);
        }
        bal.totalEvalAmount = cash + static_cast<double>(qty) * price;

        broker->fillPrice = price;
        const auto d      = ex.execute(signal, price, bal);
        if (!d.sent || !d.order.success) {
            continue;
        }
        if (d.side == "BUY") {
            avg = (avg * static_cast<double>(qty) + price * static_cast<double>(d.quantity))
                / static_cast<double>(qty + d.quantity);
            qty += d.quantity;
            cash -= price * static_cast<double>(d.quantity);
            buyBar = day;
        } else {
            cash += price * static_cast<double>(d.quantity);
            qty -= d.quantity;
            if (qty == 0) {
                out.trades.emplace_back(buyBar, day);
            }
        }
    }
    out.openAtEnd = qty > 0;
    out.openedAt  = buyBar;
    return out;
}

std::vector<std::pair<std::size_t, std::size_t>> backtestTrades(const StrategyProfile& profile, const StockInfo& full) {
    BacktestConfig cfg;
    cfg.commissionRate                                     = 0.0;
    cfg.slippagePct                                        = 0.0;
    cfg.sellTaxRate                                        = 0.0;
    cfg.positionPct                                        = 1.0;
    cfg.stopLossPct                                        = 0.0;
    auto                                             strat = profile.createStrategy();
    BacktestEngine                                   engine(10000000.0);
    const auto                                       r = engine.run(*strat, full, cfg);
    std::vector<std::pair<std::size_t, std::size_t>> out;
    for (const auto& t : r.trades) {
        out.emplace_back(t.buyIndex, t.sellIndex);
    }
    return out;
}

void agree(const std::string& type, const nlohmann::json& params, const std::string& tag) {
    StrategyProfile p;
    p.id          = 7;
    p.name        = type;
    p.ticker      = "TEST";
    p.market      = "KRX";
    p.type        = type;
    p.params      = params;
    p.positionPct = 1.0;

    const StockInfo series = walk(320);
    auto            bt     = backtestTrades(p, series);
    const auto      live   = driveLive(p, series, tag);

    // The engine closes a position still open on the last bar and records that as
    // a trade, so its result is always fully realised. The live path has no last
    // bar — it is still holding. Those are the same state described two ways, and
    // the first run of this test reported exactly that as a one-trade mismatch.
    const std::size_t lastBar = series.close.size() - 1;
    if (!bt.empty() && bt.back().second == lastBar) {
        const auto synthetic = bt.back();
        bt.pop_back();
        CHECK_MSG(live.openAtEnd, type + ": backtest liquidated an open position at the end but live holds nothing");
        CHECK_MSG(live.openedAt == synthetic.first, type + ": the still-open position was bought at bar "
                                                        << live.openedAt << " live, " << synthetic.first
                                                        << " in the backtest");
    } else {
        CHECK_MSG(!live.openAtEnd, type + ": live is still holding a position the backtest never opened");
    }

    // A failing comparison must show both lists: which trade differs is the
    // whole diagnosis, and a count alone says nothing about where.
    auto show = [](const std::vector<std::pair<std::size_t, std::size_t>>& v) {
        std::string s;
        for (const auto& [b, e] : v) {
            s += "(" + std::to_string(b) + "-" + std::to_string(e) + ") ";
        }
        return s;
    };

    // A pass on zero trades would prove nothing.
    CHECK_MSG(bt.size() >= 2, type + ": backtest made " << bt.size() << " trades; the series is not exercising it");
    CHECK_MSG(bt.size() == live.trades.size(), type + ": backtest closed "
                                                   << bt.size() << " trades, live closed " << live.trades.size()
                                                   << (live.openAtEnd ? " (+1 still open)" : "") << "\n      backtest: "
                                                   << show(bt) << "\n      live:     " << show(live.trades));
    for (std::size_t i = 0; i < bt.size() && i < live.trades.size(); ++i) {
        CHECK_MSG(bt[i].first == live.trades[i].first, type + " trade " << i << ": backtest bought at bar "
                                                                        << bt[i].first << ", live at "
                                                                        << live.trades[i].first);
        CHECK_MSG(bt[i].second == live.trades[i].second, type + " trade " << i << ": backtest sold at bar "
                                                                          << bt[i].second << ", live at "
                                                                          << live.trades[i].second);
    }
}

}  // namespace

TEST(agreement, the_live_path_enters_and_exits_on_the_same_bars_as_the_backtest_for_a_crossover) {
    agree("sma_crossover", {{"short_window", 5}, {"long_window", 20}}, "sma");
}

TEST(agreement, the_live_path_enters_and_exits_on_the_same_bars_as_the_backtest_for_the_live_strategy) {
    // aroon(25, 70) is what config/live.json trades. Emits BUY only on the crossover
    // bar and HOLD after it, which is the shape that exposed the tranche mismatch.
    agree("aroon_trend", {{"period", 25}, {"strength_threshold", 70.0}}, "aroon");
}
