add_library(tradeflow_engine INTERFACE)
target_include_directories(tradeflow_engine INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(tradeflow_engine INTERFACE Threads::Threads)
target_compile_features(tradeflow_engine INTERFACE cxx_std_17)
