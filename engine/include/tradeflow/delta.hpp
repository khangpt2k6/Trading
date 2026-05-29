#pragma once
#include <vector>
#include "tradeflow/order.hpp"

namespace tradeflow {

// One aggregate price-level state. qty == 0 means the level was removed.
struct LevelUpdate {
  Side side = Side::Buy;
  Price price = 0;
  Qty qty = 0;
};

struct BookDelta {
  SymbolId symbol = 0;
  Seq seq = 0;
  std::vector<LevelUpdate> levels;
  std::vector<Trade> trades;

  bool empty() const { return levels.empty() && trades.empty(); }
  void clear() { levels.clear(); trades.clear(); }
};

}  // namespace tradeflow
