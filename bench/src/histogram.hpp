#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace tradeflow::bench {

// Collects raw latency samples (ns) and computes percentiles by sorting.
class Histogram {
public:
  void reserve(std::size_t n) { samples_.reserve(n); }
  void add(std::uint64_t v) { samples_.push_back(v); }

  void merge(const Histogram& other) {
    samples_.insert(samples_.end(), other.samples_.begin(),
                    other.samples_.end());
  }

  std::size_t count() const { return samples_.size(); }

  // p in [0,1]. Must call sort() first.
  std::uint64_t percentile(double p) const {
    if (samples_.empty()) return 0;
    std::size_t idx = static_cast<std::size_t>(p * (samples_.size() - 1));
    return samples_[idx];
  }

  void sort() { std::sort(samples_.begin(), samples_.end()); }

private:
  std::vector<std::uint64_t> samples_;
};

}  // namespace tradeflow::bench
