#include <gtest/gtest.h>
#include "tradeflow/order.hpp"
#include "tradeflow/delta.hpp"

using namespace tradeflow;

TEST(Types, OrderDefaults) {
  Order o{};
  o.id = 1; o.symbol = 0; o.side = Side::Buy; o.type = OrderType::Limit;
  o.price = 100; o.qty = 10;
  EXPECT_EQ(o.qty, 10);
  EXPECT_EQ(o.side, Side::Buy);
}

TEST(Types, NowMonotonic) {
  const auto a = now_ns();
  const auto b = now_ns();
  EXPECT_GE(b, a);
}

TEST(Types, LevelUpdateRemoveSemantics) {
  LevelUpdate u{Side::Sell, 101, 0};  // qty 0 means level removed
  EXPECT_EQ(u.qty, 0);
}
