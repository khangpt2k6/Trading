#pragma once
#include <algorithm>
#include <functional>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>
#include "tradeflow/delta.hpp"
#include "tradeflow/order.hpp"

namespace tradeflow {

class OrderBook {
public:
  void add_limit(const Order& o, std::vector<Trade>& trades,
                 std::vector<LevelUpdate>& changes) {
    touched_buy_.clear();
    touched_sell_.clear();
    Qty remaining = o.qty;
    if (o.side == Side::Buy)
      remaining = cross_(asks_, Side::Sell, o, remaining, true, trades);
    else
      remaining = cross_(bids_, Side::Buy, o, remaining, true, trades);
    if (remaining > 0) rest_(o, remaining);
    emit_touched_(changes);
  }

  void add_market(const Order& o, std::vector<Trade>& trades,
                  std::vector<LevelUpdate>& changes) {
    touched_buy_.clear();
    touched_sell_.clear();
    Qty remaining = o.qty;
    if (o.side == Side::Buy)
      cross_(asks_, Side::Sell, o, remaining, false, trades);
    else
      cross_(bids_, Side::Buy, o, remaining, false, trades);
    // market remainder (if book exhausted) is discarded
    emit_touched_(changes);
  }

  bool cancel(OrderId id, std::vector<LevelUpdate>& changes) {
    auto it = locate_.find(id);
    if (it == locate_.end()) return false;
    touched_buy_.clear();
    touched_sell_.clear();
    const Side side = it->second.side;
    const Price price = it->second.price;
    if (side == Side::Buy)
      erase_resting_(bids_, Side::Buy, price, it->second.iter);
    else
      erase_resting_(asks_, Side::Sell, price, it->second.iter);
    locate_.erase(it);
    emit_touched_(changes);
    return true;
  }

  Price best_bid() const { return bids_.empty() ? 0 : bids_.begin()->first; }
  Price best_ask() const { return asks_.empty() ? 0 : asks_.begin()->first; }

  // Top-N aggregate levels per side (used by snapshots later).
  std::vector<LevelUpdate> snapshot(std::size_t depth) const {
    std::vector<LevelUpdate> out;
    std::size_t n = 0;
    for (auto& kv : bids_) {
      if (n++ >= depth) break;
      out.push_back({Side::Buy, kv.first, kv.second.total});
    }
    n = 0;
    for (auto& kv : asks_) {
      if (n++ >= depth) break;
      out.push_back({Side::Sell, kv.first, kv.second.total});
    }
    return out;
  }

private:
  struct Resting {
    OrderId id;
    Qty qty;
  };
  struct Level {
    Qty total = 0;
    std::list<Resting> fifo;
  };
  using Bids = std::map<Price, Level, std::greater<Price>>;
  using Asks = std::map<Price, Level, std::less<Price>>;

  struct Locator {
    Side side;
    Price price;
    std::list<Resting>::iterator iter;
  };

  // Cross an incoming order against `book` (the opposite side). `limited`
  // applies the price constraint (limit orders); market orders ignore price.
  template <typename BookT>
  Qty cross_(BookT& book, Side maker_side, const Order& taker, Qty remaining,
             bool limited, std::vector<Trade>& trades) {
    while (remaining > 0 && !book.empty()) {
      auto lvl_it = book.begin();
      const Price lvl_price = lvl_it->first;
      if (limited) {
        // For a buy taker, match while ask_price <= taker.price.
        // For a sell taker, match while bid_price >= taker.price.
        const bool ok = (taker.side == Side::Buy) ? (lvl_price <= taker.price)
                                                  : (lvl_price >= taker.price);
        if (!ok) break;
      }
      Level& lvl = lvl_it->second;
      while (remaining > 0 && !lvl.fifo.empty()) {
        Resting& front = lvl.fifo.front();
        const Qty fill = std::min(remaining, front.qty);
        trades.push_back(Trade{lvl_price, fill, taker.id, front.id, now_ns()});
        remaining -= fill;
        front.qty -= fill;
        lvl.total -= fill;
        if (front.qty == 0) {
          locate_.erase(front.id);
          lvl.fifo.pop_front();
        }
      }
      mark_touched_(maker_side, lvl_price);
      if (lvl.fifo.empty()) book.erase(lvl_it);
    }
    return remaining;
  }

  void rest_(const Order& o, Qty qty) {
    if (o.side == Side::Buy) {
      Level& lvl = bids_[o.price];
      lvl.total += qty;
      lvl.fifo.push_back({o.id, qty});
      auto it = std::prev(lvl.fifo.end());
      locate_[o.id] = {Side::Buy, o.price, it};
      mark_touched_(Side::Buy, o.price);
    } else {
      Level& lvl = asks_[o.price];
      lvl.total += qty;
      lvl.fifo.push_back({o.id, qty});
      auto it = std::prev(lvl.fifo.end());
      locate_[o.id] = {Side::Sell, o.price, it};
      mark_touched_(Side::Sell, o.price);
    }
  }

  template <typename BookT>
  void erase_resting_(BookT& book, Side side, Price price,
                      std::list<Resting>::iterator iter) {
    auto lvl_it = book.find(price);
    if (lvl_it == book.end()) return;
    Level& lvl = lvl_it->second;
    lvl.total -= iter->qty;
    lvl.fifo.erase(iter);
    if (lvl.fifo.empty()) book.erase(lvl_it);
    mark_touched_(side, price);
  }

  void mark_touched_(Side side, Price price) {
    if (side == Side::Buy)
      touched_buy_.insert(price);
    else
      touched_sell_.insert(price);
  }

  void emit_touched_(std::vector<LevelUpdate>& changes) {
    for (Price p : touched_buy_) {
      auto it = bids_.find(p);
      changes.push_back({Side::Buy, p, it == bids_.end() ? 0 : it->second.total});
    }
    for (Price p : touched_sell_) {
      auto it = asks_.find(p);
      changes.push_back({Side::Sell, p, it == asks_.end() ? 0 : it->second.total});
    }
  }

  Bids bids_;
  Asks asks_;
  std::unordered_map<OrderId, Locator> locate_;
  std::set<Price> touched_buy_;
  std::set<Price> touched_sell_;
};

}  // namespace tradeflow
