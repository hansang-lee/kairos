cmake_minimum_required(VERSION 3.16)
project(libyfinance)

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
  src/trade/trade_journal.cpp
  src/trade/signal_executor.cpp
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
  src/strategy/strategy_factory.cpp
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
)

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    CURL::libcurl
)

set_target_properties(${PROJECT_NAME}
  PROPERTIES
    OUTPUT_NAME "yfinance"
    DEBUG_POSTFIX d
)

add_library(
  yfinance::yfinance
    ALIAS ${PROJECT_NAME}
)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(IS_TOP_LEVEL True)
endif()

if (IS_TOP_LEVEL)
  add_subdirectory(app)
endif()
