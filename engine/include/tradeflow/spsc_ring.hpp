#pragma once
#include <atomic>
#include <cstddef>
#include <vector>

namespace tradeflow {

// Single-producer / single-consumer bounded ring buffer.
// capacity is rounded up to a power of two; usable slots == capacity.
template <typename T>
class SpscRing {
public:
  explicit SpscRing(std::size_t capacity) {
    std::size_t cap = 1;
    while (cap < capacity) cap <<= 1;
    buf_.resize(cap);
    mask_ = cap - 1;
  }

  bool push(const T& v) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_acquire);
    if (tail - head > mask_) return false;          // full
    buf_[tail & mask_] = v;
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  bool pop(T& out) {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    if (head == tail) return false;                  // empty
    out = buf_[head & mask_];
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  std::size_t capacity() const { return mask_ + 1; }

private:
  std::vector<T> buf_;
  std::size_t mask_ = 0;
  alignas(64) std::atomic<std::size_t> head_{0};   // consumer cursor
  alignas(64) std::atomic<std::size_t> tail_{0};   // producer cursor
};

}  // namespace tradeflow
