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

Almost everything you will want to change lives in **`config/live.json`**,
and both trading apps pick up an edit without a restart — the scalper re-reads
the file when it changes on disk, and `trader` starts fresh every run.

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
| `config/live.json` | Which strategy trades which ticker, and risk limits | Often |
| `config/strategies.json` | Strategy definitions and parameter grids, with no ticker | Sometimes |
| `config/universe.json` | Tickers the sweep crosses strategies with | Rarely |
| systemd units | Which profile scalps, poll interval, `--live` | Rarely |
| `config/krx_holidays.json` | KRX closures | Once a year |
| `.env` | KIS credentials, Telegram token | Almost never |

`config/macro_allocation.json`, `config/macro_sweep.json` and
`config/strategies/` belong to the macro analysis and sweep tools. They have no
effect on automated trading.

### The three config files

A strategy definition and the ticker it trades are separate decisions, so they
live in separate files. A backtest and the trader then reference **one**
definition instead of each carrying a copy that can drift apart.

**`config/strategies.json`** — definitions, no tickers:

```json
{
  "strategies": [
    { "id": "bb40", "type": "bollinger", "category": "swing",
      "params": { "period": 40, "std_devs": 2.0 } }
  ],
  "grids": [
    { "type": "bollinger", "params": { "period": [14,20,30,40], "std_devs": [1.5,2.0,2.5] } }
  ]
}
```

A `grid` expands to one definition per combination, with a stable generated id
(`bollinger(40,2.0)`), so widening a sweep is a config edit rather than a code
change. Stable ids matter: two sweep reports cannot be compared if the same
combination is named differently each run.

**`config/live.json`** — which definition trades which ticker:

```json
{
  "initial_capital_krw": 10000000,
  "risk": { "daily_loss_limit_pct": 3.0, "max_orders_per_day": 20 },
  "positions": [
    { "id": 30, "strategy": "bb40", "ticker": "005930", "market": "KRX",
      "position_pct": 0.2, "stop_loss_pct": 8.0, "enabled": true }
  ]
}
```

| Field | Effect |
|---|---|
| `strategy` | Id from the catalog. **An id that does not resolve drops that position and is reported** — it never falls back to something else. |
| `ticker` | 6-digit KRX code. Only `market: "KRX"` can trade; US positions are backtest-only. |
| `position_pct` | Fraction of **cash balance** committed per buy. `0.2` = 20%. |
| `stop_loss_pct` | Forced exit this far below the holding's average price. `0` disables. |
| `take_profit_pct` | Forced exit this far above average price. |
| `trailing_stop_pct` | Forced exit this far below the **peak since entry**. Protects a gain already made. |
| `cooldown_minutes` | Refuse to re-enter this ticker for this long after an exit. |
| `entry_tranches` / `exit_tranches` | Split the position over N orders. `1` = all at once. |
| `trade_window` | `{"start":"0930","end":"1520"}` — entries only inside this KST window. |
| `enabled` | `false` keeps a position out of live trading while leaving it backtestable. |
| `category` | `scalp` makes the trader poll it every interval; anything else runs once a day. |
| `params` | Optional. Overrides the catalog's, so one position can be adjusted without forking the shared definition. |

A position may also carry `type` and `params` inline instead of a `strategy`
reference, which keeps a self-contained file usable.

**Exit priority**: stop-loss → trailing stop → take-profit → strategy SELL. The
first three are *forced exits* and always sell the whole position, since scaling
out of a stop defeats it. Only a strategy SELL respects `exit_tranches`.

**`trade_window` and `cooldown_minutes` gate entries only.** Exits are never
blocked — refusing to close a position is the one thing these rules must not do.

Entry tranches are sized from the cash available at the time of each one, so a
staged entry lands slightly *under* `position_pct` rather than over it.

**`config/universe.json`** — the tickers a sweep crosses strategies with. It says
in the file that every listing is a currently-listed company, so results from it
are an upper bound.

### Risk limits

Enforced by `trade::RiskGuard` on every order, in both apps:

- `daily_loss_limit_pct` — once account equity falls this far below the day's
  opening equity, **buys** are blocked for the rest of the day.
- `max_orders_per_day` — cap on orders the broker accepted per calendar day. An
  order KIS rejects outright is not counted: it moved nothing, and a broker outage
  that refuses everything must not spend the allowance so that buys are blocked
  once it recovers. An order whose response was lost *is* counted, since it may
  have filled; the next session's fill reconciliation settles it. The right value
  depends on the strategy; what it counts does not.

State lives in `data/risk_state.json` (`date`, `opening_equity`, `orders`), so
restarting a process does **not** reset the count. It re-baselines on the first
balance of each new KST day.

**Sells are never blocked.** A limit that prevented closing a losing position
would do the opposite of what it exists for.

A blocked order is written to the journal as `"event": "skip"` with the limit
that stopped it in `message`, so nothing disappears silently.

---

## Asking the account questions from a phone

`kairos-bot` polls Telegram once a minute and answers, then exits. It is a timer
rather than a daemon for the same reason the trader is: nothing here needs to be
resident, and a one-shot cannot leak a connection overnight.

| Command | Answers |
|---|---|
| `/status` | Valuation, return against principal, the stock/cash split |
| `/positions` | Each holding's quantity, average price and unrealised P/L |
| `/signals` | What each live strategy says right now, and what is held |
| `/trades` | The last ten journal rows — orders and fills |
| `/help` | The list above |

**It answers only `TELEGRAM_CHAT_ID`.** A bot's username is searchable, so anyone
who finds it can message it; without that check the first stranger to type
`/status` reads the account. Everyone else gets silence rather than a refusal,
because a refusal confirms the bot is live and worth probing. Ignored attempts are
logged.

The bot never places an order. It reads the broker and the journal, nothing else.

`data/telegram_offset.json` holds the last update id handled. Telegram redelivers a
message until it is acknowledged, so deleting that file makes the bot answer the
whole backlog again. It is written after a batch rather than during it, so a crash
mid-batch leaves the unanswered messages to come back instead of losing them.

If replies stop arriving: `systemctl --user status kairos-bot` first, then
`./build/Release/app/bot` by hand — it prints why it gave up.

---

## What the systemd units control

Edit them with `systemctl --user edit --full <unit>`, not by hand in
`deploy/systemd/` — that directory holds the templates the installer copies from.

There are four, and only one of them can move money.

| Unit | Runs | Nature |
|---|---|---|
| **`kairos-trader.timer`** | Fires `trader --once` at `Mon..Fri 15:15` | **The only thing that places orders.** Enable the timer, not the service |
| `kairos-trader.service` | `trader --once --daily-at 1515` | One-shot; reads `inactive (dead)` between runs — that is normal |
| `kairos-dashboard.service` | `scripts/dashboard_server.py --port 8800` | Reads the account; places no orders |
| `kairos-collector.timer` | `bar_collect --interval 5m --range 1mo`, Sundays | Public price data only; independent of trading |
| `kairos-bot.timer` | every minute | Answers Telegram commands. Reads only. |

There is one trader because a strategy's bar size is a property of the strategy,
not a reason for a second service — and because two trading processes would
share the risk guard, position store and journal and erase each other's writes.
The trader takes an `flock` and refuses to start if one is already running.

Which profiles it runs is entirely in the config: every `enabled` KRX profile.
A profile with `category: "scalp"` is polled every `--interval`; every other
profile is evaluated **once a day**, at or after `--daily-at`.

These live in the **unit**, not the config:

- **poll interval** (`--interval`, default 60s)
- **time of day for once-a-day profiles** (`--daily-at`, default 1515)
- **per-profile session order cap** (`--max-trades`, default 10)
- **`--live`** — without it, nothing is ever ordered

### Going live

One line, in one unit:

```bash
systemctl --user edit --full kairos-trader.service    # append --live to ExecStart
systemctl --user daemon-reload
```

### Timer or loop

The trader runs in either shape, chosen by the unit rather than the code.

**Timer (`--once`, current).** Right while only once-a-day profiles are enabled:
no process sleeps for 23 hours, and systemd handles clock changes and resume
better than a loop would. `Persistent=true` makes up a run missed because the
machine was busy — bounded by the trader's own guards, which refuse outside
market hours and record that a profile already ran today, so a catch-up cannot
double-trade.

**Loop (no `--once`).** Required for minute-bar (`scalp`) profiles, which must be
polled. Set `Type=simple`, drop `--once`, add `Restart=on-failure`, then enable
the `.service` instead of the `.timer`.

A once-a-day profile records the KST date it last evaluated, in
`data/schedule.json`. It therefore runs once per day and not again, but **is
still run if the process was busy or restarting at `--daily-at`** — being late
is better than skipping the day, which is what a wall-clock timer would have
done.

---

## Where to look

| What | Where |
|---|---|
| Return vs principal, holdings | <http://localhost:8800> |
| Every decision, with strategy attribution | `data/trades.jsonl` |
| What KIS itself recorded | `./build/Release/app/kis_order fills` |
| Live logs | `journalctl --user -u kairos-trader -f` |
| Durable run logs | `logs/trader-YYYY-MM-DD.log`, `logs/trader-…` |
| Prices a decision used | `data/bars/<ticker>/daily.csv` |
| Next timer fire | `systemctl --user list-timers 'kairos*'` |

`cache/` holds the KIS token and dashboard snapshots. It regenerates itself;
ignore it.

Both trading apps tee stdout and stderr into `logs/<app>-<KST date>.log`, appending
a `=====` header per run. This is separate from journald and survives its
rotation. The file is flushed per line, so a loop stopped by a signal does not
lose its tail.

`trader` also archives the bars each decision was made on to
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
| `order_unknown` | **Sent, outcome never received.** It may or may not exist at the broker. |

An `order_unknown` is the one entry that needs a human. The request left the
process and never came back, so KIS may have accepted it with only the response
lost. Do not re-send it — run `kis_order fills` and let the broker's own record
settle whether it exists.

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
1. Edit the strategy's `params` in `config/strategies.json`, or override them for
   one position in `config/live.json`
2. `systemctl --user restart kairos-trader` — only if the scalper uses that profile
3. Daily profiles need nothing; the next 15:15 run reads the file

**Scalp a different strategy**
```bash
systemctl --user edit --full kairos-trader.service   # change --id
systemctl --user daemon-reload && systemctl --user restart kairos-trader
```

**Add a profile to the daily run** — append it to `strategies` with
`market: "KRX"` and a category other than `scalp`. Nothing else to do.

**Stop everything immediately**
```bash
systemctl --user stop kairos-trader kairos-trader
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
`trader` always reads fresh.

**Never run two trading processes against one account.** Each keeps its own risk
guard and position store while writing the same files, so the day's order count
and other tickers' peaks are lost on every save, and each sizes orders from the
full cash balance — committing twice what you intended. This is why there is one
trader service and why it takes a lock; to trade several tickers, enable several
profiles.

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

The loss limit measures against the **higher of the day's opening equity and the
previous session's last known equity**. Both are needed: a loop starting at 09:00
has a meaningful intraday baseline, but `trader` sees exactly one balance
per day and would otherwise be comparing it against itself.

**"Another kairos trading process is already running."** Exactly what it says —
the apps share the risk guard, position store and journal, so a second one would
erase the first's writes. Usually a systemd timer firing while the same command
is run by hand. The lock is an `flock` on `data/trading.lock`, released by the
kernel, so it cannot go stale.

**A buy was skipped with "allocation below one share".** `position_pct` of the
available cash did not cover a single share. Not an error — buying one anyway
would have committed far more than the configured fraction.

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
./build/Release/app/research/scalp_backtest --id 18                 # net of real costs
./build/Release/app/research/scalp_backtest --id 18 --gross         # costs zeroed
```

`trader` saves every poll's bars automatically, so the archive grows a day
per session. Yahoo's measured limits: 1m ~5 days, 5m/15m/30m ~1 month, 1h ~1 year.

`kairos-collector.timer` runs `bar_collect` weekly against a one-month window, so
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
./build/Release/app/research/sweep                                   # all strategies x universe
./build/Release/app/research/sweep --strategy bollinger               # one strategy's parameter grid
./build/Release/app/research/sweep --split 2022-01-01 --end 2022-12-31 --start 2019-01-01
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

# one cycle in dry-run, ignoring market hours
./build/Release/app/trader --once
./build/Release/app/trader --once     # one cycle, ignores market hours

# backtest instead of trade
./build/Release/app/research/run_strategy --list
./build/Release/app/research/run_strategy --id 18 --start 2021-01-01 --end 2026-09-18
```

`run_strategy` never places orders — it is the backtest path. `trader` is
its live counterpart.

---

## After changing C++

```bash
cmake --build build/Release
systemctl --user restart kairos-trader kairos-dashboard
```

The daily timer needs no restart; it launches a fresh binary each run.
