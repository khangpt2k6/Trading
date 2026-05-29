#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include "tradeflow/delta.hpp"
#include "tradeflow/matching_engine.hpp"
#include "tradeflow/mpsc_ring.hpp"
#include "tradeflow/order.hpp"
#include "tradeflow/spin.hpp"

namespace tradeflow {

// Owns a lock-free MPSC ingress ring and a dedicated matching thread for one
// symbol. Producers call submit(); the thread pops, matches, and invokes the
// delta callback with the source order and resulting delta. The callback runs
// on the matching thread. (The source order is passed so consumers can read
// ts_ingress for latency measurement.)
class SymbolWorker {
public:
  using DeltaSink = std::function<void(const Order&, const BookDelta&)>;

  SymbolWorker(SymbolId symbol, std::size_t ring_capacity, DeltaSink sink)
      : engine_(symbol), ring_(ring_capacity), sink_(std::move(sink)) {}

  ~SymbolWorker() { drain_and_stop(); }

  void start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { run_(); });
  }

  // Multi-producer enqueue; returns false if the ring is full.
  bool submit(const Order& o) { return ring_.push(o); }

  // Wait until the ring is empty, then stop the thread.
  void drain_and_stop() {
    if (!running_.load()) {
      if (thread_.joinable()) thread_.join();
      return;
    }
    stop_when_empty_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    running_.store(false);
  }

  std::uint64_t processed() const {
    return processed_.load(std::memory_order_relaxed);
  }

private:
  void run_() {
    Order o;
    BookDelta delta;
    for (;;) {
      if (ring_.pop(o)) {
        engine_.submit(o, delta);
        if (sink_) sink_(o, delta);
        processed_.fetch_add(1, std::memory_order_relaxed);
      } else if (stop_when_empty_.load(std::memory_order_acquire)) {
        break;
      } else {
        cpu_relax();  // ring empty: pause to free the core while we wait
      }
    }
  }

  MatchingEngine engine_;
  MpscRing<Order> ring_;
  DeltaSink sink_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_when_empty_{false};
  std::atomic<std::uint64_t> processed_{0};
};

}  // namespace tradeflow
