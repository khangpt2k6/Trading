#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tradeflow {

// Bounded multi-producer / single-consumer queue (Dmitry Vyukov design).
// Lock-free: producers claim a slot with a compare_exchange (CAS) loop on
// enqueue_pos_; each cell carries a sequence counter that gates publication.
template <typename T>
class MpscRing {
  struct Cell {
    std::atomic<std::size_t> seq;
    T data;
  };

public:
  explicit MpscRing(std::size_t capacity) {
    std::size_t cap = 1;
    while (cap < capacity) cap <<= 1;
    buf_ = std::vector<Cell>(cap);
    mask_ = cap - 1;
    for (std::size_t i = 0; i <= mask_; ++i)
      buf_[i].seq.store(i, std::memory_order_relaxed);
    enqueue_pos_.store(0, std::memory_order_relaxed);
    dequeue_pos_.store(0, std::memory_order_relaxed);
  }

  bool push(const T& v) {
    Cell* cell;
    std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
    for (;;) {
      cell = &buf_[pos & mask_];
      const std::size_t seq = cell->seq.load(std::memory_order_acquire);
      const std::intptr_t dif =
          static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);
      if (dif == 0) {
        if (enqueue_pos_.compare_exchange_weak(pos, pos + 1,
                                               std::memory_order_relaxed))
          break;                                  // claimed slot
      } else if (dif < 0) {
        return false;                              // full
      } else {
        pos = enqueue_pos_.load(std::memory_order_relaxed);
      }
    }
    cell->data = v;
    cell->seq.store(pos + 1, std::memory_order_release);
    return true;
  }

  // Single-consumer pop. (Uses the generic CAS form; safe for one consumer.)
  bool pop(T& out) {
    Cell* cell;
    std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
    for (;;) {
      cell = &buf_[pos & mask_];
      const std::size_t seq = cell->seq.load(std::memory_order_acquire);
      const std::intptr_t dif = static_cast<std::intptr_t>(seq) -
                                static_cast<std::intptr_t>(pos + 1);
      if (dif == 0) {
        if (dequeue_pos_.compare_exchange_weak(pos, pos + 1,
                                               std::memory_order_relaxed))
          break;
      } else if (dif < 0) {
        return false;                              // empty
      } else {
        pos = dequeue_pos_.load(std::memory_order_relaxed);
      }
    }
    out = cell->data;
    cell->seq.store(pos + mask_ + 1, std::memory_order_release);
    return true;
  }

  std::size_t capacity() const { return mask_ + 1; }

private:
  std::vector<Cell> buf_;
  std::size_t mask_ = 0;
  alignas(64) std::atomic<std::size_t> enqueue_pos_{0};
  alignas(64) std::atomic<std::size_t> dequeue_pos_{0};
};

}  // namespace tradeflow
