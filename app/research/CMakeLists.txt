# Backtests, sweeps and studies. Nothing here places an order or is started by a
# systemd unit; the macro report runner is the one research-shaped tool that CI
# calls daily, and it stays in app/ for that reason.
#
# BUILD_APP is the parent directory's macro. DIRECTORY_NAME is re-derived here so
# target names carry this directory rather than the parent's.
get_filename_component(DIRECTORY_PATH ${CMAKE_CURRENT_LIST_DIR} ABSOLUTE)
string(REPLACE "/" "_" DIRECTORY_NAME ${DIRECTORY_PATH})

BUILD_APP(backtest)
BUILD_APP(run_strategy)
BUILD_APP(strategy_sweep)
BUILD_APP(sweep)
BUILD_APP(portfolio_sweep)
BUILD_APP(portfolio_robustness)
BUILD_APP(beat_benchmark)
BUILD_APP(leverage_study)
BUILD_APP(dca_backtest)
BUILD_APP(scalp_backtest)
BUILD_APP(buy_and_hold)
BUILD_APP(macro_backtest)
BUILD_APP(macro_sweep)
BUILD_APP(test_indicators)
BUILD_APP(stock)
BUILD_APP(kis_stock)
BUILD_APP(fng)
BUILD_APP(fred)
