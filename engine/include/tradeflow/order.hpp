#pragma once
#include <chrono>
#include <cstdint>

namespace tradeflow {

using Price = std::int64_t;     // price in integer ticks
using Qty = std::int64_t;
using OrderId = std::uint64_t;
using SymbolId = std::uint32_t;
using Seq = std::uint64_t;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };
enum class OrderType : std::uint8_t { Limit = 0, Market = 1, Cancel = 2 };

struct Order {
  OrderId id = 0;
  SymbolId symbol = 0;
  Side side = Side::Buy;
  OrderType type = OrderType::Limit;
  Price price = 0;            // ignored for Market and Cancel
  Qty qty = 0;               // for Cancel, target order id is in `id`
  std::uint64_t ts_ingress = 0;  // ns timestamp, set when submitted
};

struct Trade {
  Price price = 0;
  Qty qty = 0;
  OrderId taker = 0;
  OrderId maker = 0;
  std::uint64_t ts = 0;
};

// Monotonic nanosecond clock for latency measurement.
inline std::uint64_t now_ns() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

}  // namespace tradeflow
