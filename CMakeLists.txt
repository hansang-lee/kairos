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
  lib/sma_crossover/sma_crossover.cpp
  lib/rsi/rsi_strategy.cpp
  lib/macd/macd_strategy.cpp
  lib/bollinger/bollinger_strategy.cpp
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
