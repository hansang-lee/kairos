# Operations

Day-to-day guide to running kairos against a KIS account: what to edit,
what each edit changes, when a restart is needed, and where to look when
something seems wrong.

For what the system *is*, see [../README.md](../README.md). For the roadmap and
what shipped versus what was planned, see [PLAN.md](PLAN.md).

---

## The short version

Almost everything you will want to change lives in **`config/portfolio.json`**.

The one thing that trips people up: **`scalp_trade` reads the config once, at
startup.** Editing the JSON while it is running changes nothing until you
restart it. `daily_trade` is immune to this because it is a one-shot process —
every 15:15 run reads the file fresh.

```bash
systemctl --user restart kairos-scalp      # after editing config for the scalper
```

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
| `category` | `swing` / `trend` / `position` / `scalp`. **`scalp` excludes a profile from `daily_trade --all`**, so the two loops never fight over one position. |

Registered `type` values:

```
sma_crossover  rsi  macd  bollinger  stochastic_reversal  williams_r
cci_reversal   mfi_reversal  adx_trend  supertrend  aroon_trend
psar_trend     donchian_breakout  obv_trend  keltner_breakout  ma_slope_trend
```

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
| `kairos-daily.service` | `daily_trade --all` | One-shot; reads `inactive (dead)` between runs — that is normal |
| `kairos-daily.timer` | Fires the above at `Mon..Fri 15:15` | The thing you enable, not the service |
| `kairos-dashboard.service` | `scripts/dashboard_server.py --port 8800` | Reads the account; places no orders |

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
| Next timer fire | `systemctl --user list-timers 'kairos*'` |

`cache/` holds the KIS token and dashboard snapshots. It regenerates itself;
ignore it.

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

**Config edits seem ignored.** The scalper read the file at startup. Restart it.

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

## Manual commands

```bash
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
