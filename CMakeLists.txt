cmake_minimum_required(VERSION 3.16)
project(kairos)

if(NOT CMAKE_C_STANDARD)
  set(CMAKE_C_STANDARD 99)
endif()

if(NOT CMAKE_CXX_STANDARD)
  if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 7)
    set(CMAKE_CXX_STANDARD 17)
  else()
    set(CMAKE_CXX_STANDARD 14)
  endif()
  set(CMAKE_CXX_STANDARD_REQUIRED True)
endif()

find_package(CURL REQUIRED)

add_library(${PROJECT_NAME} SHARED
  src/yfinance.cpp
  src/backtest/backtest_engine.cpp
  src/macro/macro_scorer.cpp
  src/macro/macro_backtester.cpp
  src/broker/kis_auth.cpp
  src/broker/kis_trader.cpp
  src/data/kis_provider.cpp
  src/data/krx_calendar.cpp
  src/data/bar_recorder.cpp
  src/data/reference_series.cpp
  src/portfolio/portfolio_data.cpp
  src/portfolio/portfolio_engine.cpp
  src/portfolio/price_cache.cpp
  src/portfolio/strategies.cpp
  src/trade/trade_journal.cpp
  src/trade/signal_executor.cpp
  src/trade/risk_guard.cpp
  src/trade/schedule_state.cpp
  src/trade/position_store.cpp
  src/notify/telegram.cpp
  lib/sma_crossover/sma_crossover.cpp
  lib/rsi/rsi_strategy.cpp
  lib/macd/macd_strategy.cpp
  lib/bollinger/bollinger_strategy.cpp
  lib/stochastic_reversal/stochastic_reversal.cpp
  lib/williams_r/williams_r_strategy.cpp
  lib/cci_reversal/cci_reversal.cpp
  lib/mfi_reversal/mfi_reversal.cpp
  lib/adx_trend/adx_trend.cpp
  lib/supertrend_follow/supertrend_follow.cpp
  lib/aroon_trend/aroon_trend.cpp
  lib/psar_trend/psar_trend.cpp
  lib/donchian_breakout/donchian_breakout.cpp
  lib/obv_trend/obv_trend.cpp
  lib/keltner_breakout/keltner_breakout.cpp
  lib/ma_slope_trend/ma_slope_trend.cpp
  lib/regime_rsi/regime_rsi.cpp
  lib/volume_breakout/volume_breakout.cpp
  lib/squeeze_breakout/squeeze_breakout.cpp
  lib/ichimoku_trend/ichimoku_trend.cpp
  lib/ma_timing/ma_timing.cpp
  lib/absolute_momentum/absolute_momentum.cpp
  lib/dual_momentum/dual_momentum.cpp
  lib/relative_momentum/relative_momentum.cpp
  src/strategy/strategy_factory.cpp
  src/strategy/strategy_catalog.cpp
)

target_include_directories(${PROJECT_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/sma_crossover
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/rsi
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/macd
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/bollinger
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/stochastic_reversal
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/williams_r
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/cci_reversal
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/mfi_reversal
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/adx_trend
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/supertrend_follow
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/aroon_trend
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/psar_trend
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/donchian_breakout
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/obv_trend
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/keltner_breakout
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/ma_slope_trend
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/regime_rsi
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/volume_breakout
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/squeeze_breakout
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/ichimoku_trend
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/ma_timing
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/absolute_momentum
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/dual_momentum
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/relative_momentum
)

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    CURL::libcurl
)

set_target_properties(${PROJECT_NAME}
  PROPERTIES
    OUTPUT_NAME "kairos"
    DEBUG_POSTFIX d
)

add_library(
  kairos::kairos
    ALIAS ${PROJECT_NAME}
)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(IS_TOP_LEVEL True)
endif()

if (IS_TOP_LEVEL)
  add_subdirectory(app)
endif()

# ----- Tests -----
# Built by default so a broken test is a broken build, not something to remember
# to run. Uses a small in-tree framework rather than GTest: the project's only
# dependencies are libcurl and nlohmann/json, and a test harness is not worth a
# third.
enable_testing()

add_executable(kairos_tests
  tests/main.cpp
  tests/test_indicator.cpp
  tests/test_backtest_engine.cpp
  tests/test_strategy_lookahead.cpp
  tests/test_trade.cpp
  tests/test_data_config.cpp
  tests/test_integration.cpp
  tests/test_portfolio.cpp
  tests/test_portfolio_leverage.cpp
  tests/test_portfolio_allocation.cpp
)
target_link_libraries(kairos_tests PRIVATE ${PROJECT_NAME})
target_include_directories(kairos_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/tests
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
set_target_properties(kairos_tests PROPERTIES OUTPUT_NAME "kairos_tests")

add_test(NAME unit COMMAND kairos_tests)
