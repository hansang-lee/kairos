# kairos

> A C++17 quant trading system for the Korean and US equity markets.

[![Daily Macro Report](https://github.com/hansang-lee/kairos/actions/workflows/macro-report.yml/badge.svg)](https://github.com/hansang-lee/kairos/actions)

---

## 📋 Overview

kairos is a C++17 automated trading system: strategies are backtested, then executed against a live broker account. The name is Greek for the decisive moment — the opportune instant to act.

**Core features:**
- **25 technical indicators** — trend (SMA/EMA/WMA/ADX/Parabolic SAR/SuperTrend/Aroon), momentum (RSI/MACD/ROC/CCI/Williams %R/TRIX/Stochastic/MA slope), volume (VWAP/OBV/MFI/CMF/A-D Line), volatility (Bollinger/ATR/StdDev/Keltner/Donchian)
- **16 trading strategies** — grouped by category (swing / trend / position / scalp); see [Strategy categories](#-strategy-categories)
- **Live order execution** — `daily_trade` (daily bars, one run per day) and `scalp_trade` (minute-bar polling) place paper-trading orders on KRX
- **Trade journal** — every decision, including dry runs, is appended to `data/trades.jsonl` with the strategy that produced it
- **Backtest engine** — models commission, slippage and stop-loss; reports a 0–100 composite score
- **Macro analysis** — 12 FRED series + CNN Fear & Greed, mapped onto a 4-regime model
- **KIS OpenAPI integration** — KRX market data plus paper/live order placement and balance inquiry
- **Config-driven strategies** — portfolio profiles in JSON, so tickers and parameters change without touching code
- **Dashboards** — a daily macro report via GitHub Actions, and a local real-time paper-trading dashboard

---

## 🏛️ Architecture

```
kairos/
├── include/                     # C++ headers
│   ├── yfinance.hpp             # Yahoo Finance / FRED / CNN F&G API client
│   ├── indicator.hpp            # 25 technical indicators (trend/momentum/volume/volatility)
│   ├── stock_info.hpp           # StockInfo struct (OHLCV time series)
│   ├── fng_info.hpp             # FearAndGreedInfo struct
│   ├── fred_info.hpp            # FredSeriesInfo struct
│   ├── macro_scorer.hpp         # 5-axis macro score + 4-regime classification
│   ├── strategy/
│   │   ├── istrategy.hpp        # IStrategy pure virtual interface
│   │   └── strategy_factory.hpp # JSON-driven strategy factory (incl. category field)
│   ├── backtest/
│   │   └── backtest_engine.hpp  # Single-ticker backtest engine
│   ├── macro/
│   │   └── macro_backtester.hpp # Macro portfolio backtester
│   ├── broker/
│   │   ├── kis_auth.hpp         # KIS OpenAPI OAuth2 authentication
│   │   └── kis_trader.hpp       # KIS paper/live orders + balance inquiry
│   └── data/
│       ├── idata_provider.hpp   # Data source interface
│       └── kis_provider.hpp     # KIS market data (daily + intraday minute bars)
│
├── src/                         # C++ implementations
├── lib/                         # Strategy implementations (16, one hpp/cpp dir each)
│   │                             # swing:    rsi, bollinger, stochastic_reversal, williams_r, cci_reversal, mfi_reversal
│   │                             # trend:    sma_crossover, macd, adx_trend, supertrend_follow, aroon_trend, psar_trend, ma_slope_trend
│   │                             # position: donchian_breakout, obv_trend, keltner_breakout
│
├── app/                         # CLI executables (16)
├── scripts/                     # dashboard_server.py (local dashboard)
├── config/                      # JSON configuration
│   ├── portfolio.json           # Strategy profiles (loaded at runtime)
│   ├── macro_allocation.json    # Macro allocation settings
│   └── strategies/              # Macro strategy profiles (aggressive/balanced/defensive)
│
├── docs/                        # API docs + GitHub Pages dashboard
└── .github/workflows/           # CI/CD (daily macro report)
```

---

## 🚀 Build

### Prerequisites

```bash
sudo apt install cmake ninja-build libcurl4-openssl-dev nlohmann-json3-dev
```

### Building

```bash
# Release build (default)
./make.sh

# Debug build
./make.sh Debug
```

Build output: `build/Release/` (or `build/Debug/`).

### Docker

```bash
./docker.sh build    # build the image
./docker.sh run      # run the container
```

---

## 📖 Usage

### US stock data

```bash
./build/Release/app/stock AAPL 1d 1y
```

### Korean stock data (KIS API)

```bash
# Requires KIS credentials in .env (see .env.example)
./build/Release/app/kis_stock 005930 2024-01-01 2024-12-31
```

### Paper-trading orders / balance (KIS API)

```bash
# Requires KIS_PAPER_* credentials in .env (paper mode is the default)
./build/Release/app/kis_order balance
./build/Release/app/kis_order buy  005930 1        # market buy, 1 share
./build/Release/app/kis_order sell 005930 1 75000  # limit sell at 75,000 KRW, 1 share
```

### Backtest

```bash
# Backtest the SMA crossover strategy on AAPL over 1 year
./build/Release/app/backtest AAPL sma 1y
```

### Strategy sweep (multi-strategy × multi-ticker comparison)

```bash
./build/Release/app/strategy_sweep
```

### Portfolio-driven strategy runs

```bash
# List registered strategies
./build/Release/app/run_strategy --list

# Run one strategy by ID
./build/Release/app/run_strategy --id 1

# Run every strategy in the portfolio
./build/Release/app/run_strategy --all

# Custom backtest window (default: last ~1 year)
./build/Release/app/run_strategy --id 1 --start 2021-01-01 --end 2026-09-18

# Custom config file
./build/Release/app/run_strategy --config my_portfolio.json --all
```

### Macro analysis

```bash
# Requires a FRED API key
export FRED_API_KEY=your_key
./build/Release/app/macro config/macro_allocation.json
```

### Fear & Greed Index

```bash
./build/Release/app/fng
```

---

## ⚙️ Configuration

### `.env` — environment variables (gitignored)

Copy `.env.example` to `.env` and fill in real keys.

```bash
cp .env.example .env
```

### `config/portfolio.json` — strategy profiles

Strategies are defined in JSON, so tickers and parameters can be added or changed without code edits:

```json
{
  "initial_capital_krw": 10000000,
  "strategies": [
    {
      "id": 1,
      "name": "Samsung RSI",
      "ticker": "005930",
      "market": "KRX",
      "type": "rsi",
      "category": "swing",
      "params": { "period": 14, "oversold": 30.0, "overbought": 70.0 },
      "position_pct": 0.5,
      "stop_loss_pct": 3.0
    }
  ]
}
```

`initial_capital_krw` is the principal the dashboard measures returns against (the paper account's seed money).

**Supported strategy types:** `sma_crossover`, `rsi`, `macd`, `bollinger`, `stochastic_reversal`, `williams_r`, `cci_reversal`, `mfi_reversal`, `adx_trend`, `supertrend`, `aroon_trend`, `psar_trend`, `donchian_breakout`, `obv_trend`, `keltner_breakout`, `ma_slope_trend` (parameter defaults live in `src/strategy/strategy_factory.cpp`).

---

## 🗂️ Strategy categories

`IStrategy` operates on a `StockInfo` (an OHLCV time series) and never assumes daily bars — feeding it minute bars produces signals just the same. Backtesting (historical validation) runs through `run_strategy`; live execution goes through `daily_trade` (daily bars) or `scalp_trade` (minute bars), which share one `SignalExecutor` so sizing and stop-loss rules cannot drift apart.

| Category | Holding period | Character | Strategies | Runner |
|---|---|---|---|---|
| **swing** | days to 1–2 weeks | mean reversion (overbought/oversold) | rsi, bollinger, stochastic_reversal, williams_r, cci_reversal, mfi_reversal | `daily_trade` (daily) |
| **trend** | 1 week to several weeks | trend following | sma_crossover, macd, adx_trend, supertrend, aroon_trend, psar_trend, ma_slope_trend | `daily_trade` (daily) |
| **position** | weeks to months | volatility breakout / volume confirmation | donchian_breakout, obv_trend, keltner_breakout | `daily_trade` (daily) |
| **scalp** | minutes | minute-bar polling, intraday live trading | sma_crossover (short parameters) | `scalp_trade` (intraday) |

Every profile in `config/portfolio.json` carries a `category` field, visible directly in `run_strategy --list`.

### Running the daily strategies live

```bash
# Requires KIS_PAPER_* credentials in .env. Dry-run by default (no real orders).
./build/Release/app/daily_trade --id 3                  # one profile
./build/Release/app/daily_trade --all                   # every KRX profile except 'scalp'

# Add --live to actually place orders on the paper account
./build/Release/app/daily_trade --all --live

# --force evaluates outside KRX hours (dry-run inspection; live orders would be rejected)
./build/Release/app/daily_trade --id 3 --force
```

`daily_trade` runs **once and exits**, placing at most one order per profile — the shape a cron job near the close wants (e.g. `15:15 KST`). It is the live counterpart to `run_strategy`, which only ever backtests.

The index it evaluates is the bar the order would fill at: today's bar once KIS publishes it, otherwise the one past the last. This is the same convention the backtest uses, so a live signal matches what the backtest would have produced on that bar.

### Running the scalper

```bash
# Requires KIS_PAPER_* credentials in .env. Dry-run by default (no real orders).
./build/Release/app/scalp_trade --id 18 --interval 60

# Add --live to actually place orders on the paper account
./build/Release/app/scalp_trade --id 18 --interval 60 --live --max-trades 10
```

KIS's `inquire-time-itemchartprice` (TR_ID `FHKST03010200`) serves **today's minute bars only, ~30 per call**. `scalp_trade` polls only during KRX hours (09:00–15:30 KST, weekdays) and re-reads holdings and average price from `KisTrader::getBalance()` on every cycle, treating KIS as the source of truth rather than keeping local position state. Overseas (US) order placement is not implemented, so only KRX tickers are supported.

### Risk limits

`config/portfolio.json` carries an account-wide `risk` block, enforced by both trading apps:

```json
"risk": {
  "daily_loss_limit_pct": 3.0,
  "max_orders_per_day": 20
}
```

The day's opening equity is recorded on the first balance of each KST day and kept in `data/risk_state.json`, so restarting a process cannot reset the count. Once either limit is hit, **buys** are blocked for the rest of the day and journaled as a `skip` with the limit that stopped them. **Sells are never blocked** — a limit that prevented closing a losing position would do the opposite of what it exists for.

### Market calendar

KRX closures live in `config/krx_holidays.json`; weekends are handled in code. KIS's own holiday API (`CTCA0903R`) is a ledger service rejected on paper accounts (`모의투자 TR 이 아닙니다`), so the file is the paper-mode substitute — switch to the API on a live account.

The 2026 dates up to the file's `verified_through` were derived from real KIS daily bars (every weekday with no bar is a closure); later dates are marked projected and need confirming before they arrive. A date missing from the list is treated as a trading day, so a stale list only wastes a poll rather than silently halting trading.

### Alerts

Set `TELEGRAM_BOT_TOKEN` and `TELEGRAM_CHAT_ID` in `.env` to get a push when a **live** order is placed, fails, or is blocked by a risk limit. Both apps print `alerts=on/off` at startup, and `notify_test` verifies a setup before you depend on it:

```bash
./build/Release/app/notify_test "hello from kairos"
```

`daily_trade` also sends **one summary per run, whether or not anything happened**. Without it, silence means both "held, correctly" and "never ran" — which need opposite reactions from someone not watching the terminal. `--quiet` suppresses it for interactive runs.

Every run also tees its output to `logs/<app>-<KST date>.log`, and `daily_trade` archives the bars each decision used to `data/bars/<ticker>/daily.csv`, so a past decision can be reconstructed after KIS has re-served its history.

### Trade journal

KIS records what was ordered but has no concept of our strategies, so the link from an order back to the profile that produced it only exists if we write it down. Both executables append one JSON line per decision — dry runs and orders suppressed by the cap included — to `data/trades.jsonl` (gitignored; it is account activity).

```bash
# Print KIS's own fill history and append the confirmed fills to the journal
./build/Release/app/kis_order fills                 # today
./build/Release/app/kis_order fills 20260901 20260919
```

Re-running the sync adds nothing, since fills are keyed on order number + filled quantity — but a partial fill that later fills further is recorded again. KIS recommends querying after 15:30 KST; earlier in the session the same-day results may still be incomplete.

---

## 🖥 Running it unattended

systemd **user** units (no root, nothing installed system-wide):

```bash
./scripts/install_systemd.sh           # dashboard + daily timer
./scripts/install_systemd.sh --scalp   # also the intraday loop
./scripts/install_systemd.sh --uninstall
```

| Unit | What it does |
|----|------|
| `kairos-dashboard.service` | Serves the dashboard on :8800, refreshing every 60s |
| `kairos-daily.timer` | Fires `daily_trade --all` at 15:15 on weekdays |
| `kairos-scalp.service` | Runs the scalping loop continuously (it gates itself on market hours) |

They install in **dry-run**: no orders are placed until `--live` is added to the `ExecStart` line. The installer warns if the system timezone is not `Asia/Seoul`, since `OnCalendar` is wall-clock — `15:15` on a UTC host is not 15:15 KST. User services stop at logout unless lingering is enabled (`sudo loginctl enable-linger $USER`), which the installer also checks.

```bash
systemctl --user list-timers 'kairos*'
journalctl --user -u kairos-scalp.service -f
```

What to edit, what needs a restart, and what to check when something looks wrong: [docs/OPERATIONS.md](docs/OPERATIONS.md).

---

## 📊 Technical indicators

All of them are computed from daily OHLCV alone — no order book or tick data required. `app/test_indicators.cpp` smoke-tests all 25.

| Group | Indicator | Function |
|---|---|---|
| Trend | SMA, EMA, WMA | `sma`/`ema`/`wma(prices, window)` |
| Trend | ADX/DMI | `adx(high, low, close, period)` → `{plusDI, minusDI, adx}` |
| Trend | Parabolic SAR | `parabolicSar(high, low, afStep, afMax)` |
| Trend | SuperTrend | `superTrend(high, low, close, period, multiplier)` |
| Trend | Aroon | `aroon(high, low, period)` → `{up, down}` |
| Momentum | RSI | `rsi(prices, period)` |
| Momentum | MACD | `macd(prices, fast, slow, signal)` |
| Momentum | Stochastic | `stochastic(high, low, close, k, d)` |
| Momentum | ROC | `roc(prices, period)` |
| Momentum | CCI | `cci(high, low, close, period)` |
| Momentum | Williams %R | `williamsR(high, low, close, period)` |
| Momentum | TRIX | `trix(prices, period)` |
| Momentum | MA slope | `maSlope(prices, maPeriod, slopeWindow)` (regression slope, % per bar) |
| Volume | VWAP | `vwap(high, low, close, volume)` |
| Volume | OBV | `obv(close, volume)` |
| Volume | MFI | `mfi(high, low, close, volume, period)` |
| Volume | CMF | `cmf(high, low, close, volume, period)` |
| Volume | A/D Line | `adLine(high, low, close, volume)` |
| Volatility | Bollinger Bands | `bollinger(prices, period, stddev)` → `{upper, middle, lower}` |
| Volatility | ATR | `atr(high, low, close, period)` |
| Volatility | Rolling StdDev | `stddev(prices, window)` |
| Volatility | Keltner Channels | `keltner(high, low, close, emaPeriod, atrPeriod, multiplier)` |
| Volatility | Donchian Channels | `donchian(high, low, period)` |

All live in `namespace indicator` (`include/indicator.hpp`), header-only.

---

## 🧩 Adding a strategy

### 1. Create the strategy files

```
lib/my_strategy/
├── my_strategy.hpp
└── my_strategy.cpp
```

### 2. Implement IStrategy

```cpp
#pragma once
#include "strategy/istrategy.hpp"
#include "indicator.hpp"

class MyStrategy : public IStrategy {
public:
    MyStrategy(/* params */);
    [[nodiscard]] std::string name() const override;
    void init(const StockInfo& data) override;
    [[nodiscard]] std::size_t warmupPeriod() const override;
    [[nodiscard]] Signal evaluate(const StockInfo& data, std::size_t index) override;
private:
    // cached indicator values
};
```

Cache indicator arrays in `init()` and map data indices to them in `evaluate()`. Follow the existing
`startIndex_` convention: `evaluate(index)` must only read values derived from data up to `index - 1`,
so a signal never peeks at the bar it trades on.

### 3. Register in CMakeLists.txt

Root `CMakeLists.txt`:
```cmake
add_library(${PROJECT_NAME} SHARED
  # ... existing sources ...
  lib/my_strategy/my_strategy.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE
  # ... existing paths ...
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/my_strategy
)
```

Inside the `BUILD_APP` macro in `app/CMakeLists.txt`:
```cmake
target_include_directories(${APP} PRIVATE
  # ... existing paths ...
  ${CMAKE_SOURCE_DIR}/lib/my_strategy
)
```

### 4. Register in StrategyFactory

Add a branch in `src/strategy/strategy_factory.cpp`:
```cpp
if (type == "my_strategy") {
    return std::make_unique<MyStrategy>(/* params from JSON */);
}
```

### 5. Add a profile to portfolio.json

```json
{
  "id": 19,
  "type": "my_strategy",
  "ticker": "AAPL",
  "category": "swing",
  "params": { /* strategy-specific */ }
}
```

---

## 📊 Paper-trading dashboard (local)

A real-time dashboard for running this machine as a server. It re-runs `portfolio_report` on a timer to
read the KIS paper account and plots the accumulated history as a stock-app style chart.

```bash
./make.sh                                   # builds portfolio_report too
./scripts/dashboard_server.py               # port 8800, refreshes every 60s
./scripts/dashboard_server.py --port 9000 --interval 30
```

Open `http://localhost:8800`. Top section is the paper account (account-value chart with a dashed
principal baseline, 1H/6H/1D/ALL range buttons, hover tooltip), below it the holdings table
(ticker / strategy / average price / current price / P&L % / weight), and below that the existing
macro report.

- Snapshots and rolling history are written to `cache/` (gitignored) and served under `/live/`, so
  account data is never committed to the repository.
- The page re-fetches every 30 seconds on its own.
- To keep it running in the background, start it inside a `tmux` session.
- External network access is deliberately not set up yet — the server has no authentication. See
  Phase 3-D in [docs/PLAN.md](docs/PLAN.md) before exposing it.

---

## 📈 CLI applications

| App | Description |
|----|------|
| `stock` | Yahoo Finance stock data |
| `kis_stock` | KRX stock data via the KIS API |
| `kis_order` | KIS paper/live orders (buy/sell) and balance inquiry |
| `fng` | CNN Fear & Greed Index |
| `fred` | FRED economic series |
| `backtest` | Single-strategy backtest |
| `buy_and_hold` | Buy-and-hold benchmark |
| `macro` | Macro analysis (5-axis score + 4 regimes) |
| `macro_backtest` | Macro-driven portfolio backtest |
| `macro_sweep` | Macro strategy profile comparison |
| `qld_dca_backtest` | QLD dollar-cost-averaging backtest |
| `strategy_sweep` | Multi-strategy × multi-ticker sweep |
| `run_strategy` | Portfolio-driven **backtests** (daily bars; `--start`/`--end` for the window) |
| `daily_trade` | **Live** daily-bar execution, one run per day (KRX, dry-run by default) |
| `scalp_trade` | Intraday scalping via minute-bar polling (KRX, dry-run by default) |
| `portfolio_report` | Paper-account snapshot as JSON (return vs principal + holdings) |
| `notify_test` | Sends one Telegram message to verify alert setup |
| `doctor` | Health check: credentials, account, market data, config, local state |
| `bar_collect` | Seeds the intraday bar archive from Yahoo (KIS serves no history) |
| `scalp_backtest` | Backtests a profile on archived minute bars; `--gross` isolates costs |
| `sweep` | Strategy x ticker cross-test with out-of-sample selection |
| `test_indicators` | Technical indicator smoke tests |

---

## 🗺️ Roadmap

| Phase | Item | Status |
|-------|------|--------|
| **1-A** | GTest test framework | 🔲 not started |
| **1-B** | Realistic backtests (commission/slippage/stop-loss) | ✅ done |
| **1-C** | Additional technical indicators | ✅ done |
| **1-D** | Korean market data collection (KIS API) | ✅ done |
| **2-A** | Broker abstraction (IBroker) | 🔲 not started |
| **2-B** | Order management (OrderManager, RiskManager) | 🔲 not started |
| **2-C** | Automated trading daemon | 🟡 partial — `daily_trade` + `scalp_trade` cover KRX; scheduling and risk limits pending |
| **3** | Dashboard & alerts | 🟡 partial — local dashboard done; Go server, Telegram, external access pending |
| **4** | AI / adaptive strategies (Python ML) | 🔲 not started |
| **5** | Multi-tenant productization | 🔲 not started |

> Full plan: [docs/PLAN.md](docs/PLAN.md)
> Running it day to day: [docs/OPERATIONS.md](docs/OPERATIONS.md)
> Terms used in the output: [docs/GLOSSARY.md](docs/GLOSSARY.md)
> Placing the first real order: [docs/FIRST_LIVE_ORDER.md](docs/FIRST_LIVE_ORDER.md)

---

## 📁 Data flow

```
Yahoo Finance ─┐
FRED API ──────┤                    ┌─── BacktestEngine ──→ BacktestResult
CNN F&G ───────┼→ StockInfo/Macro  ─┤
KIS OpenAPI ───┘                    └─── StrategyFactory ──→ run_strategy (backtest)
                                                                  │
                                                    SignalExecutor ─┴──→ daily_trade / scalp_trade
                                                          │
                                     KisTrader ───────────┴──→ paper account
                                     TradeJournal ────────┴──→ data/trades.jsonl
                                                                         │
                                                  portfolio_report ──────┴──→ dashboard
```

---

## 🔧 Development environment

| Item | Value |
|------|------|
| **C++ standard** | C++17 |
| **Build** | CMake 3.16+ / Ninja |
| **Dependencies** | libcurl, nlohmann-json |
| **Formatter** | clang-format (120 columns, 4-space indent) |
| **CI/CD** | GitHub Actions |

> Note: documentation and commit messages are written in English; the dashboard UI is Korean.

---

## 📄 License

Private project.
