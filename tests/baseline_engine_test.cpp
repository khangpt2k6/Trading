#include <gtest/gtest.h>
#include <atomic>
#include "tradeflow/baseline_engine.hpp"

using namespace tradeflow;

TEST(BaselineEngine, ProcessesAcrossSymbolsWithSingleLock) {
  std::atomic<long> matched{0};
  BaselineEngine eng(/*num_symbols=*/2, [&](const Order&, const BookDelta& d) {
    for (auto& t : d.trades) matched.fetch_add(t.qty, std::memory_order_relaxed);
  });
  eng.start();

  auto lim = [](OrderId id, SymbolId sym, Side s, Price p, Qty q) {
    Order o{}; o.id=id; o.symbol=sym; o.side=s; o.type=OrderType::Limit; o.price=p; o.qty=q; return o;
  };
  eng.submit(lim(1, 0, Side::Sell, 100, 5));
  eng.submit(lim(2, 0, Side::Buy, 100, 5));
  eng.submit(lim(3, 1, Side::Sell, 50, 7));
  eng.submit(lim(4, 1, Side::Buy, 50, 7));

  eng.drain_and_stop();
  EXPECT_EQ(matched.load(), 12);   // 5 + 7
}
