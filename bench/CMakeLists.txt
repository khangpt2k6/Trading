add_executable(tradeflow_bench src/bench_main.cpp)
target_include_directories(tradeflow_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tradeflow_bench PRIVATE tradeflow_engine)

find_package(ZLIB REQUIRED)
add_executable(itch_bench src/itch_bench_main.cpp)
target_include_directories(itch_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(itch_bench PRIVATE tradeflow_engine ZLIB::ZLIB)
