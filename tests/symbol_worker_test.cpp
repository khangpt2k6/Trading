#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include "tradeflow/symbol_worker.hpp"

using namespace tradeflow;

TEST(SymbolWorker, ProcessesSubmittedOrders) {
  std::atomic<long> matched_qty{0};
  SymbolWorker w(0, 1024, [&](const Order&, const BookDelta& d) {
    for (auto& t : d.trades) matched_qty.fetch_add(t.qty, std::memory_order_relaxed);
  });
  w.start();

  Order s{}; s.id=1; s.symbol=0; s.side=Side::Sell; s.type=OrderType::Limit; s.price=100; s.qty=10;
  Order b{}; b.id=2; b.symbol=0; b.side=Side::Buy;  b.type=OrderType::Limit; b.price=100; b.qty=10;
  while (!w.submit(s)) {}
  while (!w.submit(b)) {}

  w.drain_and_stop();
  EXPECT_EQ(matched_qty.load(), 10);
  EXPECT_EQ(w.processed(), 2u);
}
