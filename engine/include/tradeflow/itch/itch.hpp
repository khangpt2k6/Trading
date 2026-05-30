#pragma once
#include <cstddef>
#include <cstdint>

// Zero-copy parser for NASDAQ TotalView-ITCH 5.0.
//
// File framing: each message is preceded by a 2-byte big-endian length, then a
// payload whose first byte is the message-type char. All multi-byte fields are
// big-endian; timestamps are 48-bit ns since midnight; prices are u32 with 4
// implied decimals. Fields are read directly from the input buffer (no
// per-message allocation, no full-message copy).
namespace tradeflow::itch {

// --- Big-endian field readers (read straight from the buffer) ---
inline std::uint16_t rd_u16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>((std::uint16_t(p[0]) << 8) | p[1]);
}
inline std::uint32_t rd_u32(const std::uint8_t* p) {
  return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
         (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}
inline std::uint64_t rd_u48(const std::uint8_t* p) {
  std::uint64_t v = 0;
  for (int i = 0; i < 6; ++i) v = (v << 8) | p[i];
  return v;
}
inline std::uint64_t rd_u64(const std::uint8_t* p) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
  return v;
}

// Decoded book-relevant event. One compact POD, cheap to copy through a ring.
struct ItchEvent {
  char type = 0;               // 'A' 'F' 'E' 'C' 'X' 'D' 'U'
  char side = 0;               // 'B' or 'S' (add messages only)
  std::uint16_t stock_locate = 0;
  std::uint64_t timestamp = 0;     // 48-bit ns since midnight
  std::uint64_t order_ref = 0;     // primary ref (original ref for 'U')
  std::uint64_t new_order_ref = 0; // 'U' only
  std::uint32_t shares = 0;        // shares / executed / cancelled
  std::uint32_t price = 0;         // add/replace price, or 'C' exec price
};

// Decode one ITCH message payload (buf[0] = type, `len` bytes available) into a
// book event. Returns true for book-relevant messages (A F E C X D U) and false
// for everything else (which the caller can ignore).
inline bool parse_event(const std::uint8_t* buf, std::size_t len, ItchEvent& e) {
  if (len < 1) return false;
  const char t = static_cast<char>(buf[0]);
  e = ItchEvent{};
  e.type = t;
  switch (t) {
    case 'A':  // Add Order, no MPID (36)
    case 'F':  // Add Order, MPID attribution (40) - same layout for our fields
      if (len < 36) return false;
      e.stock_locate = rd_u16(buf + 1);
      e.timestamp = rd_u48(buf + 5);
      e.order_ref = rd_u64(buf + 11);
      e.side = static_cast<char>(buf[19]);
      e.shares = rd_u32(buf + 20);
      e.price = rd_u32(buf + 32);
      return true;
    case 'E':  // Order Executed (31)
      if (len < 31) return false;
      e.stock_locate = rd_u16(buf + 1);
      e.timestamp = rd_u48(buf + 5);
      e.order_ref = rd_u64(buf + 11);
      e.shares = rd_u32(buf + 19);
      return true;
    case 'C':  // Order Executed With Price (36)
      if (len < 36) return false;
      e.stock_locate = rd_u16(buf + 1);
      e.timestamp = rd_u48(buf + 5);
      e.order_ref = rd_u64(buf + 11);
      e.shares = rd_u32(buf + 19);
      e.price = rd_u32(buf + 32);
      return true;
    case 'X':  // Order Cancel (23)
      if (len < 23) return false;
      e.stock_locate = rd_u16(buf + 1);
      e.timestamp = rd_u48(buf + 5);
      e.order_ref = rd_u64(buf + 11);
      e.shares = rd_u32(buf + 19);
      return true;
    case 'D':  // Order Delete (19)
      if (len < 19) return false;
      e.stock_locate = rd_u16(buf + 1);
      e.timestamp = rd_u48(buf + 5);
      e.order_ref = rd_u64(buf + 11);
      return true;
    case 'U':  // Order Replace (35)
      if (len < 35) return false;
      e.stock_locate = rd_u16(buf + 1);
      e.timestamp = rd_u48(buf + 5);
      e.order_ref = rd_u64(buf + 11);
      e.new_order_ref = rd_u64(buf + 19);
      e.shares = rd_u32(buf + 27);
      e.price = rd_u32(buf + 31);
      return true;
    default:
      return false;
  }
}

// Read the ticker (8 bytes, space-padded) from a Stock Directory ('R', 39)
// message into out[8]. Returns the stock_locate, or 0 if not an 'R' message.
inline std::uint16_t parse_stock_directory(const std::uint8_t* buf,
                                           std::size_t len, char out[8]) {
  if (len < 39 || buf[0] != 'R') return 0;
  for (int i = 0; i < 8; ++i) out[i] = static_cast<char>(buf[11 + i]);
  return rd_u16(buf + 1);
}

// Iterate length-prefixed messages in a contiguous buffer, invoking
// fn(payload_ptr, payload_len) for each complete frame. Stops at the first
// incomplete/zero-length frame and returns the number of bytes consumed.
template <class Fn>
inline std::size_t for_each_message(const std::uint8_t* data, std::size_t size,
                                    Fn&& fn) {
  std::size_t off = 0;
  while (off + 2 <= size) {
    const std::uint16_t mlen = rd_u16(data + off);
    if (mlen == 0) break;
    if (off + 2 + mlen > size) break;  // incomplete trailing frame
    fn(data + off + 2, mlen);
    off += 2u + mlen;
  }
  return off;
}

}  // namespace tradeflow::itch
