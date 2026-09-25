# Backtest results

Everything measured about allocation strategies, with the commands that produced it
and an explicit note on which earlier numbers are void and why.

Read the **Invalidated results** section before quoting anything from a git history
older than `30933e2`. Four separate defects made printed numbers wrong in ways a
reader could not detect, and two of them reversed a conclusion.

- Period: 2016-01-01 to 2026-01-01 unless stated. Ten continuous years.
- Capital: 10,000,000 KRW.
- Costs: 0.0177%/side commission, 0.05% slippage, 0.20% transaction tax on sells,
  and an assumed 0.30%/yr management fee (see **Fees**).
- Rebalancing: every 21 bars unless stated.
- `Sharpe` is annualised from daily returns with no risk-free rate subtracted.
- `MDD` is measured on the mark-to-market equity curve.

---

## 1. The finding that explains the others

The 34-ticker `config/universe_index.json` carries **3.0 independent bets**, not 34.

| Measurement | Value |
|---|---|
| Average pairwise correlation, 34 assets, 2016-2026 | 0.308 |
| Effective independent bets in an equal-weight book | **3.0** |
| Pairs correlating above 0.95 | 23 |
| After removing duplicate listings (24 assets) | **3.6** |

KODEX 200, TIGER 200, KBSTAR 200, ARIRANG 200, KOSEF 200, KODEX 코스피 and KODEX
레버리지 correlate 0.975-0.998 with each other. Eleven sector funds correlate
0.49-0.78 with KOSPI 200 because they are pieces of it. Equal weight over that list
is 59% Korean equity wearing thirty-four labels; the three bond funds get 8.8%.

This is why risk parity could only halve the drawdown, and why momentum's top three
were often three copies of the same fund. `config/universe_core.json` (24 assets)
keeps one ticker per redundant cluster; `config/universe_wide.json` (29) adds five
genuinely different funds.

Reproduce: the correlation scan is not a tool, it reads `cache/daily/*.csv`
directly. The dedup rationale is recorded in each universe file's `dropped` and
`rejected` fields.

---

## 2. Allocation strategies

`portfolio_sweep --universe config/universe_core.json --start 2016-01-01 --end 2026-01-01`

24 assets, 2452 bars, rebalance 21.

| Strategy | return% | CAGR | MDD% | Sharpe | orders | costs | fees |
|---|---|---|---|---|---|---|---|
| Equal-weight buy & hold | 119.7 | 8.2 | -32.6 | 0.71 | 0 | 65,700 | 382,277 |
| Equal weight (rebalanced) | 112.1 | 7.8 | -33.1 | 0.70 | 90 | 84,648 | 386,483 |
| Risk parity (63) | 44.7 | 3.8 | -14.0 | 0.82 | 482 | 148,564 | 327,638 |
| Risk parity (126) | 45.0 | 3.8 | -12.9 | 0.84 | 302 | 92,319 | 302,571 |
| **Group parity (equal in)** | 92.3 | 6.8 | -17.9 | 0.94 | 126 | 81,469 | 371,460 |
| **Group parity (vol-weighted in)** | 88.7 | 6.6 | **-13.4** | **1.11** | 329 | 127,140 | 360,415 |
| Group parity (equal) + abs252 | 51.7 | 4.3 | -9.9 | 0.92 | 307 | 234,055 | 195,377 |
| Group parity (vol) + abs252 | 48.9 | 4.1 | **-8.4** | 1.01 | 429 | 256,382 | 192,892 |
| Equal weight + abs252 | 66.0 | 5.2 | -19.8 | 0.73 | 301 | 252,549 | 196,712 |
| Risk parity (63) + abs252 | 30.3 | 2.7 | -5.5 | 0.99 | 529 | 230,654 | 208,197 |
| Momentum top 1 of 63 | -9.7 | -1.0 | -62.5 | 0.11 | 155 | 1,535,182 | |
| Momentum top 1 of 126 | 471.0 | 19.1 | -45.6 | 0.75 | 103 | 3,690,088 | |
| Momentum top 1 of 252 | 195.6 | 11.5 | -49.4 | 0.53 | 75 | 1,388,578 | |
| Momentum top 2 of 126 | 259.5 | 13.7 | -39.5 | 0.70 | 245 | 2,597,400 | |
| Momentum top 3 of 126 | 159.7 | 10.0 | -35.7 | 0.59 | 299 | 2,056,515 | |
| Momentum top 5 of 126 | 121.8 | 8.3 | -34.6 | 0.58 | 411 | 1,677,788 | |
| Momentum top 5 of 252 | 104.6 | 7.4 | -37.2 | 0.54 | 308 | 1,008,618 | |

Momentum's trading costs run 10-50% of the initial capital. Group parity's run 0.8-1.3%.

**No allocation rule beats buy-and-hold on return.** What they buy is drawdown. That
is the same conclusion single-ticker timing rules reached, reached again one level up.

---

## 3. Which results survive scrutiny

Four tests. A strategy that passes all four is describing something about markets; one
that fails them is describing this particular decade.

### 3.1 Rebalancing interval

`portfolio_sweep --universe config/universe_core.json --rebalance-sweep`

| Interval (bars) | Group parity (vol) return / Sharpe | Risk parity (63) return / Sharpe |
|---|---|---|
| 5 | 91.9 / 1.14 | 55.2 / 0.79 |
| 10 | 89.5 / 1.12 | 56.0 / 0.80 |
| 21 | 88.7 / 1.11 | 55.0 / 0.78 |
| 42 | 90.4 / 1.11 | 56.0 / 0.79 |
| 63 | 84.7 / 1.08 | 58.2 / 0.82 |
| 126 | 92.1 / 1.12 | 63.0 / 0.84 |

Group parity moves 84.7-92.1 across a 25-fold change in interval. On the 34-asset
universe the same axis moved `Momentum top 1 of 126` from **-52.4% to +724.3%** —
that is not a strategy responding to a parameter, it is noise being sampled.

### 3.2 Split period

`portfolio_sweep --start 2016-01-01 --end 2021-01-01` then `--start 2021-01-01 --end 2026-01-01`, `universe_core`.

| Strategy | 2016-2020 Sharpe / MDD | 2021-2025 Sharpe / MDD |
|---|---|---|
| Equal-weight buy & hold | 0.62 / -32.6 | 0.79 / -20.3 |
| Group parity (equal in) | 0.68 / -17.9 | **1.22** / -8.8 |
| Group parity (vol-weighted in) | 0.91 / -13.4 | **1.25** / -8.2 |
| Risk parity (63) | 0.95 / -10.5 | 0.71 / -13.6 |
| Momentum top 1 of 126 | 86.8% return | **10.0% return** |

Rank correlation between the two halves for the twelve momentum variants, measured
earlier on the 34-asset universe: **Spearman rho = -0.566**. The three best in-sample
became three of the four worst out of sample; the in-sample last place came first.

### 3.3 Rolling holding periods

`portfolio_robustness --universe config/universe_core.json --start 2016-01-01 --end 2026-01-01`

Three-year windows stepped three months. Windows starting inside a strategy's warm-up
are excluded, which is why the count differs per row.

| Strategy | windows | worst | median | best | worst MDD | loss% | beat B&H% |
|---|---|---|---|---|---|---|---|
| Equal-weight buy & hold | 28 | -2.7 | 3.8 | 19.5 | -32.6 | 3.6 | — |
| Equal weight (rebalanced) | 27 | -2.4 | 4.0 | 17.3 | -33.1 | 3.7 | 63.0 |
| Risk parity (63) | 26 | -1.5 | 2.8 | 9.9 | -14.0 | 15.4 | 19.2 |
| Group parity (equal in) | 27 | -0.4 | **5.0** | 11.8 | -17.9 | 3.7 | 70.4 |
| **Group parity (vol-weighted in)** | 26 | **+1.2** | 4.6 | 11.8 | -13.4 | **0.0** | **73.1** |
| Risk parity (63) + abs252 | 23 | 0.0 | 1.4 | 5.9 | -5.5 | 0.0 | 13.0 |

Group parity (vol-weighted) **never ended a three-year holding period below where it
started**, across all 26 measured windows, and its worst was +1.2% a year.

Caveat recorded in the tool's own footnote: consecutive windows share 92% of their
bars, so 28 windows carry roughly three windows' worth of independent information.

### 3.4 The taxonomy is a hidden parameter — and it is really the equity share

This is the most important methodological result here.

Ten plausible ways of drawing the asset-class lines, across both universes:

| Taxonomy | classes | non-equity share | GP-equal Sharpe | GP-vol Sharpe |
|---|---|---|---|---|
| core: as chosen | 5 | 60.0% | 0.94 | **1.11** |
| wide: REITs into intl equity | 5 | 60.0% | 0.99 | 1.05 |
| wide: dollar split out | 7 | 57.1% | 0.95 | 1.02 |
| wide: equity/bond/real | 3 | 66.7% | 0.94 | 0.88 |
| core: sectors own class | 6 | 50.0% | 0.81 | 0.94 |
| core: currency into bond | 4 | 50.0% | 0.79 | 0.88 |
| wide: as chosen | 6 | 50.0% | 0.83 | 0.85 |
| core: equity vs rest | 2 | 50.0% | 0.82 | 0.81 |
| wide: sectors own class | 7 | 42.9% | 0.80 | 0.82 |
| wide: all bonds one class | 5 | 40.0% | 0.75 | **0.71** |

**Correlation between non-equity share and Sharpe: +0.89 (equal-in), +0.70 (vol-in).**

Redrawing the taxonomy was never a modelling choice. It was a way of setting the
equity share without admitting to setting it, and the originally reported 1.11 was
one point on a curve, picked by whichever grouping was written first.

`GroupParity` therefore takes the class split as an explicit parameter.

---

## 4. The equity share, asked directly

`portfolio_sweep --universe config/universe_wide.json --start 2016-01-01 --end 2026-01-01 --equity-sweep`

29 assets. Equity sleeve = KR_EQUITY + INTL_EQUITY + REAL_ESTATE.

| Equity share | return% | CAGR | MDD% | Sharpe (equal in) | Sharpe (vol in) |
|---|---|---|---|---|---|
| 0% | 45.3 | 3.8 | -6.5 | 0.82 | 0.94 |
| **20%** | 57.2 | 4.6 | -9.8 | 1.01 | **1.15** |
| 30% | 62.3 | 5.0 | -12.9 | 0.99 | 1.06 |
| 40% | 69.2 | 5.4 | -15.6 | 0.90 | 0.97 |
| 50% | 72.8 | 5.6 | -18.7 | 0.83 | 0.85 |
| 60% | 76.7 | 5.9 | -20.8 | 0.76 | 0.77 |
| 80% | 82.4 | 6.2 | -27.4 | 0.66 | 0.63 |
| 100% | 92.7 | 6.8 | -32.6 | 0.60 | 0.57 |
| Equal-weight buy & hold | 113.4 | 7.9 | -30.4 | — | 0.76 |

Monotonic in both directions, which is what a real relationship looks like. And the
peak holds out of sample at the same parameter value:

| Equity share | 2016-2020 Sharpe | 2021-2025 Sharpe |
|---|---|---|
| 0% | 0.61 | 1.15 |
| **20%** | **0.89** | **1.17** |
| 40% | 0.79 | 0.96 |
| 70% | 0.64 | 0.65 |
| 100% | 0.56 | 0.49 |

**Nothing else measured in this repository has reproduced across a split at the same
parameter value.** This is the one result to build on.

**But the goal is not on this curve.** The highest compound return anywhere on it is
buy-and-hold's 7.9%/yr, and every step toward it costs Sharpe. A target of 15-20%/yr
is not reachable from this universe by allocation.

---

## 5. Leverage: no

`portfolio_sweep --leverage-sweep --margin-rate 0.06 --margin-call 3.5`

6%/yr margin interest, forced sale at 3.5x gross exposure (담보유지비율 140%).

| Leverage | Group parity (vol) return / MDD / Sharpe | interest paid |
|---|---|---|
| 1.0 | 88.7 / -13.4 / **1.11** | 0 |
| 1.5 | 89.1 / -24.4 / 0.76 | 3,415,472 |
| 2.0 | 91.0 / -34.4 / 0.60 | 6,689,297 |
| 2.5 | 86.4 / -44.9 / 0.49 | 9,584,796 |
| 3.0 | 77.9 / -52.4 / 0.41 | 12,003,374 |

Return is flat while drawdown triples. The reason is arithmetic: the strategy
compounds at 6.6%/yr and the loan costs 6%/yr, so the carry is roughly zero and all
that leverage adds is path risk. On a 10M account, 3x pays 12M in interest over the
decade.

Equal weight (CAGR 8.9%, above the rate) is the one case where borrowing adds return —
133.6% to 158.0% at 2.0x — and it pays for it with MDD -38.3% to -69.6% and Sharpe
0.69 to 0.48.

---

## 6. Fees

Modelled as a daily share-count haircut, which is where a fund's fee actually lands
(inside NAV), not as a cash debit. Measured on the 34-asset universe:

| Assumed annual fee | Equal weight return% | Risk parity (63) return% |
|---|---|---|
| 0.00% | 133.6 | 59.4 |
| 0.15% | 130.2 | 57.2 |
| **0.30% (default)** | **126.9** | **55.0** |
| 0.50% | 122.6 | 52.2 |

**Both of the above were later found to be wrong, and are corrected here.**

A listed fund's fee comes out of its net asset value daily, so a price series already
carries it. Charging an expense ratio on top of that was double counting, and the
default is now zero. The field remains for costs a price cannot contain — an advisory
fee, or a synthetic series built from an index, as in the leverage study.

And the distributions were never missing. On 2026-09-24 the KIS series (requested with
`FID_ORG_ADJ_PRC=0`, 수정주가) was checked against Yahoo's dividend-adjusted close for
069500, 148070, 114260, 132030 and 133690 over eleven years:

| Ticker | KIS vs Yahoo adjusted | KIS vs Yahoo raw |
|---|---|---|
| 069500 KODEX 200 | 0.12% | 20.4% |
| 148070 KOSEF 국고채10년 | 0.00% | 19.1% |
| 114260 KODEX 국고채3년 | 1.25% | 7.6% |
| 132030 KODEX 골드선물 | 0.01% | 0.5% |
| 133690 TIGER 미국나스닥100 | 0.12% | 4.0% |

KIS tracks the adjusted series, not the raw one. So KRX figures are total returns, they
are not understated, and they are directly comparable to the dividend-adjusted US ones.
The gold fund makes a useful control: it distributes nothing, and there the raw and
adjusted series agree with each other anyway.

Yahoo's US prices, by contrast, genuinely were unadjusted until the fetch started asking
for `events=div|split`. That mattered most where it was least visible: on price alone TLT
compounded at -0.14% a year and SHY at -0.05%, because a bond fund pays out nearly
everything it earns.

---

## 7. The live strategy, measured the same way

`sweep --strategy aroon_trend --start 2016-01-01 --split 2021-01-01 --end 2026-01-01`
over the three tickers `config/live.json` currently trades (133690, 069500, 132030).

| Parameter set | in-sample median% | out-of-sample median% | B&H median% | MDD% | B&H MDD% | beat B&H |
|---|---|---|---|---|---|---|
| aroon(50,80) | 38.02 | 89.01 | 91.77 | -20.98 | -31.03 | **0 of 3** |
| aroon(20,80) | 75.19 | 71.83 | 91.77 | -18.41 | -31.03 | **0 of 3** |
| **aroon(25,80) — live** | 45.54 | **53.28** | 91.77 | **-17.40** | -31.03 | **0 of 3** |
| aroon(14,80) — best in-sample | 75.57 | 46.42 | 91.77 | -20.62 | -31.03 | **0 of 3** |
| aroon(10,80) | 40.58 | 19.08 | 91.77 | -24.56 | -31.03 | **0 of 3** |

Two things to note. **Not one of the 21 parameter combinations beat buy-and-hold on a
single one of the three tickers out of sample.** And the best in-sample parameter set
(14,80) ranked 13th of 21 out of sample, which is the same selection failure momentum
showed at the portfolio level.

The live configuration cuts drawdown from -31.0% to -17.4% and gives up 38 points of
return to do it. That is a coherent choice; it is not an edge.

---

## 8. Invalidated results

Numbers printed before these commits are void.

| Defect | Effect | Fixed in |
|---|---|---|
| The drift band vetoed position entry, not just drift | With 34 assets, 28 of risk parity's targets fell below the 2%-of-equity band and were **never bought**. A quarter of the account sat in cash for eleven years, and that idle cash *was* the strategy's apparent edge: correcting it moved MDD -10.0% → -15.3% and Sharpe 1.14 → 0.83 | `30933e2` |
| Buy-and-hold stranded a late-listing asset's slice in cash forever | Understated the benchmark by 6.9 points from 2016, 13.5 from 2015 (9 of 34 assets, 26.5% of capital). Every "the strategy beat buy-and-hold" claim was inflated by that | `30933e2` |
| Management fees were not modelled at all | Flattered everything that stayed invested by 4-7 points over the decade | `30933e2` |
| Warm-up reported as performance | A 252-lookback strategy's flat curve counted as a result. `Momentum(3,252)` median window CAGR read 4.7 where 7.3 was correct; warm-up windows had `returnPct == 0` and so were never counted as losses | `30933e2` |
| Calendar years measured from the year's first close | Dropped each year's opening move. 2021 read 3.19 against a true 4.68 | `30933e2` |
| Transient KIS errors cached as "end of history" | `EGW00316` — whose own message asks the caller to retry — truncated six funds by 2 to 9 years each. TIGER 미국채10년선물, listed 2018, arrived as 500 bars starting August 2024 and passed the 300-bar guard | `2a6dbb2` |

Two earlier defects from the previous session, listed because their conclusions are
still cited: Bollinger read the bar it traded (every Bollinger backtest before the fix
was fiction), and the parameter name `std_dev` vs `std_devs` meant a whole grid
returned identical rows.

---

## 9. Where this leaves the goal

The target was ~15-20%/yr with a safe drawdown. Every route checked is closed:

| Route | Result |
|---|---|
| Single-ticker timing rules | Cut drawdown, never beat buy-and-hold on return. 0 of 21 aroon variants beat B&H on any ticker out of sample |
| Cross-sectional momentum | Noise. rho = -0.566 across periods, -52% to +724% across rebalance intervals, costs 10-50% of capital |
| Risk parity | Halves drawdown and halves return. Degrades out of sample (0.95 → 0.71) |
| Group parity / explicit equity share | The one robust finding. Sharpe 1.15 at 20% equity, reproduces across the split. Caps out at 4.6%/yr |
| Leverage | Negative carry: CAGR 6.6% against a 6% loan |
| Wider universe | Five genuine diversifiers added; still only 3.6 effective bets |

Two defensible configurations, and they are ends of one line:

1. **20% equity, group parity within classes** — CAGR 4.6%, MDD -9.8%, Sharpe 1.15.
2. **Equal-weight buy and hold** — CAGR 7.9%, MDD -30.4%, Sharpe 0.76.

The live configuration is neither.

## 10. Open items

- **No live order has ever succeeded.** Zero orders have gone through the real path.
  A backtest is worth nothing if the order never leaves. See `docs/FIRST_LIVE_ORDER.md`.
- `SignalExecutor` still has the tranche-fill defect the backtest engine had: a
  crossover strategy emits BUY once, so three tranches buy one third and stay there.
- Per-ticker `expense_ratio` fields are unfilled; every fee figure is an assumption.
- `--equity-sweep` has not been run through `portfolio_robustness`, so the 20% point
  has no rolling-window statistics yet.


---

## 11. Volatility targeting — the first rule to beat QQQ across the dot-com bust (2026-09-25)

Everything above concluded that no rule beat QQQ on return except leverage, and
that leverage was destroyed in 2000-2002. That conclusion covered moving-average
gates, allocation rules and static leverage. It did not cover **volatility
targeting**, which scales exposure to the last month's realised volatility rather
than switching all-or-nothing on a trend line. Volatility rises before and during
every crash, so exposure falls into them gradually and comes back as it settles.

Rule: exposure = min(cap, target / σ₂₀), in tenth-steps, decided on bar i and
applied to i+1. Exposure above 1x is carried by the reconstructed 2x fund
(financing at bill + 0.4%, 0.95% fee; 0.9951 daily correlation with real QLD).
The unallocated remainder earns nothing — cash yield deliberately left out, so
these rows are understated rather than flattered. Switching costs are zero per the
standing decision to ignore fees; the cost row below shows what that hides.

`app/research/beat_benchmark --start 1999-03-10 --window-years 5 --switch-cost 0`

### Across the whole span, from the top of the bubble

| Strategy (2000-03 ~ 2026-09, 26.5 y) | CAGR | MDD | Sharpe | beat QQQ, 5y windows | 10y windows |
|---|---|---|---|---|---|
| QQQ held | 8.1 | -83.0 | 0.43 | — | — |
| QQQ above ma200, else cash | 7.9 | -57.3 | 0.55 | 27.6% | 10.4% |
| 2x reconstructed, held | 5.5 | -98.8 | 0.37 | 69.0% | 85.1% |
| vol-target 15%, cap 1x | 9.4 | **-45.4** | **0.71** | 29.9% | 13.4% |
| vol-target 20%, cap 1x | 10.7 | -55.4 | 0.68 | 37.9% | 50.7% |
| **vol-target 20%, cap 1.5x** | **12.3** | -55.4 | 0.69 | **80.5%** | **97.0%** |
| vol-target 25%, cap 1.5x | 13.4 | -64.3 | 0.66 | 97.7% | 100% |
| ma200 gate + vol-target 25% 1.5x | 10.8 | **-40.1** | 0.63 | 37.9% | 20.9% |

Note the 2x row: it wins 69% of 5-year windows and still compounds at 5.5%
against QQQ's 8.1%, because the windows it lost, it lost 98.8%. Window win-rate
and total return disagree whenever one event is ruinous. Volatility targeting is
the first row where they agree.

### The tests everything else failed

**Era split.** 2000-2013 held the dot-com bust and 2008; QQQ compounded at
-2.8% with an 83% drawdown. Vol-target 20%/1.5x compounded at +3.8% with -55%,
beating QQQ in 79% of five-year windows. 2013-2026 was the bull decade; QQQ did
20.2%, vol-target 20%/1.5x 21.3% with a drawdown of -26% against -35%. The same
parameters work in both halves, which momentum never managed.

**Through 2008 alone** (2007-06 ~ 2014-06, 3-year windows): QQQ 11.1% / -53.4%;
vol-target 15%/1x matched the return at 11.1% with a drawdown of -28.5%;
vol-target 20%/1.5x did 14.9% / -36.8%, winning 88% of windows.

**The last fifteen years** (2011-09 ~ 2026-09, 5-year windows): QQQ 19.9% / -35.1%
/ 0.99; vol-target 20%/1.5x 21.3% / -26.3% / 1.08, winning 80%; vol-target 20%/1x
18.4% / -25.3% / 1.12 — the "slightly lower, much safer" version.

**Lookback.** The one parameter that is not a policy choice. 10, 20, 40, 60 days
give 12.8, 12.3, 11.7, 11.4% — monotonic, all above QQQ, drawdown -57 to -48. The
result is the rule's, not the window's.

**Cost.** At 0.05% per switch the unbanded rule loses three points a year (12.3 →
9.2) and most of its win rate, because tenth-steps trade almost daily. Trading
only when the target has moved a fifth (`band 0.2`) recovers it: 12.4% at zero
cost, 11.5% at 0.05% a switch, 75% of windows won. **The banded form is the one to
implement.** This is the extreme-turnover case the standing cost decision said to
flag, and it is flagged.

### What is not settled

- Fourteen variants of one family, chosen after seeing 27 years. The family is
  monotonic in every parameter, and volatility targeting is a well-documented
  approach rather than something found by search here, which is a weaker version
  of the multiple-comparisons trap than momentum's — not an absence of it.
- The `rest TLT` variant (17.7% / -35.4% / 0.93 from 2002) cannot be tested
  through the dot-com bust and rides the 2002-2020 bond bull; it survived 2022,
  but is not evidence of the same kind.
- ~~Nothing in the live path can express this rule.~~ The unlevered form now can
  (branch `feat/vol-target`, held back from `main` until the 09-29 first order is
  verified): `IStrategy::targetExposure`, a `vol_target` strategy type, and an
  exposure branch in both `BacktestEngine` and `SignalExecutor` that moves the
  holding toward the target in whole shares only when it has moved by the band.
  The levered leg (QQQ and QLD in a set ratio, for cap 1.5x) is still to do.

### The live path's figures differ from the table above by one day of lag

The research tool sized on bar i's own close and applied that to the next day's
return. The product engine and the trader decide from bars up to i-1 and fill at
bar i's close — the convention every strategy in this project shares, because at
15:15 KST the day's bar is what the order fills against, not what it is decided
from. Re-running the tool's rule on fractional weights under each convention:

| vol-target 15%, cap 1x, 2000-03-08 ~ 2026-09-21 | CAGR | MDD |
|---|---|---|
| research convention (sized on bar i, applied to i+1) | 9.43 | -45.4 |
| product convention (sized on bar i-1, filled at i) | 9.89 | -41.2 |
| product engine, whole shares, daily re-target | 9.89 | -41.2 |
| product engine, whole shares, band 0.2 | 9.38 | -43.7 |

The engine on whole shares lands on the fractional figure to the second decimal,
so the lag is the whole difference and the sizing is right. Pinned by
`tests/test_vol_target.cpp` against the cached QQQ series.
