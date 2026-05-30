#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include "tradeflow/itch/book_builder.hpp"
#include "tradeflow/itch/itch.hpp"
#include "tradeflow/mpsc_ring.hpp"
#include "tradeflow/order.hpp"  // now_ns()
#include "tradeflow/spin.hpp"

namespace tradeflow::itch {

// Event as it travels through a pipeline ring, carrying a wall-clock ingress
// timestamp for latency measurement.
struct PipeEvent {
  ItchEvent ev;
  std::uint64_t ingress_ns = 0;
};

// Symbol-sharded, lock-free book reconstruction engine. Events route to a shard
// by stock_locate; each shard owns a lock-free MPSC ring (CAS) and a dedicated
// book-building thread. Because every order for a symbol lands on the same
// shard, per-symbol event order is preserved and the result is identical to a
// single-threaded build.
class ShardedBookEngine {
public:
  using Sink = std::function<void(unsigned shard, const PipeEvent&)>;

  ShardedBookEngine(unsigned shards, std::size_t ring_capacity, Sink sink = {})
      : shards_(shards), sink_(std::move(sink)) {
    workers_.reserve(shards);
    for (unsigned i = 0; i < shards; ++i)
      workers_.push_back(std::make_unique<Shard>(ring_capacity));
  }
  ~ShardedBookEngine() { drain_and_stop(); }

  unsigned shard_count() const { return shards_; }
  unsigned shard_of(std::uint16_t locate) const { return locate % shards_; }

  void start() {
    if (running_.exchange(true)) return;
    for (unsigned i = 0; i < shards_; ++i) {
      Shard* sh = workers_[i].get();
      threads_.emplace_back([this, sh, i] { run_(sh, i); });
    }
  }

  // Multi-producer enqueue; routes by symbol. Returns false if the shard ring
  // is full (caller should spin/retry).
  bool submit(const ItchEvent& e) {
    Shard* sh = workers_[shard_of(e.stock_locate)].get();
    PipeEvent pe{e, now_ns()};
    return sh->ring.push(pe);
  }

  void drain_and_stop() {
    if (!running_.load()) { join_(); return; }
    stop_.store(true, std::memory_order_release);
    join_();
    running_.store(false);
  }

  std::uint64_t processed() const {
    std::uint64_t s = 0;
    for (auto& w : workers_) s += w->processed.load(std::memory_order_relaxed);
    return s;
  }

  const BookBuilder& builder(unsigned shard) const { return workers_[shard]->book; }
  std::int64_t best_bid(std::uint16_t locate) const {
    return workers_[shard_of(locate)]->book.best_bid(locate);
  }
  std::int64_t best_ask(std::uint16_t locate) const {
    return workers_[shard_of(locate)]->book.best_ask(locate);
  }

private:
  struct Shard {
    MpscRing<PipeEvent> ring;
    BookBuilder book;
    std::atomic<std::uint64_t> processed{0};
    explicit Shard(std::size_t cap) : ring(cap) {}
  };

  void run_(Shard* sh, unsigned idx) {
    PipeEvent pe;
    for (;;) {
      if (sh->ring.pop(pe)) {
        sh->book.apply(pe.ev);
        if (sink_) sink_(idx, pe);
        sh->processed.fetch_add(1, std::memory_order_relaxed);
      } else if (stop_.load(std::memory_order_acquire)) {
        break;
      } else {
        cpu_relax();
      }
    }
  }

  void join_() {
    for (auto& t : threads_)
      if (t.joinable()) t.join();
    threads_.clear();
  }

  unsigned shards_;
  Sink sink_;
  std::vector<std::unique_ptr<Shard>> workers_;
  std::vector<std::thread> threads_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_{false};
};

// Coarse-locked baseline: all symbols share one std::mutex-guarded queue drained
// by a single book-building thread. This is the "before" the sharded lock-free
// engine is compared against.
class SequentialBookEngine {
public:
  using Sink = std::function<void(unsigned shard, const PipeEvent&)>;

  explicit SequentialBookEngine(Sink sink = {}) : sink_(std::move(sink)) {}
  ~SequentialBookEngine() { drain_and_stop(); }

  void start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread([this] { run_(); });
  }

  void submit(const ItchEvent& e) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      q_.push(PipeEvent{e, now_ns()});
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
      stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    running_ = false;
  }

  std::uint64_t processed() const { return processed_; }
  const BookBuilder& builder() const { return book_; }

private:
  void run_() {
    for (;;) {
      PipeEvent pe;
      {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this] { return !q_.empty() || stop_; });
        if (q_.empty()) {
          if (stop_) break;
          continue;
        }
        pe = q_.front();
        q_.pop();
      }
      book_.apply(pe.ev);
      if (sink_) sink_(0, pe);
      ++processed_;
    }
  }

  Sink sink_;
  BookBuilder book_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::queue<PipeEvent> q_;
  std::thread thread_;
  bool running_ = false;
  bool stop_ = false;
  std::uint64_t processed_ = 0;
};

}  // namespace tradeflow::itch
