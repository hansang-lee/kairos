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
      yfinance::yfinance
  )
  target_include_directories(
    ${APP} PRIVATE
      ${CMAKE_SOURCE_DIR}/lib/sma_crossover
      ${CMAKE_SOURCE_DIR}/lib/rsi
  )
  set_target_properties(
    ${APP} PROPERTIES
      OUTPUT_NAME ${NAME}
      DEBUG_POSTFIX d
  )
endmacro()

BUILD_APP(fng)
BUILD_APP(stock)
BUILD_APP(backtest)
BUILD_APP(fred)
BUILD_APP(macro)
BUILD_APP(macro_backtest)
BUILD_APP(macro_sweep)
BUILD_APP(qld_dca_backtest)
BUILD_APP(buy_and_hold)
