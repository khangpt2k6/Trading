#pragma once
#include <cstdint>
#include <random>
#include <vector>
#include "tradeflow/order.hpp"

namespace tradeflow::bench {

struct WorkloadConfig {
  std::uint32_t num_symbols = 8;
  std::uint64_t num_orders = 2'000'000;
  unsigned seed = 12345;
  tradeflow::Price mid = 10000;     // center price in ticks
  tradeflow::Price band = 50;       // +/- ticks around mid
};

// Pre-generates orders WITHOUT timestamps (set ts_ingress at submit time).
// Mix: ~60% limit, ~20% market, ~20% cancel (cancel targets a prior order id).
inline std::vector<tradeflow::Order> generate(const WorkloadConfig& cfg) {
  using namespace tradeflow;
  std::mt19937_64 rng(cfg.seed);
  std::uniform_int_distribution<int> kind(0, 99);
  std::uniform_int_distribution<int> side(0, 1);
  std::uniform_int_distribution<int> off(-static_cast<int>(cfg.band),
                                         static_cast<int>(cfg.band));
  std::uniform_int_distribution<int> qty(1, 100);
  std::uniform_int_distribution<std::uint32_t> sym(0, cfg.num_symbols - 1);

  std::vector<Order> out;
  out.reserve(cfg.num_orders);
  std::vector<std::vector<OrderId>> live(cfg.num_symbols);  // resting per symbol
  OrderId next_id = 1;

  for (std::uint64_t i = 0; i < cfg.num_orders; ++i) {
    Order o{};
    o.id = next_id++;
    o.symbol = sym(rng);
    o.side = side(rng) ? Side::Sell : Side::Buy;
    const int k = kind(rng);
    if (k < 20 && !live[o.symbol].empty()) {            // ~20% cancel
      o.type = OrderType::Cancel;
      auto& v = live[o.symbol];
      std::uniform_int_distribution<std::size_t> pick(0, v.size() - 1);
      o.id = v[pick(rng)];                              // cancel a live order
    } else if (k < 40) {                                // ~20% market
      o.type = OrderType::Market;
      o.qty = qty(rng);
    } else {                                            // ~60% limit
      o.type = OrderType::Limit;
      o.price = cfg.mid + off(rng);
      o.qty = qty(rng);
      live[o.symbol].push_back(o.id);                   // may rest
    }
    out.push_back(o);
  }
  return out;
}

}  // namespace tradeflow::bench
