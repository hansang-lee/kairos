# Glossary

Every term this project's output actually prints, with a worked example. The
numbers are small and made up so the arithmetic can be followed by hand.

Assume a starting account of **10,000,000 KRW** throughout.

---

## Performance

### Total return (총수익률)

What the account ended with, against what it started with.

```
start  10,000,000
end    11,500,000
return = (11,500,000 - 10,000,000) / 10,000,000 = +15.0%
```

Says nothing about how long it took or how unpleasant the ride was, which is why
it is never read alone.

### CAGR (연평균 성장률)

Total return expressed as the steady annual rate that would have produced it.
Makes runs of different lengths comparable.

```
+15% over 3 years
CAGR = (1.15)^(1/3) - 1 = 4.77% per year
```

+15% over three years and +15% over three months are the same total return and
very different results; CAGR is what separates them.

### MDD — maximum drawdown (최대낙폭)

The worst peak-to-trough fall in account value over the period. Always negative.

```
account: 10,000,000 → 13,000,000 → 9,100,000 → 12,000,000
peak 13,000,000, trough after it 9,100,000
MDD = (9,100,000 - 13,000,000) / 13,000,000 = -30.0%
```

Measured from the **peak**, not from the start — so a strategy can finish up
+20% and still have had a -30% MDD along the way.

This is usually the number that decides whether a strategy is livable. -30% on
10,000,000 means watching 3,900,000 disappear and holding on. Most people stop a
strategy at its worst moment, which converts a drawdown into a permanent loss,
so a strategy you will actually stick with beats a better one you will not.

### Win rate (승률)

Share of closed trades that made money.

```
20 trades, 12 profitable
win rate = 12 / 20 = 60%
```

Misleading on its own. Ten wins of +1% and one loss of -20% is a 91% win rate
and a losing strategy.

### Profit factor

Total gains divided by total losses. Above 1.0 makes money; below 1.0 loses it.

```
winning trades sum to +3,000,000
losing  trades sum to -2,000,000
profit factor = 3,000,000 / 2,000,000 = 1.5
```

Fixes what win rate misses: the 91%-win-rate example above has a profit factor
of 10 × 1 / 20 = 0.5, and the number says plainly that it loses.

### Sharpe ratio

Return per unit of volatility — how much bumpiness was endured for the result.
Higher is better; above 1 is good, below 0.5 is weak.

```
average daily return   0.05%
daily standard deviation 1.0%
Sharpe = 0.05 / 1.0 × sqrt(252) ≈ 0.79
```

The `sqrt(252)` annualizes from daily bars (roughly 252 trading days a year).
**This project hardcodes that 252**, so Sharpe is only meaningful for daily-bar
backtests. `scalp_backtest` omits it for exactly this reason.

### Composite score

This project's own 0–100 blend, weighting total return 35%, MDD 30%, Sharpe 20%
and win rate 15%. A convenience for ranking, not a standard measure — prefer
reading the components.

---

## Costs

### Commission (수수료)

The broker's fee, charged on both the buy and the sell. KIS online: about
**0.0177%** per side domestically, **0.25%** per side for US stocks.

### Transaction tax (증권거래세)

A Korean tax charged **on sells only**, whether or not the trade made money.
**0.20%** since 2026-01-01 (KOSPI 0.05% + 농특세 0.15%; KOSDAQ 0.20%).

### Slippage (슬리피지)

The gap between the price you decided at and the price you actually got. A
market order takes whatever is on the book, which is rarely the last printed
price.

### Round trip (왕복 비용)

Everything one complete buy-and-sell costs. This is the number that decides
whether frequent trading can work at all.

```
KRX: 0.0177% × 2 (commission) + 0.20% (tax) + 0.05% × 2 (slippage) ≈ 0.335%
```

On a 71,500 KRW share that is about **240 KRW per round trip**. A strategy
trading 30 times a day at 20% of the account pays roughly 2% of the account per
day in costs alone — which is why scalping was shelved here.

### Gross vs net

**Gross** is the strategy's raw result with costs zeroed; **net** is what the
account actually sees. Comparing them separates "no edge" from "an edge too
small to pay for", which are different problems.

```
gross  -0.97%   ← the strategy itself barely loses
net   -10.67%   ← costs turn it into a rout
```

That is a real measurement from this project's 1-minute scalping test.
`scalp_backtest --gross` produces it.

---

## Position and risk

### Position sizing / `position_pct`

The fraction of available cash committed to one position.

```
cash 10,000,000, position_pct 0.2, price 71,500
allocated = 2,000,000 → 27 shares
```

The single most effective risk control here: the same strategy at
`position_pct` 1.0 ran a -42% drawdown where 0.2 ran -8%.

### Stop loss (손절)

Exit when price falls a set amount below the **average purchase price**.

```
bought at 100,000, stop_loss_pct 8.0
exit triggers at or below 92,000
```

### Take profit (익절)

The mirror image: exit when price rises a set amount above the average price.

```
bought at 100,000, take_profit_pct 12.0
exit triggers at or above 112,000
```

### Trailing stop (트레일링 스탑)

Exit a set amount below the **highest price seen since entry**, not below the
purchase price. It protects a gain already made.

```
bought at 100,000, trailing_stop_pct 5.0
price rises to 130,000  → peak is now 130,000, exit level 123,500
price falls to 123,000  → exit, locking in about +23%
```

A plain stop loss would still be sitting at 92,000 here, giving the whole gain
back. This is why the peak has to be remembered across restarts.

### Tranches (분할 매수/매도)

Entering or exiting over several orders instead of one, so a single bad price
does not set the whole position.

```
entry_tranches 3, target 3,000,000
→ roughly 1,000,000 per buy signal, three signals to be fully in
```

Forced exits (stop, trailing, take-profit) always sell everything at once —
scaling out of a stop defeats having one.

### Cooldown (재진입 대기)

A refusal to re-enter the same ticker for a set time after exiting it. Stops a
strategy from churning in a sideways market, where signals flip repeatedly and
each flip costs a round trip.

### Daily loss limit (일일 손실 한도)

Stops **new buying** once the account falls a set amount below where the day
opened. Selling is never blocked — a limit that trapped you in a losing position
would do the opposite of its job.

```
day opened at 10,000,000, limit 3%
equity reaches 9,650,000 (-3.5%) → no more buys today
```

---

## Testing a strategy

### Backtest (백테스팅)

Running a strategy over historical prices to see what it would have done.
Cheap, and easy to fool yourself with — everything below is a way it lies.

### Buy and hold (단순 보유)

Buy at the start, sell at the end, do nothing in between. The benchmark every
strategy has to clear to justify its complexity. Frequently it does not: over
2025–2026 in this project's sweep, **no strategy beat buy-and-hold on a majority
of the universe**.

### In-sample / out-of-sample (표본 내 / 표본 외)

**In-sample** is the period used to choose a strategy. **Out-of-sample** is a
period held back and never looked at until the choice was made. Only the
out-of-sample result is evidence, because the in-sample one was selected for.

```
2021-2024  choose the strategy here
2025-2026  report this, untouched
```

### Overfitting (과최적화)

Choosing something that fits the past in detail and the future not at all.

The trap is arithmetic: testing 20 strategies against 30 tickers gives 600
results, and the best of 600 looks excellent even if all 600 are coin flips.
Guarding against it means selecting out-of-sample, ranking by the **median**
across tickers rather than the maximum, and distrusting any result resting on
one ticker.

### Look-ahead bias (미래 참조)

Using information that was not available yet — reading today's close to decide
today's trade. It makes a backtest look wonderful and cannot be reproduced live.

This project found it in its own Bollinger and MACD strategies, where the code
read the bar it was about to trade at. Every result from those strategies before
the fix was fiction. The test suite now checks for it by running each strategy
on the full series and again on the series cut off at the bar being decided, and
requiring the same answer.

### Survivorship bias (생존 편향)

Testing only on companies that still exist. The ones that collapsed are missing
from the list, so results come out better than what was actually achievable.
This project's 30-ticker universe has this problem and says so in the file.

### Regime (국면)

The market's character over a period — rising, falling, or going sideways. It
dominates results, so a strategy chosen in one regime is chosen *for* that
regime. Measured here: over 2025–2026 almost nothing beat buy-and-hold; over the
2022 decline almost everything did. Same strategies, opposite verdicts.

### Warmup period (워밍업)

Bars a strategy needs before it can say anything. A 40-day moving average has
nothing to report until day 40.

---

## Signals

### Golden cross / death cross (골든크로스 / 데드크로스)

A short moving average crossing **above** a long one is a golden cross, read as
an uptrend beginning; crossing below is a death cross.

```
SMA(3) rises past SMA(8)  → BUY
SMA(3) falls below SMA(8) → SELL
```

### Bollinger Bands (볼린저 밴드)

A moving average with bands drawn a number of standard deviations above and
below it. Price at the lower band is unusually cheap against its own recent
range, at the upper band unusually expensive.

```
period 40, std_devs 2.0
middle = 40-day average
upper  = middle + 2 × (40-day standard deviation)
lower   = middle - 2 × (40-day standard deviation)
```

The strategy this project currently runs live: buy a bounce off the lower band,
sell at the upper.

### RSI (상대강도지수)

0–100, measuring how one-sided recent moves have been. Below 30 is conventionally
"oversold", above 70 "overbought".

### MDD vs volatility

Two different discomforts. Volatility is how much it moves day to day; MDD is
the worst cumulative hole it dug. A strategy can be calm daily and still have a
deep drawdown if it declines steadily.
