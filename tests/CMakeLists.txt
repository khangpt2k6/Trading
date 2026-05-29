include(GoogleTest)

function(tf_add_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE tradeflow_engine GTest::gtest_main)
  gtest_discover_tests(${name})
endfunction()

tf_add_test(sanity_test sanity_test.cpp)
tf_add_test(spsc_ring_test spsc_ring_test.cpp)
