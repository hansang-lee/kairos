# Status and hand-off — 2026-09-25

The entry point for whoever picks this project up next, human or agent. It says
what exists, what is in flight, what is decided, and what comes next, in that
order. `docs/PLAN.md` is the original phase plan (v1.0, 2026-09-13) and is not
kept current; this file is. `docs/REVIEW-2026-09-25.md` is the last full review
and every item in it is closed.

## 1. What exists

**Product** (`app/`, 9 binaries; 158 commits; 211 tests passing on `main`, 228 on `feat/vol-target`):

- `trader` — the daily trading daemon. Loads `config/live.json`, fetches bars from
  KIS, runs each enabled position's strategy through `SignalExecutor`, journals
  every decision to `data/trades.jsonl`, sends a Telegram summary. Dry-run by
  default; `--live` sends real orders (paper account for now). Runs from a systemd
  user timer at 15:15 KST on weekdays. Decision logic is in `trade/session.hpp`
  and is unit-tested; the order path goes through the `IBroker` seam.
- `bot` — Telegram query bot (`/status`, `/positions`, `/trades`, `/signals`,
  `/help`), polled every minute by a timer, acknowledges each command first.
- `doctor` — pre-flight checks (token, balance, calendar, config).
- `portfolio_report`, `fetch_universe`, `kis_order` (manual single order),
  `notify_test`, `bar_collect` (intraday bars, Sunday timer), `macro`.
- Research tools in `app/research/` (18): `beat_benchmark`, `portfolio_sweep`,
  `portfolio_robustness`, `leverage_study`, `dca_backtest`, and others. Output to
  the terminal only; conclusions are written up in `docs/BACKTEST_RESULTS.md`.

**Library:** 25 single-ticker strategies in `lib/` behind `IStrategy` (24 signal
strategies plus `vol_target`, which is sized by exposure); `BacktestEngine` with
whole-share sizing, tranches, costs; portfolio engine with group parity, risk
parity, momentum rotation, MA filters, leverage and rebalance sweeps; KIS broker
(`KisBroker`), Yahoo and KIS data providers with a `cache/daily/` CSV cache;
KRX holiday calendar with an audit against observed bars; fill reconciliation;
Telegram notifier.

**Operations:** systemd user units live in the repo (`scripts/install_systemd.sh
--live`), linger enabled, `KIS_MODE=paper`. Dashboard (`scripts/dashboard_server.py`)
binds `127.0.0.1` only. CI builds, tests, and checks formatting on every push.
Backups the user keeps by hand: `.env`, `config/live.json`, `data/`, `cache/kis_token_*.json`.

**Live configuration:** positions 50/51/52, `aroon25` (Aroon 25, strength 70) on
133690 (KODEX NASDAQ100), 069500 (KODEX 200), 132030 (KODEX Gold), 0.33 each, no
stop, daily loss limit 3%, 20 orders/day. Positions 30-44 (bb40, slope_balanced)
are present but disabled.

## 2. In flight

**First live (paper) order — Monday 2026-09-28, 15:15 KST.** No order has ever
gone through the real path. Expected: BUY 133690 ×17 and 069500 ×29 (132030's
signal was HOLD as of 09-25). The date was 09-29 until 09-25, when the holiday
file was found to mark Monday 09-28 as a Chuseok substitute holiday that does not
exist: the substitute rule for Chuseok applies only when the period overlaps a
Sunday or another holiday, and this year's (Thu 24 to Sat 26) overlaps neither.
Verify afterwards with
`journalctl --user -u kairos-trader -n 40`, `/trades` and `/positions` on the bot,
and the KIS app. Fill reconciliation runs at the start of the 09-30 run.
Procedure: `docs/FIRST_LIVE_ORDER.md`.

**Volatility targeting on the live path — branch `feat/vol-target`, worktree
`/home/hslee/workspace/kairos-vt`.** Phase 1 (unlevered, single instrument) is
implemented, tested, and pushed; CI green. It is deliberately not merged: `main`
and the installed binary stay untouched until the 09-29 order is verified. After
that: merge, rebuild, reinstall (`scripts/install_systemd.sh --live`), and decide
whether to switch a live position to `vt15` or `vt20` (both in the catalog).
Details and the measured figures are in `docs/BACKTEST_RESULTS.md` §11.

## 3. Open items

- Phase 2 of volatility targeting: the levered leg. Cap 1.5x needs QQQ and QLD
  (or their KRX equivalents) held in a set ratio, which means one profile driving
  two tickers. Not designed yet.
- Per-ticker `expense_ratio` fields in the universe files are unfilled. Moot
  under the standing decision to ignore fees, but the fields exist.
- `--equity-sweep` has not been run through `portfolio_robustness`.
- Dashboard external access (`docs/PLAN.md` §3-D): bind address done; auth and
  the access path are not. Only needed if the Telegram bot stops being enough.
- KRX holidays after 2026-09-19 are projected (no holiday API on the paper account);
  the trader audits the trailing 14 days against observed bars and alerts on
  disagreement, so a wrong projection is caught after one day, not before.
- Meritz API for real-money trading: researched, not implemented. Meritz has no
  paper trading, so the switch happens only after paper trading through KIS has
  proven the path. Toss is a possible alternative, also without paper trading.

## 4. Decisions in effect

These were made by the user and are not up for re-litigation in a working
session; bring evidence if one looks wrong.

- **Goal:** beat holding QQQ (NASDAQ 100) robustly over any 5/10/15-year window,
  or match it with a smaller drawdown — not a fixed return target.
- **Ignore FX, ETF expense ratios and brokerage commissions** in research and
  reporting. Flag turnover only when it is extreme.
- **Whole shares only** in backtests; ignore volume limits; dividends are in the
  price series (Yahoo adjclose, KIS 수정주가), so they need no separate handling.
- **Brokerage plan:** KIS paper now; Meritz (the user's main account) for real
  money later, via its API; Toss possible.
- **Order caps count only accepted or lost-response orders**, never explicit
  rejections. The cap value (20) stays until a strategy needs otherwise.
- **`app/` is product, `app/research/` is research**; superseded research tools
  are deleted rather than kept.
- **Config, units and scripts live in the repo.** Account data (`.env`, `data/`,
  tokens) never does.

## 5. Safety rules for agents

- Live orders require the user's explicit confirmation. The trader is dry-run
  unless `--live` is passed, and the installer's `--live` flag is what puts it on
  the timer. Never add `--live` on your own.
- Never paste or read out credentials. The user enters them in their own terminal.
- Never expose the dashboard beyond `127.0.0.1` without authentication.
- Never commit `.env`, `data/`, `cache/kis_token_*`, or anything with an account
  number in it.
- Commit and push finished units of work without asking. Never force-push,
  rebase shared history, or reset without explicit confirmation.
- Do not change `main` or reinstall the trader between now and the 09-29
  verification.

## 6. Next steps, in order

1. 09-28 15:15: first live order. Verify per §2. If it fails, fix the path before
   anything else; the backtests are worth nothing until an order leaves.
2. 09-29: confirm fill reconciliation recorded the fills (`/trades` shows fill
   prices; `data/trades.jsonl` has `fill` events).
3. Merge `feat/vol-target`, rebuild, reinstall. Run `doctor`.
4. Decide whether to move a live position to `vt15`/`vt20`, or run one alongside
   aroon25 with a small `position_pct`.
5. Design phase 2 (levered leg) — only after phase 1 has traded live for a while.
6. Ask for a review (Fable) before the Meritz migration, since that is the first
   real-money step.

## 7. Where things are

| Need | Look at |
|---|---|
| How to run anything | `docs/OPERATIONS.md` |
| First-order procedure | `docs/FIRST_LIVE_ORDER.md` |
| Every backtest conclusion | `docs/BACKTEST_RESULTS.md` |
| Last review and its plan | `docs/REVIEW-2026-09-25.md` |
| Original phase plan | `docs/PLAN.md` (historical) |
| Build and test | `CLAUDE.md` |
| Terms | `docs/GLOSSARY.md` |
