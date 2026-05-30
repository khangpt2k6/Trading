#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include "tradeflow/itch/itch.hpp"

using namespace tradeflow::itch;

// Helpers to append big-endian fields to a byte buffer (test-side encoder).
static void put_u16(std::vector<std::uint8_t>& b, std::uint16_t v) {
  b.push_back(v >> 8); b.push_back(v & 0xff);
}
static void put_u32(std::vector<std::uint8_t>& b, std::uint32_t v) {
  for (int i = 3; i >= 0; --i) b.push_back((v >> (8 * i)) & 0xff);
}
static void put_u48(std::vector<std::uint8_t>& b, std::uint64_t v) {
  for (int i = 5; i >= 0; --i) b.push_back((v >> (8 * i)) & 0xff);
}
static void put_u64(std::vector<std::uint8_t>& b, std::uint64_t v) {
  for (int i = 7; i >= 0; --i) b.push_back((v >> (8 * i)) & 0xff);
}

TEST(ItchReaders, BigEndianDecode) {
  std::uint8_t b16[] = {0x12, 0x34};
  EXPECT_EQ(rd_u16(b16), 0x1234u);
  std::uint8_t b32[] = {0x01, 0x02, 0x03, 0x04};
  EXPECT_EQ(rd_u32(b32), 0x01020304u);
  std::uint8_t b48[] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x00};
  EXPECT_EQ(rd_u48(b48), 256u);
  std::uint8_t b64[] = {0, 0, 0, 0, 0, 0, 0x01, 0x00};
  EXPECT_EQ(rd_u64(b64), 256u);
}

TEST(ItchParser, AddOrder) {
  std::vector<std::uint8_t> p;
  p.push_back('A');
  put_u16(p, 7);          // stock_locate
  put_u16(p, 0);          // tracking
  put_u48(p, 123456);     // timestamp
  put_u64(p, 1001);       // order_ref
  p.push_back('B');       // buy
  put_u32(p, 500);        // shares
  for (int i = 0; i < 8; ++i) p.push_back('A');  // stock "AAAAAAAA"
  put_u32(p, 1234500);    // price (123.45)
  ASSERT_EQ(p.size(), 36u);

  ItchEvent e{};
  ASSERT_TRUE(parse_event(p.data(), p.size(), e));
  EXPECT_EQ(e.type, 'A');
  EXPECT_EQ(e.stock_locate, 7u);
  EXPECT_EQ(e.timestamp, 123456u);
  EXPECT_EQ(e.order_ref, 1001u);
  EXPECT_EQ(e.side, 'B');
  EXPECT_EQ(e.shares, 500u);
  EXPECT_EQ(e.price, 1234500u);
}

TEST(ItchParser, ExecuteCancelDelete) {
  // Order Executed (E)
  std::vector<std::uint8_t> e_;
  e_.push_back('E'); put_u16(e_, 7); put_u16(e_, 0); put_u48(e_, 1);
  put_u64(e_, 1001); put_u32(e_, 100); put_u64(e_, 9001);
  ASSERT_EQ(e_.size(), 31u);
  ItchEvent e{};
  ASSERT_TRUE(parse_event(e_.data(), e_.size(), e));
  EXPECT_EQ(e.type, 'E'); EXPECT_EQ(e.order_ref, 1001u); EXPECT_EQ(e.shares, 100u);

  // Order Cancel (X)
  std::vector<std::uint8_t> x_;
  x_.push_back('X'); put_u16(x_, 7); put_u16(x_, 0); put_u48(x_, 2);
  put_u64(x_, 1001); put_u32(x_, 50);
  ASSERT_EQ(x_.size(), 23u);
  ASSERT_TRUE(parse_event(x_.data(), x_.size(), e));
  EXPECT_EQ(e.type, 'X'); EXPECT_EQ(e.shares, 50u);

  // Order Delete (D)
  std::vector<std::uint8_t> d_;
  d_.push_back('D'); put_u16(d_, 7); put_u16(d_, 0); put_u48(d_, 3);
  put_u64(d_, 1001);
  ASSERT_EQ(d_.size(), 19u);
  ASSERT_TRUE(parse_event(d_.data(), d_.size(), e));
  EXPECT_EQ(e.type, 'D'); EXPECT_EQ(e.order_ref, 1001u);
}

TEST(ItchParser, Replace) {
  std::vector<std::uint8_t> u_;
  u_.push_back('U'); put_u16(u_, 7); put_u16(u_, 0); put_u48(u_, 4);
  put_u64(u_, 1001);      // orig ref
  put_u64(u_, 2002);      // new ref
  put_u32(u_, 300);       // shares
  put_u32(u_, 1239900);   // price
  ASSERT_EQ(u_.size(), 35u);
  ItchEvent e{};
  ASSERT_TRUE(parse_event(u_.data(), u_.size(), e));
  EXPECT_EQ(e.type, 'U');
  EXPECT_EQ(e.order_ref, 1001u);
  EXPECT_EQ(e.new_order_ref, 2002u);
  EXPECT_EQ(e.shares, 300u);
  EXPECT_EQ(e.price, 1239900u);
}

TEST(ItchParser, IgnoresNonBookMessages) {
  std::vector<std::uint8_t> s_;
  s_.push_back('S'); put_u16(s_, 0); put_u16(s_, 0); put_u48(s_, 0);
  s_.push_back('O');  // event code
  ItchEvent e{};
  EXPECT_FALSE(parse_event(s_.data(), s_.size(), e));  // not book-relevant
}

TEST(ItchFraming, IteratesLengthPrefixedMessages) {
  // Build two framed messages: a Delete then an Add.
  auto frame = [](std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& msg) {
    put_u16(out, static_cast<std::uint16_t>(msg.size()));
    out.insert(out.end(), msg.begin(), msg.end());
  };
  std::vector<std::uint8_t> d_;
  d_.push_back('D'); put_u16(d_, 1); put_u16(d_, 0); put_u48(d_, 1); put_u64(d_, 42);
  std::vector<std::uint8_t> a_;
  a_.push_back('A'); put_u16(a_, 1); put_u16(a_, 0); put_u48(a_, 2); put_u64(a_, 43);
  a_.push_back('S'); put_u32(a_, 10); for (int i=0;i<8;++i) a_.push_back('Z'); put_u32(a_, 100);

  std::vector<std::uint8_t> stream;
  frame(stream, d_);
  frame(stream, a_);
  // Append a truncated trailing frame (declared len longer than available).
  put_u16(stream, 50); stream.push_back('A');

  std::vector<char> types;
  const std::size_t consumed = for_each_message(
      stream.data(), stream.size(), [&](const std::uint8_t* p, std::uint16_t len) {
        ItchEvent e{};
        if (parse_event(p, len, e)) types.push_back(e.type);
      });
  ASSERT_EQ(types.size(), 2u);
  EXPECT_EQ(types[0], 'D');
  EXPECT_EQ(types[1], 'A');
  // consumed stops before the truncated trailing frame
  EXPECT_EQ(consumed, 2u + d_.size() + 2u + a_.size());
}
