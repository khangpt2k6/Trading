#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include "tradeflow/delta.hpp"
#include "tradeflow/matching_engine.hpp"
#include "tradeflow/order.hpp"

namespace tradeflow {

// Deliberately naive "before" engine: ALL symbols share one std::mutex-guarded
// std::queue with a condition_variable, processed by a single worker thread.
// This is the coarse-grained-locking baseline the lock-free engine beats.
class BaselineEngine {
public:
  using DeltaSink = std::function<void(const Order&, const BookDelta&)>;

  BaselineEngine(std::size_t num_symbols, DeltaSink sink)
      : sink_(std::move(sink)) {
    engines_.reserve(num_symbols);
    for (std::size_t i = 0; i < num_symbols; ++i)
      engines_.emplace_back(static_cast<SymbolId>(i));
  }

  ~BaselineEngine() { drain_and_stop(); }

  void start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread([this] { run_(); });
  }

  void submit(const Order& o) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      q_.push(o);
    }
    cv_.notify_one();
  }

  void drain_and_stop() {
    if (!running_) {
      if (thread_.joinable()) thread_.join();
      return;
    }
    {
      std::lock_guard<std::mutex> lk(mu_);
      stop_when_empty_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    running_ = false;
  }

  std::uint64_t processed() const { return processed_; }

private:
  void run_() {
    BookDelta delta;
    for (;;) {
      Order o;
      {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this] { return !q_.empty() || stop_when_empty_; });
        if (q_.empty()) {
          if (stop_when_empty_) break;
          continue;
        }
        o = q_.front();
        q_.pop();
      }
      engines_[o.symbol].submit(o, delta);
      if (sink_) sink_(o, delta);
      ++processed_;
    }
  }

  std::vector<MatchingEngine> engines_;
  DeltaSink sink_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::queue<Order> q_;
  std::thread thread_;
  bool running_ = false;
  bool stop_when_empty_ = false;
  std::uint64_t processed_ = 0;
};

}  // namespace tradeflow
