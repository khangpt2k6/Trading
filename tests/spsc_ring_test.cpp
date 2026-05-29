#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "tradeflow/spsc_ring.hpp"

using tradeflow::SpscRing;

TEST(SpscRing, PushPopSingle) {
  SpscRing<int> q(4);
  int out = -1;
  EXPECT_FALSE(q.pop(out));        // empty
  EXPECT_TRUE(q.push(42));
  EXPECT_TRUE(q.pop(out));
  EXPECT_EQ(out, 42);
  EXPECT_FALSE(q.pop(out));        // empty again
}

TEST(SpscRing, FullRejectsPush) {
  SpscRing<int> q(2);              // capacity 2
  EXPECT_TRUE(q.push(1));
  EXPECT_TRUE(q.push(2));
  EXPECT_FALSE(q.push(3));         // full
  int out = 0;
  EXPECT_TRUE(q.pop(out));
  EXPECT_EQ(out, 1);
  EXPECT_TRUE(q.push(3));          // room now
}

TEST(SpscRing, FifoOrderUnderConcurrency) {
  constexpr int N = 1'000'000;
  SpscRing<int> q(1024);
  std::vector<int> got;
  got.reserve(N);
  std::thread consumer([&] {
    int v, count = 0;
    while (count < N) {
      if (q.pop(v)) { got.push_back(v); ++count; }
    }
  });
  for (int i = 0; i < N; ++i) {
    while (!q.push(i)) { /* spin */ }
  }
  consumer.join();
  ASSERT_EQ(static_cast<int>(got.size()), N);
  for (int i = 0; i < N; ++i) ASSERT_EQ(got[i], i);  // FIFO preserved
}
