#pragma once
#include <array>
#include <cstdint>
#include <random>
#include <vector>

// Spec-accurate NASDAQ ITCH 5.0 byte generator. Produces a deterministic,
// internally-consistent length-prefixed stream (System Event, Stock Directory
// per symbol, then Add/Execute/Cancel/Delete/Replace events) for tests, CI, and
// quick benchmarks without downloading the multi-GB real file. Executions and
// cancels never exceed an order's resting size, so a book builder fed this
// stream stays non-negative and ends consistent.
namespace tradeflow::itch {

struct GenConfig {
  std::uint16_t num_symbols = 16;
  std::uint64_t num_events = 1'000'000;  // book events after the directory
  unsigned seed = 7;
  std::uint32_t base_price = 1'000'000;  // $100.0000 in 1/10000 ticks
  std::uint32_t price_band = 5'000;      // +/- $0.5000
};

namespace detail {

// Appends big-endian fields and length-prefixed frames to a byte vector.
class Writer {
public:
  explicit Writer(std::vector<std::uint8_t>& b) : b_(b) {}
  void begin() {
    len_pos_ = b_.size();
    b_.push_back(0);
    b_.push_back(0);
  }
  void end() {
    const std::uint16_t l = static_cast<std::uint16_t>(b_.size() - len_pos_ - 2);
    b_[len_pos_] = static_cast<std::uint8_t>(l >> 8);
    b_[len_pos_ + 1] = static_cast<std::uint8_t>(l & 0xff);
  }
  void u8(std::uint8_t v) { b_.push_back(v); }
  void u16(std::uint16_t v) { b_.push_back(v >> 8); b_.push_back(v & 0xff); }
  void u32(std::uint32_t v) {
    for (int i = 3; i >= 0; --i) b_.push_back((v >> (8 * i)) & 0xff);
  }
  void u48(std::uint64_t v) {
    for (int i = 5; i >= 0; --i) b_.push_back((v >> (8 * i)) & 0xff);
  }
  void u64(std::uint64_t v) {
    for (int i = 7; i >= 0; --i) b_.push_back((v >> (8 * i)) & 0xff);
  }
  void bytes(const char* p, int n) {
    for (int i = 0; i < n; ++i) b_.push_back(static_cast<std::uint8_t>(p[i]));
  }

private:
  std::vector<std::uint8_t>& b_;
  std::size_t len_pos_ = 0;
};

inline std::array<char, 8> ticker_for(std::uint16_t locate) {
  std::array<char, 8> t{};
  t[0] = 'S'; t[1] = 'Y'; t[2] = 'M';
  std::uint16_t n = locate;
  for (int i = 7; i >= 3; --i) { t[i] = static_cast<char>('0' + (n % 10)); n /= 10; }
  return t;
}

}  // namespace detail

// Appends a generated ITCH stream to `out`. Returns the number of book events
// emitted (excludes the System Event and Stock Directory messages).
inline std::uint64_t generate(const GenConfig& cfg, std::vector<std::uint8_t>& out) {
  using detail::Writer;
  std::mt19937_64 rng(cfg.seed);
  Writer w(out);

  // System Event 'O' (start of messages).
  w.begin(); w.u8('S'); w.u16(0); w.u16(0); w.u48(0); w.u8('O'); w.end();

  // Stock Directory for each symbol (payload must total 39 bytes).
  for (std::uint16_t s = 0; s < cfg.num_symbols; ++s) {
    const std::uint16_t locate = static_cast<std::uint16_t>(s + 1);
    const auto t = detail::ticker_for(locate);
    w.begin();
    w.u8('R'); w.u16(locate); w.u16(0); w.u48(0);
    w.bytes(t.data(), 8);
    for (int i = 0; i < 20; ++i) w.u8(' ');  // remaining R fields (defaults)
    w.end();
  }

  struct Live { std::uint64_t ref; std::uint32_t shares; };
  std::vector<std::vector<Live>> live(cfg.num_symbols);
  std::uint64_t next_ref = 1;
  std::uint64_t ts = 0;
  std::uint64_t match = 1;

  for (std::uint64_t i = 0; i < cfg.num_events; ++i) {
    ts += (rng() % 100) + 1;
    const std::uint16_t s = static_cast<std::uint16_t>(rng() % cfg.num_symbols);
    const std::uint16_t locate = static_cast<std::uint16_t>(s + 1);
    auto& v = live[s];
    const int k = static_cast<int>(rng() % 100);

    if (k < 55 || v.empty()) {                       // Add Order
      const std::uint64_t ref = next_ref++;
      const char side = (rng() & 1) ? 'S' : 'B';
      const std::uint32_t shares = static_cast<std::uint32_t>(rng() % 1000) + 1;
      const std::uint32_t price =
          cfg.base_price +
          static_cast<std::uint32_t>(rng() % (2 * cfg.price_band + 1)) -
          cfg.price_band;
      const auto t = detail::ticker_for(locate);
      w.begin();
      w.u8('A'); w.u16(locate); w.u16(0); w.u48(ts); w.u64(ref); w.u8(side);
      w.u32(shares); w.bytes(t.data(), 8); w.u32(price);
      w.end();
      v.push_back({ref, shares});
    } else {
      const std::size_t idx = rng() % v.size();
      Live& o = v[idx];
      const int op = static_cast<int>(rng() % 4);
      if (op == 0) {                                  // Order Executed (E)
        const std::uint32_t qty =
            static_cast<std::uint32_t>(rng() % o.shares) + 1;
        w.begin();
        w.u8('E'); w.u16(locate); w.u16(0); w.u48(ts); w.u64(o.ref);
        w.u32(qty); w.u64(match++);
        w.end();
        o.shares -= qty;
        if (o.shares == 0) { v[idx] = v.back(); v.pop_back(); }
      } else if (op == 1) {                           // Order Cancel (X)
        const std::uint32_t qty =
            static_cast<std::uint32_t>(rng() % o.shares) + 1;
        w.begin();
        w.u8('X'); w.u16(locate); w.u16(0); w.u48(ts); w.u64(o.ref); w.u32(qty);
        w.end();
        o.shares -= qty;
        if (o.shares == 0) { v[idx] = v.back(); v.pop_back(); }
      } else if (op == 2) {                           // Order Delete (D)
        w.begin();
        w.u8('D'); w.u16(locate); w.u16(0); w.u48(ts); w.u64(o.ref);
        w.end();
        v[idx] = v.back(); v.pop_back();
      } else {                                        // Order Replace (U)
        const std::uint64_t nref = next_ref++;
        const std::uint32_t shares =
            static_cast<std::uint32_t>(rng() % 1000) + 1;
        const std::uint32_t price =
            cfg.base_price +
            static_cast<std::uint32_t>(rng() % (2 * cfg.price_band + 1)) -
            cfg.price_band;
        w.begin();
        w.u8('U'); w.u16(locate); w.u16(0); w.u48(ts); w.u64(o.ref); w.u64(nref);
        w.u32(shares); w.u32(price);
        w.end();
        o = {nref, shares};
      }
    }
  }
  return cfg.num_events;
}

}  // namespace tradeflow::itch
