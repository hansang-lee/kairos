# kairos — agent guide

C++17 quant trading framework: backtests, a daily trading daemon on the KIS paper
account, a Telegram bot. Read `docs/STATUS.md` first; it says what exists, what is
in flight, what has been decided, and what comes next. Then `docs/OPERATIONS.md`
for how to run things.

## Build and test

```zsh
cmake -S . -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build/Release -j
./build/Release/kairos_tests
git ls-files '*.cpp' '*.hpp' | xargs clang-format --dry-run --Werror
```

CI runs exactly this on every push. Tests must not touch the network. A test that
needs `cache/daily/*.csv` skips when the file is absent.

## Conventions

- `evaluate(data, i)` reads bars up to `i-1` and fills at bar `i`. Every strategy,
  the engine and the executor share this; `tests/test_strategy_lookahead.cpp`
  enforces it for signals and exposures.
- Paths resolve from the executable (`util::resolveFromExe`), never from the
  working directory. Clock reads go through `common/kst_time.hpp`.
- Strategy parameters are read through `ParamReader`; an unknown key is a warning
  and a test failure.
- `app/` is product, `app/research/` is research. Research results go in
  `docs/BACKTEST_RESULTS.md`, not in new files.
- Comments say why, and what went wrong before. Keep that.
- Docs are in English. The user reads Korean; reply in Korean, write docs in English.

## Safety

- The trader is dry-run unless `--live` is passed. Never add `--live` yourself;
  live orders need the user's explicit confirmation.
- Never read out or paste credentials. Never commit `.env`, `data/`, or
  `cache/kis_token_*`.
- The dashboard binds `127.0.0.1`; do not widen it without authentication.
- Commit and push finished work without asking. Never force-push, rebase shared
  history or reset without explicit confirmation.
- Standing decisions (fees and FX ignored, whole shares, KIS paper now and Meritz
  later) are listed in `docs/STATUS.md` §4. Do not re-open them without evidence.
