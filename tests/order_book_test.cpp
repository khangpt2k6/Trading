#include <gtest/gtest.h>
#include <vector>
#include "tradeflow/order_book.hpp"

using namespace tradeflow;

static Order limit(OrderId id, Side s, Price p, Qty q) {
  Order o{}; o.id = id; o.side = s; o.type = OrderType::Limit; o.price = p; o.qty = q;
  return o;
}

TEST(OrderBook, RestingNoCrossUpdatesLevels) {
  OrderBook ob;
  std::vector<Trade> trades; std::vector<LevelUpdate> ch;
  ob.add_limit(limit(1, Side::Buy, 100, 10), trades, ch);
  EXPECT_TRUE(trades.empty());
  ASSERT_EQ(ch.size(), 1u);
  EXPECT_EQ(ch[0].side, Side::Buy);
  EXPECT_EQ(ch[0].price, 100);
  EXPECT_EQ(ch[0].qty, 10);
  EXPECT_EQ(ob.best_bid(), 100);
}

TEST(OrderBook, FullCrossGeneratesTradeAndRemovesLevel) {
  OrderBook ob;
  std::vector<Trade> trades; std::vector<LevelUpdate> ch;
  ob.add_limit(limit(1, Side::Sell, 100, 10), trades, ch);  // rest ask
  trades.clear(); ch.clear();
  ob.add_limit(limit(2, Side::Buy, 100, 10), trades, ch);   // crosses fully
  ASSERT_EQ(trades.size(), 1u);
  EXPECT_EQ(trades[0].price, 100);
  EXPECT_EQ(trades[0].qty, 10);
  EXPECT_EQ(trades[0].taker, 2u);
  EXPECT_EQ(trades[0].maker, 1u);
  // ask level 100 now removed -> qty 0
  bool ask_removed = false;
  for (auto& u : ch) if (u.side == Side::Sell && u.price == 100 && u.qty == 0) ask_removed = true;
  EXPECT_TRUE(ask_removed);
  EXPECT_EQ(ob.best_ask(), 0);     // 0 == no ask
  EXPECT_EQ(ob.best_bid(), 0);     // taker fully filled, nothing rested
}

TEST(OrderBook, PartialFillRestsRemainder) {
  OrderBook ob;
  std::vector<Trade> trades; std::vector<LevelUpdate> ch;
  ob.add_limit(limit(1, Side::Sell, 100, 4), trades, ch);
  trades.clear(); ch.clear();
  ob.add_limit(limit(2, Side::Buy, 100, 10), trades, ch);  // buys 4, rests 6
  ASSERT_EQ(trades.size(), 1u);
  EXPECT_EQ(trades[0].qty, 4);
  EXPECT_EQ(ob.best_bid(), 100);   // remainder rested as bid
  // bid level 100 should reflect qty 6
  Qty bid_qty = 0;
  for (auto& u : ch) if (u.side == Side::Buy && u.price == 100) bid_qty = u.qty;
  EXPECT_EQ(bid_qty, 6);
}

TEST(OrderBook, PriceTimePriorityFifo) {
  OrderBook ob;
  std::vector<Trade> trades; std::vector<LevelUpdate> ch;
  ob.add_limit(limit(1, Side::Buy, 100, 5), trades, ch);   // first in queue
  ob.add_limit(limit(2, Side::Buy, 100, 5), trades, ch);   // second
  trades.clear(); ch.clear();
  ob.add_limit(limit(3, Side::Sell, 100, 5), trades, ch);  // hits order 1 first
  ASSERT_EQ(trades.size(), 1u);
  EXPECT_EQ(trades[0].maker, 1u);   // FIFO: oldest order matched first
}

TEST(OrderBook, CancelRemovesResting) {
  OrderBook ob;
  std::vector<Trade> trades; std::vector<LevelUpdate> ch;
  ob.add_limit(limit(1, Side::Buy, 100, 5), trades, ch);
  ch.clear();
  EXPECT_TRUE(ob.cancel(1, ch));
  EXPECT_EQ(ob.best_bid(), 0);
  ASSERT_FALSE(ch.empty());
  EXPECT_EQ(ch.back().qty, 0);      // level removed
  EXPECT_FALSE(ob.cancel(1, ch));   // already gone
}
