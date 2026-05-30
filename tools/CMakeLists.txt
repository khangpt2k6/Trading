find_package(ZLIB REQUIRED)

add_executable(itch_inspect src/itch_inspect.cpp)
target_link_libraries(itch_inspect PRIVATE tradeflow_engine ZLIB::ZLIB)
