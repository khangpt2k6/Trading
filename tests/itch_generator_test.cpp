#include <gtest/gtest.h>
#include <cstdint>
#include <map>
#include <vector>
#include "tradeflow/itch/itch.hpp"
#include "tradeflow/itch/itch_generator.hpp"

using namespace tradeflow::itch;

TEST(ItchGenerator, ProducesFullyParseableStream) {
  GenConfig cfg;
  cfg.num_symbols = 8;
  cfg.num_events = 50000;
  cfg.seed = 42;
  std::vector<std::uint8_t> buf;
  const std::uint64_t emitted = generate(cfg, buf);
  EXPECT_EQ(emitted, cfg.num_events);
  ASSERT_FALSE(buf.empty());

  std::map<char, int> counts;
  std::size_t framed = 0;
  const std::size_t consumed = for_each_message(
      buf.data(), buf.size(), [&](const std::uint8_t* p, std::uint16_t len) {
        ++framed;
        ItchEvent e{};
        if (parse_event(p, len, e)) counts[e.type]++;
      });

  // Every byte is part of a valid frame (no trailing garbage).
  EXPECT_EQ(consumed, buf.size());
  // System event + N stock directories + the book events were all framed.
  EXPECT_EQ(framed, 1u + cfg.num_symbols + cfg.num_events);
  // The book-relevant events parsed equals the number generated.
  std::uint64_t parsed_book = 0;
  for (auto& kv : counts) parsed_book += kv.second;
  EXPECT_EQ(parsed_book, cfg.num_events);
  // The mix exercises adds plus at least one of each mutation type.
  EXPECT_GT(counts['A'], 0);
  EXPECT_GT(counts['E'], 0);
  EXPECT_GT(counts['X'], 0);
  EXPECT_GT(counts['D'], 0);
  EXPECT_GT(counts['U'], 0);
}

TEST(ItchGenerator, StockDirectoryTickersDecode) {
  GenConfig cfg;
  cfg.num_symbols = 4;
  cfg.num_events = 10;
  std::vector<std::uint8_t> buf;
  generate(cfg, buf);

  int dir_count = 0;
  for_each_message(buf.data(), buf.size(),
                   [&](const std::uint8_t* p, std::uint16_t len) {
                     char ticker[8];
                     std::uint16_t locate = parse_stock_directory(p, len, ticker);
                     if (locate != 0) {
                       ++dir_count;
                       EXPECT_GE(locate, 1);
                       EXPECT_LE(locate, cfg.num_symbols);
                       EXPECT_EQ(ticker[0], 'S');  // "SYM....." pattern
                     }
                   });
  EXPECT_EQ(dir_count, cfg.num_symbols);
}

TEST(ItchGenerator, Deterministic) {
  GenConfig cfg;
  cfg.num_symbols = 8;
  cfg.num_events = 20000;
  cfg.seed = 123;
  std::vector<std::uint8_t> a, b;
  generate(cfg, a);
  generate(cfg, b);
  EXPECT_EQ(a, b);  // same seed -> identical bytes
}
