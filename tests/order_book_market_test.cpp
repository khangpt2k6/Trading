#include <gtest/gtest.h>
#include <vector>
#include "tradeflow/order_book.hpp"

using namespace tradeflow;

static Order lim(OrderId id, Side s, Price p, Qty q) {
  Order o{}; o.id=id; o.side=s; o.type=OrderType::Limit; o.price=p; o.qty=q; return o;
}
static Order mkt(OrderId id, Side s, Qty q) {
  Order o{}; o.id=id; o.side=s; o.type=OrderType::Market; o.qty=q; return o;
}

TEST(OrderBookMarket, SweepsMultipleLevels) {
  OrderBook ob;
  std::vector<Trade> tr; std::vector<LevelUpdate> ch;
  ob.add_limit(lim(1, Side::Sell, 100, 5), tr, ch);
  ob.add_limit(lim(2, Side::Sell, 101, 5), tr, ch);
  tr.clear(); ch.clear();
  ob.add_market(mkt(3, Side::Buy, 8), tr, ch);  // takes 5@100 then 3@101
  ASSERT_EQ(tr.size(), 2u);
  EXPECT_EQ(tr[0].price, 100); EXPECT_EQ(tr[0].qty, 5);
  EXPECT_EQ(tr[1].price, 101); EXPECT_EQ(tr[1].qty, 3);
  EXPECT_EQ(ob.best_ask(), 101);  // 2 remaining at 101
}

TEST(OrderBookMarket, DiscardsRemainderWhenBookEmpty) {
  OrderBook ob;
  std::vector<Trade> tr; std::vector<LevelUpdate> ch;
  ob.add_limit(lim(1, Side::Sell, 100, 3), tr, ch);
  tr.clear(); ch.clear();
  ob.add_market(mkt(2, Side::Buy, 10), tr, ch);  // only 3 available
  ASSERT_EQ(tr.size(), 1u);
  EXPECT_EQ(tr[0].qty, 3);
  EXPECT_EQ(ob.best_ask(), 0);   // book emptied
  EXPECT_EQ(ob.best_bid(), 0);   // market remainder NOT rested
}
