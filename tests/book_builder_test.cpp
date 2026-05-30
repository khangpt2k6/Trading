#include <gtest/gtest.h>
#include <vector>
#include "tradeflow/itch/book_builder.hpp"
#include "tradeflow/itch/itch.hpp"
#include "tradeflow/itch/itch_generator.hpp"

using namespace tradeflow::itch;

static ItchEvent add(std::uint64_t ref, std::uint16_t loc, char side,
                     std::uint32_t price, std::uint32_t shares) {
  ItchEvent e{}; e.type = 'A'; e.order_ref = ref; e.stock_locate = loc;
  e.side = side; e.price = price; e.shares = shares; return e;
}

TEST(BookBuilder, AddSetsBestBidAsk) {
  BookBuilder bb;
  bb.apply(add(1, 5, 'B', 1000, 100));
  bb.apply(add(2, 5, 'S', 1010, 50));
  EXPECT_EQ(bb.best_bid(5), 1000);
  EXPECT_EQ(bb.best_ask(5), 1010);
  EXPECT_EQ(bb.shares_at(5, 'B', 1000), 100);
  EXPECT_EQ(bb.shares_at(5, 'S', 1010), 50);
}

TEST(BookBuilder, ExecuteReducesThenRemoves) {
  BookBuilder bb;
  bb.apply(add(1, 5, 'B', 1000, 100));
  ItchEvent e{}; e.type='E'; e.order_ref=1; e.stock_locate=5; e.shares=40;
  bb.apply(e);
  EXPECT_EQ(bb.shares_at(5, 'B', 1000), 60);
  e.shares = 60; bb.apply(e);           // fully executed
  EXPECT_EQ(bb.best_bid(5), 0);          // level gone
  EXPECT_EQ(bb.order_count(), 0u);
}

TEST(BookBuilder, ExecuteIsClampedNeverNegative) {
  BookBuilder bb;
  bb.apply(add(1, 5, 'S', 2000, 30));
  ItchEvent e{}; e.type='X'; e.order_ref=1; e.stock_locate=5; e.shares=999;  // over-cancel
  bb.apply(e);
  EXPECT_EQ(bb.best_ask(5), 0);
  EXPECT_EQ(bb.total_level_shares(), 0);
}

TEST(BookBuilder, DeleteRemovesRemaining) {
  BookBuilder bb;
  bb.apply(add(1, 5, 'B', 1000, 100));
  bb.apply(add(2, 5, 'B', 1000, 25));    // same level, aggregate 125
  EXPECT_EQ(bb.shares_at(5, 'B', 1000), 125);
  ItchEvent d{}; d.type='D'; d.order_ref=1; d.stock_locate=5;
  bb.apply(d);
  EXPECT_EQ(bb.shares_at(5, 'B', 1000), 25);   // order 2 remains
}

TEST(BookBuilder, ReplaceMovesPriceKeepingSide) {
  BookBuilder bb;
  bb.apply(add(1, 5, 'B', 1000, 100));
  ItchEvent u{}; u.type='U'; u.order_ref=1; u.new_order_ref=2; u.stock_locate=5;
  u.price=1005; u.shares=80;
  bb.apply(u);
  EXPECT_EQ(bb.shares_at(5, 'B', 1000), 0);
  EXPECT_EQ(bb.shares_at(5, 'B', 1005), 80);
  EXPECT_EQ(bb.best_bid(5), 1005);
}

// Invariant: across an entire generated stream, the sum of per-order shares must
// always equal the sum of aggregated price-level shares.
TEST(BookBuilder, DualBookkeepingStaysConsistent) {
  GenConfig cfg; cfg.num_symbols = 8; cfg.num_events = 200000; cfg.seed = 99;
  std::vector<std::uint8_t> buf;
  generate(cfg, buf);

  BookBuilder bb;
  for_each_message(buf.data(), buf.size(),
                   [&](const std::uint8_t* p, std::uint16_t len) {
                     ItchEvent e{};
                     if (parse_event(p, len, e)) bb.apply(e);
                   });
  EXPECT_EQ(bb.total_order_shares(), bb.total_level_shares());
  EXPECT_GT(bb.total_level_shares(), 0);  // some orders still resting
}
