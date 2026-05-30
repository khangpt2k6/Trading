#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include "tradeflow/itch/itch.hpp"

// Reconstructs aggregated L2 order books from a stream of ITCH events. Tracks
// every live order by reference number and maintains per-symbol price-level
// totals. Defensive against malformed/over-sized reductions (clamps to zero).
namespace tradeflow::itch {

class BookBuilder {
public:
  struct Order {
    std::uint16_t locate;
    char side;            // 'B' or 'S'
    std::uint32_t price;
    std::uint32_t shares;
  };
  struct Book {
    std::map<std::uint32_t, std::int64_t, std::greater<std::uint32_t>> bids;
    std::map<std::uint32_t, std::int64_t> asks;
  };

  void apply(const ItchEvent& e) {
    switch (e.type) {
      case 'A':
      case 'F': add_(e); break;
      case 'E':
      case 'C':
      case 'X': reduce_(e); break;   // E/C/X all reduce by e.shares
      case 'D': del_(e); break;
      case 'U': replace_(e); break;
      default: break;
    }
  }

  std::int64_t best_bid(std::uint16_t locate) const {
    auto it = books_.find(locate);
    if (it == books_.end() || it->second.bids.empty()) return 0;
    return static_cast<std::int64_t>(it->second.bids.begin()->first);
  }
  std::int64_t best_ask(std::uint16_t locate) const {
    auto it = books_.find(locate);
    if (it == books_.end() || it->second.asks.empty()) return 0;
    return static_cast<std::int64_t>(it->second.asks.begin()->first);
  }
  std::int64_t shares_at(std::uint16_t locate, char side,
                         std::uint32_t price) const {
    auto it = books_.find(locate);
    if (it == books_.end()) return 0;
    if (side == 'B') {
      auto lv = it->second.bids.find(price);
      return lv == it->second.bids.end() ? 0 : lv->second;
    }
    auto lv = it->second.asks.find(price);
    return lv == it->second.asks.end() ? 0 : lv->second;
  }

  std::size_t order_count() const { return orders_.size(); }

  std::int64_t total_order_shares() const {
    std::int64_t s = 0;
    for (auto& kv : orders_) s += kv.second.shares;
    return s;
  }
  std::int64_t total_level_shares() const {
    std::int64_t s = 0;
    for (auto& kv : books_) {
      for (auto& l : kv.second.bids) s += l.second;
      for (auto& l : kv.second.asks) s += l.second;
    }
    return s;
  }

  const std::unordered_map<std::uint16_t, Book>& books() const { return books_; }

private:
  void level_add_(std::uint16_t locate, char side, std::uint32_t price,
                  std::int64_t dq) {
    Book& b = books_[locate];
    if (side == 'B') {
      auto& q = b.bids[price];
      q += dq;
      if (q <= 0) b.bids.erase(price);
    } else {
      auto& q = b.asks[price];
      q += dq;
      if (q <= 0) b.asks.erase(price);
    }
  }

  void add_(const ItchEvent& e) {
    orders_[e.order_ref] = {e.stock_locate, e.side, e.price, e.shares};
    level_add_(e.stock_locate, e.side, e.price,
               static_cast<std::int64_t>(e.shares));
  }

  void reduce_(const ItchEvent& e) {
    auto it = orders_.find(e.order_ref);
    if (it == orders_.end()) return;
    Order& o = it->second;
    const std::uint32_t q = e.shares < o.shares ? e.shares : o.shares;  // clamp
    o.shares -= q;
    level_add_(o.locate, o.side, o.price, -static_cast<std::int64_t>(q));
    if (o.shares == 0) orders_.erase(it);
  }

  void del_(const ItchEvent& e) {
    auto it = orders_.find(e.order_ref);
    if (it == orders_.end()) return;
    Order& o = it->second;
    level_add_(o.locate, o.side, o.price, -static_cast<std::int64_t>(o.shares));
    orders_.erase(it);
  }

  void replace_(const ItchEvent& e) {
    char side = 'B';
    std::uint16_t locate = e.stock_locate;
    auto it = orders_.find(e.order_ref);
    if (it != orders_.end()) {
      side = it->second.side;
      locate = it->second.locate;
      level_add_(it->second.locate, it->second.side, it->second.price,
                 -static_cast<std::int64_t>(it->second.shares));
      orders_.erase(it);
    }
    orders_[e.new_order_ref] = {locate, side, e.price, e.shares};
    level_add_(locate, side, e.price, static_cast<std::int64_t>(e.shares));
  }

  std::unordered_map<std::uint64_t, Order> orders_;  // ref -> live order
  std::unordered_map<std::uint16_t, Book> books_;    // locate -> aggregated book
};

}  // namespace tradeflow::itch
