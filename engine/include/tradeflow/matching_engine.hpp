#pragma once
#include "tradeflow/delta.hpp"
#include "tradeflow/order.hpp"
#include "tradeflow/order_book.hpp"

namespace tradeflow {

// Wraps one OrderBook for a single symbol and turns submitted orders into
// BookDeltas with a monotonically increasing sequence number.
class MatchingEngine {
public:
  explicit MatchingEngine(SymbolId symbol) : symbol_(symbol) {}

  // Fills `out` with the resulting delta. `out` is cleared first.
  void submit(const Order& o, BookDelta& out) {
    out.clear();
    out.symbol = symbol_;
    out.seq = ++seq_;
    switch (o.type) {
      case OrderType::Limit:
        book_.add_limit(o, out.trades, out.levels);
        break;
      case OrderType::Market:
        book_.add_market(o, out.trades, out.levels);
        break;
      case OrderType::Cancel:
        book_.cancel(o.id, out.levels);
        break;
    }
  }

  const OrderBook& book() const { return book_; }
  SymbolId symbol() const { return symbol_; }
  Seq seq() const { return seq_; }

private:
  SymbolId symbol_;
  Seq seq_ = 0;
  OrderBook book_;
};

}  // namespace tradeflow
