get_filename_component(DIRECTORY_PATH ${CMAKE_CURRENT_LIST_DIR} ABSOLUTE)
string(REPLACE "/" "_" DIRECTORY_NAME ${DIRECTORY_PATH})
macro(BUILD_APP "NAME")
  set("APP" ${DIRECTORY_NAME}_${NAME})
  add_executable(
    ${APP}
      ${NAME}.cpp
  )
  target_link_libraries(
    ${APP} PRIVATE
      kairos::kairos
  )
  target_compile_options(${APP} PRIVATE ${KAIROS_WARNINGS})
  target_include_directories(
    ${APP} PRIVATE
      ${CMAKE_SOURCE_DIR}/lib/sma_crossover
      ${CMAKE_SOURCE_DIR}/lib/rsi
      ${CMAKE_SOURCE_DIR}/lib/macd
      ${CMAKE_SOURCE_DIR}/lib/bollinger
      ${CMAKE_SOURCE_DIR}/lib/stochastic_reversal
      ${CMAKE_SOURCE_DIR}/lib/williams_r
      ${CMAKE_SOURCE_DIR}/lib/cci_reversal
      ${CMAKE_SOURCE_DIR}/lib/mfi_reversal
      ${CMAKE_SOURCE_DIR}/lib/adx_trend
      ${CMAKE_SOURCE_DIR}/lib/supertrend_follow
      ${CMAKE_SOURCE_DIR}/lib/aroon_trend
      ${CMAKE_SOURCE_DIR}/lib/psar_trend
      ${CMAKE_SOURCE_DIR}/lib/donchian_breakout
      ${CMAKE_SOURCE_DIR}/lib/obv_trend
      ${CMAKE_SOURCE_DIR}/lib/keltner_breakout
      ${CMAKE_SOURCE_DIR}/lib/ma_slope_trend
      ${CMAKE_SOURCE_DIR}/lib/regime_rsi
      ${CMAKE_SOURCE_DIR}/lib/volume_breakout
      ${CMAKE_SOURCE_DIR}/lib/squeeze_breakout
      ${CMAKE_SOURCE_DIR}/lib/ichimoku_trend
      ${CMAKE_SOURCE_DIR}/lib/ma_timing
      ${CMAKE_SOURCE_DIR}/lib/absolute_momentum
      ${CMAKE_SOURCE_DIR}/lib/dual_momentum
      ${CMAKE_SOURCE_DIR}/lib/relative_momentum
  )
  set_target_properties(
    ${APP} PROPERTIES
      OUTPUT_NAME ${NAME}
      DEBUG_POSTFIX d
  )
endmacro()

BUILD_APP(macro)
BUILD_APP(kis_order)
BUILD_APP(trader)
BUILD_APP(notify_test)
BUILD_APP(bot)
BUILD_APP(doctor)
BUILD_APP(bar_collect)
BUILD_APP(portfolio_report)
BUILD_APP(fetch_universe)

# Research tools live one directory down, so a listing of app/ answers "what runs
# the account" without a reader having to know which twenty of twenty-eight do not.
add_subdirectory(research)
