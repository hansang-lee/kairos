# First live order — verification runbook

Not one order has ever been placed successfully. Everything in this repo is
verified up to the point of sending, and no further: the order path has been
exercised only in dry-run, and `KisTrader::getDailyFills()` has only ever been
run against an account with zero orders, so its record parsing is still
unproven.

This is the session that changes that. Work through it in order, on a paper
account, during KRX hours. Stop at the first step that does not do what it says.

**Next trading day: Monday 2026-09-21, 09:00–15:30 KST.**

---

## Before the open

```bash
cd /home/hslee/workspace/kairos
cmake --build build/Release          # tests build too; a failure here stops everything
./build/Release/kairos_tests
./build/Release/app/doctor
```

`doctor` must report **0 failures**. One warning about Telegram is expected and
fine — alerts are unconfigured.

Confirm the account is the paper one before anything else:

```bash
./build/Release/app/doctor | grep -A2 "KIS credentials"
```

It must say `mode: paper (모의투자)`. If it says LIVE, stop and fix `KIS_MODE`
in `.env`.

---

## Step 1 — Buy one share by hand

The smallest possible real order, placed manually rather than by a strategy, so
that a failure means the order API and nothing else.

```bash
./build/Release/app/kis_order buy 005930 1
```

Expected:

```
[*] BUY 005930 x1 (market)
[+] Order accepted. No: 0000123456 at 093015
```

**If it fails**, the message comes from KIS and names the reason. Common ones:

| Message contains | Meaning |
|---|---|
| `장시작전` / `장종료` | Outside 09:00–15:30; the order cannot be accepted yet |
| `주문가능금액` | Not enough cash in the paper account |
| `모의투자` | The TR is not supported on paper accounts |
| `초당 거래건수` | Rate limited — wait a few seconds and retry |

---

## Step 2 — Confirm the broker agrees

```bash
./build/Release/app/kis_order balance
```

005930 must appear with quantity 1. If the order was accepted but nothing is
held, it was accepted and not filled — check the fill history in step 3 before
assuming anything is wrong.

---

## Step 3 — Confirm the fill history parses

This is the step that has never run against real data. `getDailyFills()` was
written from the KIS spec and verified only to the extent that the request is
accepted and an empty result is handled.

```bash
./build/Release/app/kis_order fills
```

Expected: one row for the order, with a real average price and filled quantity,
followed by `[+] Journal: 1 new fill record(s)`.

**Check each column against what you expect.** Wrong field names would show up
as zeros or blanks rather than as an error — `avg_prvs` (average fill price) and
`tot_ccld_qty` (filled quantity) are the two worth reading carefully.

Then confirm re-running adds nothing:

```bash
./build/Release/app/kis_order fills
```

The second run must say `0 new fill record(s)`.

---

## Step 4 — Confirm the journal recorded it

```bash
tail -3 data/trades.jsonl
```

There should be two lines for this order: the `"event":"order"` written when it
was sent, and the `"event":"fill"` written by the sync. The order line carries
`"reason":"manual"` because it did not come from a strategy.

---

## Step 5 — Sell it back

```bash
./build/Release/app/kis_order sell 005930 1
./build/Release/app/kis_order balance     # holding should be gone
./build/Release/app/kis_order fills       # both sides should now appear
```

A completed round trip means the order path works in both directions. Sells also
carry the transaction tax, so the cash returned will be slightly less than the
purchase cost even at an unchanged price — that is correct, not a bug.

---

## Step 6 — Watch the strategies decide, without trading

```bash
./build/Release/app/daily_trade --all
```

This runs the five live profiles against real intraday prices and prints what
each would do. It places nothing, because `--live` is absent.

Read the output for:

- `bars=` — several hundred, not a handful
- `close=` — a price that matches reality
- `(today, still forming)` — KIS has published today's bar
- `signal=` — most days this is HOLD for every profile; that is normal for a
  strategy that trades roughly 20 times in five years

---

## Step 7 — One real strategy order

Only after steps 1–6 have all done what they say.

```bash
./build/Release/app/daily_trade --id 30 --live
```

If the signal is HOLD, nothing is placed and there is nothing to verify — that
is the likely outcome on any given day. To exercise the path on a day with no
signal, place the manual order from step 1 instead; it uses the same
`KisTrader::placeOrder`. What step 7 adds over step 1 is only the strategy
attribution in the journal, which the dry run already demonstrates.

---

## After it works

Only then consider running unattended:

```bash
./scripts/install_systemd.sh          # installs in dry-run
sudo loginctl enable-linger $USER     # or services stop at logout
```

and, separately and deliberately, adding `--live` to the units:

```bash
systemctl --user edit --full kairos-daily.service   # append --live to ExecStart
systemctl --user daemon-reload
```

Leave `kairos-scalp` alone. Scalping is on hold: measured against five days of
real 1-minute bars it lost 10.67% net where it lost 0.97% gross, meaning the
whole loss was trading costs, and the strategy had no edge to pay them with.

---

## If something goes wrong

```bash
./build/Release/app/doctor                       # start here
journalctl --user -u kairos-daily --since today  # if running under systemd
tail -20 data/trades.jsonl                       # what was decided, and why
cat data/risk_state.json                         # is the day's limit blocking?
```

The journal records blocked orders as `"event":"skip"` with the limit that
stopped them, so an order that silently did not happen still left a reason
behind.
