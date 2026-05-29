#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include "tradeflow/mpsc_ring.hpp"

using tradeflow::MpscRing;

TEST(MpscRing, PushPopSingle) {
  MpscRing<int> q(4);
  int out = -1;
  EXPECT_FALSE(q.pop(out));
  EXPECT_TRUE(q.push(7));
  EXPECT_TRUE(q.pop(out));
  EXPECT_EQ(out, 7);
}

TEST(MpscRing, FullRejects) {
  MpscRing<int> q(2);
  EXPECT_TRUE(q.push(1));
  EXPECT_TRUE(q.push(2));
  EXPECT_FALSE(q.push(3));   // full
}

TEST(MpscRing, MultiProducerNoLossNoDup) {
  constexpr int kProducers = 4;
  constexpr int kPerProducer = 250'000;
  constexpr int kTotal = kProducers * kPerProducer;
  MpscRing<long> q(4096);

  std::vector<int> seen(kTotal, 0);
  std::atomic<int> consumed{0};

  std::thread consumer([&] {
    long v;
    while (consumed.load(std::memory_order_relaxed) < kTotal) {
      if (q.pop(v)) {
        ++seen[static_cast<std::size_t>(v)];
        consumed.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });

  std::vector<std::thread> producers;
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&, p] {
      const long base = static_cast<long>(p) * kPerProducer;
      for (int i = 0; i < kPerProducer; ++i) {
        while (!q.push(base + i)) { /* spin until room */ }
      }
    });
  }
  for (auto& t : producers) t.join();
  consumer.join();

  EXPECT_EQ(consumed.load(), kTotal);
  for (int i = 0; i < kTotal; ++i) {
    ASSERT_EQ(seen[i], 1) << "value " << i << " seen " << seen[i] << " times";
  }
}
