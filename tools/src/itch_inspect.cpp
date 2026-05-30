#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include "tradeflow/itch/itch.hpp"
#include "tradeflow/itch/itch_reader.hpp"

using namespace tradeflow::itch;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: itch_inspect <file.gz> [max_messages]\n");
    return 1;
  }
  const std::string path = argv[1];
  const std::uint64_t maxm = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 0;

  std::map<char, std::uint64_t> hist;
  std::unordered_map<std::uint16_t, std::array<char, 8>> tickers;
  std::unordered_map<std::uint16_t, std::uint64_t> per_sym;

  const auto t0 = std::chrono::steady_clock::now();
  const ReadStats st = read_itch_gz(
      path,
      [&](const std::uint8_t* p, std::uint16_t len) {
        hist[static_cast<char>(p[0])]++;
        char tk[8];
        const std::uint16_t loc = parse_stock_directory(p, len, tk);
        if (loc) {
          std::array<char, 8> a{};
          std::memcpy(a.data(), tk, 8);
          tickers[loc] = a;
        }
        ItchEvent e{};
        if (parse_event(p, len, e)) per_sym[e.stock_locate]++;
      },
      maxm);
  const auto t1 = std::chrono::steady_clock::now();

  if (!st.opened) {
    std::printf("error: could not open %s\n", path.c_str());
    return 1;
  }
  const double secs = std::chrono::duration<double>(t1 - t0).count();

  std::printf("file: %s\n", path.c_str());
  std::printf("messages: %llu  decompressed: %.1f MB  truncated: %s\n",
              (unsigned long long)st.messages, st.bytes / 1e6,
              st.truncated ? "yes (partial file)" : "no");
  std::printf("parse rate: %.2f M msgs/sec (%.2fs)\n",
              st.messages / secs / 1e6, secs);

  std::printf("\nmessage-type histogram:\n");
  for (auto& kv : hist)
    std::printf("  %c : %llu\n", kv.first, (unsigned long long)kv.second);

  std::vector<std::pair<std::uint16_t, std::uint64_t>> top(per_sym.begin(),
                                                           per_sym.end());
  std::sort(top.begin(), top.end(),
            [](auto& a, auto& b) { return a.second > b.second; });
  std::printf("\ntop symbols by book events:\n");
  for (std::size_t i = 0; i < top.size() && i < 10; ++i) {
    char name[9] = {0};
    auto it = tickers.find(top[i].first);
    if (it != tickers.end()) std::memcpy(name, it->second.data(), 8);
    for (int j = 7; j >= 0; --j) {
      if (name[j] == ' ' || name[j] == 0) name[j] = 0; else break;
    }
    std::printf("  %-8s (locate %5u): %llu events\n", name, top[i].first,
                (unsigned long long)top[i].second);
  }
  return 0;
}
