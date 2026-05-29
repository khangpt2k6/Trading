#include <gtest/gtest.h>
#include "tradeflow/matching_engine.hpp"

using namespace tradeflow;

static Order lim(OrderId id, SymbolId sym, Side s, Price p, Qty q) {
  Order o{}; o.id=id; o.symbol=sym; o.side=s; o.type=OrderType::Limit; o.price=p; o.qty=q; return o;
}

TEST(MatchingEngine, ProducesDeltaWithIncrementingSeq) {
  MatchingEngine eng(7);
  BookDelta d1, d2;
  eng.submit(lim(1, 7, Side::Buy, 100, 5), d1);
  eng.submit(lim(2, 7, Side::Sell, 100, 5), d2);
  EXPECT_EQ(d1.symbol, 7u);
  EXPECT_EQ(d1.seq, 1u);
  EXPECT_EQ(d2.seq, 2u);
  EXPECT_EQ(d2.trades.size(), 1u);
}

TEST(MatchingEngine, RoutesByOrderType) {
  MatchingEngine eng(0);
  BookDelta d;
  eng.submit(lim(1, 0, Side::Sell, 100, 5), d);
  d.clear();
  Order m{}; m.id=2; m.symbol=0; m.side=Side::Buy; m.type=OrderType::Market; m.qty=5;
  eng.submit(m, d);
  EXPECT_EQ(d.trades.size(), 1u);
  d.clear();
  Order c{}; c.id=3; c.symbol=0; c.type=OrderType::Cancel;  // nothing to cancel
  eng.submit(c, d);
  EXPECT_TRUE(d.trades.empty());
}
