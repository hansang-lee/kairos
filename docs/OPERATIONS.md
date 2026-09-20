# Operations

Day-to-day guide to running kairos against a KIS account: what to edit,
what each edit changes, when a restart is needed, and where to look when
something seems wrong.

For what the system *is*, see [../README.md](../README.md). For the roadmap and
what shipped versus what was planned, see [PLAN.md](PLAN.md). For what the
numbers in the output mean, see [GLOSSARY.md](GLOSSARY.md). For placing the
first real order, see [FIRST_LIVE_ORDER.md](FIRST_LIVE_ORDER.md).

---

## The short version

Almost everything you will want to change lives in **`config/portfolio.json`**,
and both trading apps pick up an edit without a restart — the scalper re-reads
the file when it changes on disk, and `daily_trade` starts fresh every run.

When something is not behaving, start here:

```bash
./build/Release/app/doctor            # checks credentials, account, data, config, state
./build/Release/app/doctor --offline  # the same without talking to KIS
```

It exits non-zero on a real failure, so it also works as a pre-flight check
before enabling `--live`.

---

## What you edit

| File | Holds | How often |
|---|---|---|
| `config/portfolio.json` | Strategy profiles and account-wide risk limits | Often |
| systemd units | Which profile scalps, poll interval, `--live` | Rarely |
| `config/krx_holidays.json` | KRX closures | Once a year |
| `.env` | KIS credentials, Telegram token | Almost never |

`config/macro_allocation.json`, `config/macro_sweep.json` and
`config/strategies/` belong to the macro analysis and sweep tools. They have no
effect on automated trading.

### `config/portfolio.json`

```json
{
  "initial_capital_krw": 10000000,
  "risk": {
    "daily_loss_limit_pct": 3.0,
    "max_orders_per_day": 20
  },
  "strategies": [
    {
      "id": 18,
      "name": "Samsung Electronics Scalp SMA",
      "ticker": "005930",
      "market": "KRX",
      "type": "sma_crossover",
      "category": "scalp",
      "params": { "short_window": 3, "long_window": 8 },
      "position_pct": 0.2,
      "stop_loss_pct": 1.0
    }
  ]
}
```

| Field | Effect |
|---|---|
| `ticker` | 6-digit KRX code. Only `market: "KRX"` can trade — US profiles are backtest-only, since overseas order placement is not implemented. |
| `type` | Which strategy class runs. Must be one of the registered types below. |
| `params` | Strategy parameters. Keys depend on `type`. |
| `position_pct` | Fraction of **cash balance** committed per buy. `0.2` = 20%. |
| `stop_loss_pct` | Forced exit when price falls this far below the holding's average price. `0` disables. |
| `take_profit_pct` | Forced exit when price rises this far above average price. |
| `trailing_stop_pct` | Forced exit this far below the **peak since entry**. Protects a gain already made. |
| `cooldown_minutes` | Refuse to re-enter this ticker for this long after an exit. Stops chop-driven churn. |
| `entry_tranches` / `exit_tranches` | Split the position over N orders. `1` = all at once. |
| `trade_window` | `{"start":"0930","end":"1520"}` — entries only inside this KST window. |

**Exit priority**: stop-loss → trailing stop → take-profit → strategy SELL. The
first three are *forced exits* and always sell the whole position, since scaling
out of a stop defeats it. Only a strategy SELL respects `exit_tranches`.

**`trade_window` and `cooldown_minutes` gate entries only.** Exits are never
blocked — refusing to close a position is the one thing these rules must not do.

Entry tranches are sized from the cash available at the time of each one, so a
staged entry lands slightly *under* `position_pct` rather than over it.
| `category` | `swing` / `trend` / `position` / `scalp`. **`scalp` excludes a profile from `daily_trade --all`**, so the two loops never fight over one position. |

Registered `type` values:

```
sma_crossover  rsi  macd  bollinger  stochastic_reversal  williams_r
cci_reversal   mfi_reversal  adx_trend  supertrend  aroon_trend
psar_trend     donchian_breakout  obv_trend  keltner_breakout  ma_slope_trend
regime_rsi     volume_breakout  squeeze_breakout  ichimoku_trend
```

The last four pair an entry with a filter (trend regime, volume confirmation,
volatility squeeze, cloud position). On the data tested so far they cut drawdown
roughly in half but **underperform the plain versions on return** — see the
commit that added them. Treat them as available, not recommended.

Short aliases also work: `sma`, `adx`, `aroon`, `cci`, `mfi`, `obv`, `psar`,
`donchian`, `keltner`, `stochastic`, `slope_trend`.

Anything outside this list needs C++ — a new strategy class plus a branch in
`src/strategy/strategy_factory.cpp`.

### Risk limits

Enforced by `trade::RiskGuard` on every order, in both apps:

- `daily_loss_limit_pct` — once account equity falls this far below the day's
  opening equity, **buys** are blocked for the rest of the day.
- `max_orders_per_day` — cap on orders actually sent per calendar day.

State lives in `data/risk_state.json` (`date`, `opening_equity`, `orders`), so
restarting a process does **not** reset the count. It re-baselines on the first
balance of each new KST day.

**Sells are never blocked.** A limit that prevented closing a losing position
would do the opposite of what it exists for.

A blocked order is written to the journal as `"event": "skip"` with the limit
that stopped it in `message`, so nothing disappears silently.

---

## What the systemd units control

Edit them with `systemctl --user edit --full <unit>`, not by hand in
`deploy/systemd/` — that directory holds the templates the installer copies from.

| Unit | Runs | Nature |
|---|---|---|
| `kairos-scalp.service` | `scalp_trade --id 18 --interval 60` | Long-running loop; should read `active (running)` |
| | `--id` takes a list (`--id 18,19`) or `--all-scalp` for every `scalp` profile | One process, never two — see below |
| `kairos-daily.service` | `daily_trade --all` | One-shot; reads `inactive (dead)` between runs — that is normal |
| `kairos-daily.timer` | Fires the above at `Mon..Fri 15:15` | The thing you enable, not the service |
| `kairos-dashboard.service` | `scripts/dashboard_server.py --port 8800` | Reads the account; places no orders |
| `kairos-collect.timer` | `bar_collect --interval 5m --range 1mo`, Sundays | Public price data only; independent of trading |

These live in the **unit**, not the config:

- **which profile the scalper trades** (`--id`)
- **poll interval** (`--interval`, default 60s)
- **per-session order cap** (`--max-trades`, default 10)
- **`--live`** — without it, nothing is ever ordered

`daily_trade --all` picks up any KRX profile that is not `category: "scalp"`, so
adding a profile to the JSON joins the daily run automatically. The scalper is
pinned to one `--id` and does not.

### Going live

`--live` must be added to **both** trading units, or the other half stays in
dry-run:

```bash
systemctl --user edit --full kairos-scalp.service    # append --live to ExecStart
systemctl --user edit --full kairos-daily.service
systemctl --user daemon-reload
systemctl --user restart kairos-scalp
```

The timer fires on wall-clock time and `Persistent=false`, so a run missed
because the machine was asleep at 15:15 is **skipped, not run late** — placing
the day's orders at 18:00 would be worse than placing none.

---

## Where to look

| What | Where |
|---|---|
| Return vs principal, holdings | <http://localhost:8800> |
| Every decision, with strategy attribution | `data/trades.jsonl` |
| What KIS itself recorded | `./build/Release/app/kis_order fills` |
| Live logs | `journalctl --user -u kairos-scalp -f` |
| Durable run logs | `logs/daily_trade-YYYY-MM-DD.log`, `logs/scalp_trade-…` |
| Prices a decision used | `data/bars/<ticker>/daily.csv` |
| Next timer fire | `systemctl --user list-timers 'kairos*'` |

`cache/` holds the KIS token and dashboard snapshots. It regenerates itself;
ignore it.

Both trading apps tee stdout and stderr into `logs/<app>-<KST date>.log`, appending
a `=====` header per run. This is separate from journald and survives its
rotation. The file is flushed per line, so a loop stopped by a signal does not
lose its tail.

`daily_trade` also archives the bars each decision was made on to
`data/bars/<ticker>/daily.csv`. KIS revises and re-serves history, so without
this the inputs to a past decision cannot be recovered.

### Reading the journal

One JSON object per line, appended at decision time — including dry runs, which
is the point: a dry run leaves no other trace.

```json
{"ts":1789782002,"time":"2026-09-19 10:40:02","event":"order","mode":"paper",
 "dry_run":true,"strategy_id":18,"strategy":"Samsung Electronics Scalp SMA",
 "category":"scalp","ticker":"005930","side":"BUY","qty":30,"price":71500.0,
 "reason":"signal BUY","order_no":"","success":false,"message":""}
```

| `event` | Meaning |
|---|---|
| `order` | A decision was made. `dry_run` says whether it was actually sent. |
| `skip` | Suppressed by a risk limit or the session cap; `message` says which. |
| `fill` | Confirmed by KIS, with the real average price. Written by `kis_order fills`. |

```bash
# today's decisions
grep "$(date +%F)" data/trades.jsonl | jq -c '{time,side,ticker,qty,reason,dry_run}'

# what actually filled
./build/Release/app/kis_order fills
```

`kis_order fills` is safe to re-run: records are keyed on order number plus
filled quantity, so nothing duplicates, but a partial fill that later fills
further is recorded again. KIS recommends querying after 15:30 — earlier in the
session same-day results can still be incomplete.

---

## Common tasks

**Change a strategy's parameters**
1. Edit `params` in `config/portfolio.json`
2. `systemctl --user restart kairos-scalp` — only if the scalper uses that profile
3. Daily profiles need nothing; the next 15:15 run reads the file

**Scalp a different strategy**
```bash
systemctl --user edit --full kairos-scalp.service   # change --id
systemctl --user daemon-reload && systemctl --user restart kairos-scalp
```

**Add a profile to the daily run** — append it to `strategies` with
`market: "KRX"` and a category other than `scalp`. Nothing else to do.

**Stop everything immediately**
```bash
systemctl --user stop kairos-scalp kairos-daily.timer
```
This stops new orders. It does **not** close open positions — sell those through
the broker or with `kis_order sell <ticker> <qty>`.

**Pause trading but keep the dashboard** — stop the two trading units only;
`kairos-dashboard` is independent.

---

## When something looks wrong

**Nothing runs after logout.** systemd user services stop at logout unless
lingering is on:
```bash
loginctl show-user "$USER" -p Linger --value    # expect: yes
sudo loginctl enable-linger "$USER"
```

**The timer fires at the wrong hour.** `OnCalendar` is wall-clock. The host
must be on `Asia/Seoul`, or `15:15` is not 15:15 KST:
```bash
timedatectl show -p Timezone --value
```

**Config edits seem ignored.** The scalper re-reads the file when its mtime
changes, usually within one `--interval`. If it did not, the log says why
("reloaded empty", "no runnable profiles") and it kept the previous config.
`daily_trade` always reads fresh.

**Never run two trading processes against one account.** Each keeps its own risk
guard and position store while writing the same files, so the day's order count
and other tickers' peaks are lost on every save, and each sizes orders from the
full cash balance — committing twice what you intended. To trade several
tickers, give one `scalp_trade` several ids.

**"Market closed" on a day the market is open.** Check
`config/krx_holidays.json` — a wrong entry there would skip a real trading day.
Dates after the file's `verified_through` are projected, not confirmed.

**The strategy never signals.** Check the bar count in the log: a strategy needs
more than `warmupPeriod()` bars. The scalper builds these from today's minute
bars only, so early in the session it prints `Only N bars so far`.

**Orders are blocked.** Look for `[BLOCKED]` in the log and the matching `skip`
in the journal — it names the limit. Inspect `data/risk_state.json` to see the
day's opening equity and order count. Deleting that file re-baselines the day,
which also discards the loss limit's reference point; do it knowingly.

**Alerts never arrive.** Both apps print `alerts=on/off` at startup. If off,
`TELEGRAM_BOT_TOKEN` or `TELEGRAM_CHAT_ID` is empty. Verify with:
```bash
./build/Release/app/notify_test "test"
```

---

## Intraday data and scalping

Minute bars cannot be fetched retroactively — KIS serves only today's, Yahoo
about five days at 1-minute. So the archive under `data/bars/` (gitignored) is
built up as you go, and it is the only route to ever backtesting a scalper.

```bash
./build/Release/app/bar_collect --interval 1m --range 5d   # seed from Yahoo
./build/Release/app/scalp_backtest --id 18                 # net of real costs
./build/Release/app/scalp_backtest --id 18 --gross         # costs zeroed
```

`scalp_trade` saves every poll's bars automatically, so the archive grows a day
per session. Yahoo's measured limits: 1m ~5 days, 5m/15m/30m ~1 month, 1h ~1 year.

`kairos-collect.timer` runs `bar_collect` weekly against a one-month window, so
roughly four weeks of overlap covers any missed run. It is `Persistent=true`,
unlike the trading timer: a late collection is harmless, and the window closes
for good. Storage is small — 10 tickers of 5-minute bars run about 2 MB a month.

Each interval is stored separately (`data/bars/<ticker>/<interval>/<date>.csv`).
They must be: a 5-minute series shares timestamps with every fifth 1-minute bar,
so one shared file would silently leave a mixed-resolution series.

**Run `--gross` before believing any intraday result.** It separates the
strategy's raw edge from what trading it costs, and at scalping frequency those
are the same order of magnitude. A 0.335% KRX round trip taken 30 times a day is
a ~2%/day drag on a 20% position, which no minute-bar strategy overcomes.

Note that Sharpe is meaningless on intraday bars here: the engine annualizes by
sqrt(252), which is only correct for daily data. Return, drawdown and win rate
are unaffected.

## Trading costs

`BacktestConfig::forMarket()` carries real KIS rates (checked 2026-09), applied
by `run_strategy` and `scalp_backtest`:

| | commission (per side) | sell-side tax | round trip |
|---|---|---|---|
| KRX | 0.0177% | **0.20%** (증권거래세 + 농특세, raised 2026-01-01) | ~0.24% |
| US | **0.25%** | 0.00206% (SEC fee) | ~0.50% |

US is the more expensive of the two, because of the commission. Cost impact
scales with turnover: across six KRX tickers since 2021, MACD at 310 trades
loses 19.6pp to costs while RSI at 27 trades loses 1pp.

## Tests

```bash
cmake --build build/Release          # tests build with everything else
./build/Release/kairos_tests         # all of them
./build/Release/kairos_tests lookahead   # filter by "suite.name" substring
cd build/Release && ctest --output-on-failure
```

78 cases over indicators, the backtest engine, the trade layer, the data layer,
config parsing, and end-to-end scenarios. They build by default, so a broken test
is a broken build rather than something to remember to run.

The one worth understanding is `lookahead.every_strategy_reads_only_closed_bars`.
It runs each strategy on the full series and again on the series truncated at the
bar being evaluated, and requires the same signal — a strategy reading the bar it
is about to trade cannot pass. It found three such bugs on its first run,
including one in Bollinger, which is what the live profiles use.

Order placement is outside the test boundary: it needs the live KIS API, and a
test that hits a broker is not a test. Everything up to the send is covered.

## Finding a strategy

```bash
./build/Release/app/sweep                                   # all strategies x universe
./build/Release/app/sweep --strategy bollinger               # one strategy's parameter grid
./build/Release/app/sweep --split 2022-01-01 --end 2022-12-31 --start 2019-01-01
```

Selection happens on the period before `--split`; everything after it is reported
untouched. Ranking is by the median across tickers and the share that beat
buy-and-hold, never by the best single combination — testing 20 strategies
against 30 tickers gives 600 results, and the best of 600 is mostly luck.

**Results depend heavily on the regime, so run both.** Over 2025-2026 no strategy
beat buy-and-hold on a majority of the universe; over the 2022 decline most of
them did. A strategy picked from one window alone is picked for that window.

## Manual commands

```bash
# health check — run this first when anything looks wrong
./build/Release/app/doctor

# account
./build/Release/app/kis_order balance
./build/Release/app/kis_order fills [YYYYMMDD] [YYYYMMDD]
./build/Release/app/kis_order buy  005930 1        # market order
./build/Release/app/kis_order sell 005930 1 75000  # limit order

# evaluate without the timer (dry-run; --force works outside market hours)
./build/Release/app/daily_trade --all --force
./build/Release/app/scalp_trade --id 18 --interval 60

# backtest instead of trade
./build/Release/app/run_strategy --list
./build/Release/app/run_strategy --id 18 --start 2021-01-01 --end 2026-09-18
```

`run_strategy` never places orders — it is the backtest path. `daily_trade` is
its live counterpart.

---

## After changing C++

```bash
cmake --build build/Release
systemctl --user restart kairos-scalp kairos-dashboard
```

The daily timer needs no restart; it launches a fresh binary each run.
