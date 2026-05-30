#pragma once
#include <zlib.h>
#include <cstdint>
#include <string>
#include <vector>
#include "tradeflow/itch/itch.hpp"

// Streaming reader for (gzip-compressed) NASDAQ ITCH files. Decompresses in
// chunks via zlib and invokes a callback per complete length-prefixed message,
// stitching frames that span chunk boundaries. Tolerates a truncated tail (the
// expected case for HTTP-Range partial downloads).
namespace tradeflow::itch {

struct ReadStats {
  std::uint64_t messages = 0;       // complete framed messages delivered
  std::uint64_t bytes = 0;          // decompressed bytes read
  bool truncated = false;           // stream ended mid-frame
  bool opened = false;              // file opened successfully
};

// fn signature: void(const std::uint8_t* payload, std::uint16_t len).
// Stops at end of stream or once max_messages (if non-zero) is reached.
template <class Fn>
inline ReadStats read_itch_gz(const std::string& path, Fn&& fn,
                              std::uint64_t max_messages = 0) {
  ReadStats st{};
  gzFile gz = gzopen(path.c_str(), "rb");
  if (!gz) return st;
  st.opened = true;
  gzbuffer(gz, 1u << 20);

  std::vector<std::uint8_t> chunk(1u << 20);
  std::vector<std::uint8_t> work;
  work.reserve(1u << 21);
  std::size_t carry = 0;  // bytes of leftover partial frame held at front of work

  for (;;) {
    const int n = gzread(gz, chunk.data(), static_cast<unsigned>(chunk.size()));
    if (n <= 0) break;  // EOF or truncated input
    st.bytes += static_cast<std::uint64_t>(n);

    work.resize(carry);
    work.insert(work.end(), chunk.data(), chunk.data() + n);

    const std::size_t consumed = for_each_message(
        work.data(), work.size(), [&](const std::uint8_t* p, std::uint16_t len) {
          fn(p, len);
          ++st.messages;
        });

    // Move the unconsumed tail (a partial frame) to the front for next round.
    carry = work.size() - consumed;
    if (carry) work.erase(work.begin(), work.begin() + consumed);

    if (max_messages && st.messages >= max_messages) break;
  }
  st.truncated = (carry != 0);
  gzclose(gz);
  return st;
}

}  // namespace tradeflow::itch
