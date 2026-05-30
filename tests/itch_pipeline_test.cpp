#include <gtest/gtest.h>
#include <vector>
#include "tradeflow/itch/book_builder.hpp"
#include "tradeflow/itch/itch.hpp"
#include "tradeflow/itch/itch_generator.hpp"
#include "tradeflow/itch/pipeline.hpp"

using namespace tradeflow::itch;

static std::vector<ItchEvent> gen_events(unsigned symbols, std::uint64_t n,
                                         unsigned seed) {
  GenConfig cfg; cfg.num_symbols = symbols; cfg.num_events = n; cfg.seed = seed;
  std::vector<std::uint8_t> buf;
  generate(cfg, buf);
  std::vector<ItchEvent> out;
  for_each_message(buf.data(), buf.size(),
                   [&](const std::uint8_t* p, std::uint16_t len) {
                     ItchEvent e{};
                     if (parse_event(p, len, e)) out.push_back(e);
                   });
  return out;
}

TEST(ItchPipeline, ShardedMatchesSingleThreaded) {
  const unsigned symbols = 16;
  const auto events = gen_events(symbols, 300000, 7);

  // Reference single-threaded book.
  BookBuilder ref;
  for (const auto& e : events) ref.apply(e);

  // Sharded lock-free engine.
  ShardedBookEngine eng(4, 1 << 16);
  eng.start();
  for (const auto& e : events) while (!eng.submit(e)) { /* spin */ }
  eng.drain_and_stop();

  EXPECT_EQ(eng.processed(), events.size());
  for (std::uint16_t loc = 1; loc <= symbols; ++loc) {
    EXPECT_EQ(eng.best_bid(loc), ref.best_bid(loc)) << "locate " << loc;
    EXPECT_EQ(eng.best_ask(loc), ref.best_ask(loc)) << "locate " << loc;
  }
  std::int64_t total = 0;
  for (unsigned s = 0; s < 4; ++s) total += eng.builder(s).total_level_shares();
  EXPECT_EQ(total, ref.total_level_shares());
}

TEST(ItchPipeline, BaselineMatchesSingleThreaded) {
  const unsigned symbols = 8;
  const auto events = gen_events(symbols, 150000, 11);

  BookBuilder ref;
  for (const auto& e : events) ref.apply(e);

  SequentialBookEngine eng;
  eng.start();
  for (const auto& e : events) eng.submit(e);
  eng.drain_and_stop();

  EXPECT_EQ(eng.processed(), events.size());
  for (std::uint16_t loc = 1; loc <= symbols; ++loc) {
    EXPECT_EQ(eng.builder().best_bid(loc), ref.best_bid(loc));
    EXPECT_EQ(eng.builder().best_ask(loc), ref.best_ask(loc));
  }
  EXPECT_EQ(eng.builder().total_level_shares(), ref.total_level_shares());
}

TEST(ItchPipeline, SinkFiresPerEvent) {
  const auto events = gen_events(4, 5000, 3);
  std::atomic<std::uint64_t> seen{0};
  ShardedBookEngine eng(2, 1 << 14,
                        [&](unsigned, const PipeEvent&) {
                          seen.fetch_add(1, std::memory_order_relaxed);
                        });
  eng.start();
  for (const auto& e : events) while (!eng.submit(e)) {}
  eng.drain_and_stop();
  EXPECT_EQ(seen.load(), events.size());
}
