# TradeFlow Phase 0 + Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Scaffold the repo and build the lock-free, per-symbol-threaded C++ matching engine plus a benchmark harness that proves resume bullet 1 (240K+ orders/sec, ~60% P99 latency reduction vs a coarse-locked baseline).

**Architecture:** A header-focused C++17 core (`engine/`) provides a hand-rolled SPSC ring, a Vyukov bounded MPSC ring (CAS), a price-time-priority `OrderBook`, and a `MatchingEngine`. A `SymbolWorker` runs one matching thread per symbol fed by its own MPSC ring. A baseline engine uses a single mutex+condvar blocking queue and one worker thread. The benchmark drives both with identical synthetic flow and reports throughput + latency percentiles.

**Tech Stack:** C++17, CMake (>=3.20) + FetchContent, GoogleTest, pthreads, ThreadSanitizer, WSL2 Ubuntu 24.04. (uWebSockets / FlatBuffers / Emscripten arrive in the Phase 2 and 3 plans.)

---

## File Structure

- `scripts/setup.sh` - installs toolchain in WSL.
- `CMakeLists.txt` - root build: options, FetchContent(googletest), subdirs.
- `engine/include/tradeflow/order.hpp` - core types (Order, Trade, Side, etc.) + `now_ns()`.
- `engine/include/tradeflow/delta.hpp` - LevelUpdate, BookDelta.
- `engine/include/tradeflow/spsc_ring.hpp` - SPSC ring buffer.
- `engine/include/tradeflow/mpsc_ring.hpp` - Vyukov MPSC ring (CAS).
- `engine/include/tradeflow/order_book.hpp` - price-time-priority book.
- `engine/include/tradeflow/matching_engine.hpp` - per-symbol engine wrapper.
- `engine/include/tradeflow/symbol_worker.hpp` - lock-free per-symbol thread runtime.
- `engine/include/tradeflow/baseline_engine.hpp` - coarse-locked baseline.
- `engine/CMakeLists.txt` - INTERFACE library `tradeflow_engine`.
- `tests/CMakeLists.txt` + `tests/*.cpp` - GoogleTest suites.
- `bench/CMakeLists.txt` + `bench/src/bench_main.cpp` + `bench/src/workload.hpp` + `bench/src/histogram.hpp` - benchmark.
- `.github/workflows/ci.yml` - build + test on ubuntu-latest.
- `README.md` - real project README, updated with measured numbers.

All `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && <cmd>"` commands run inside WSL. Commits/pushes can run from Windows git or WSL git; use the existing author `Khang Tuáº¥n Phan` and NEVER add Claude attribution.

---

## Phase 0: Scaffold

### Task 0.1: Toolchain setup script

**Files:**
- Create: `scripts/setup.sh`

- [ ] **Step 1: Write the setup script**

```bash
#!/usr/bin/env bash
# Installs the TradeFlow build toolchain inside WSL2 Ubuntu.
set -euo pipefail

echo "==> Updating apt and installing build tools"
sudo apt-get update -y
sudo apt-get install -y build-essential cmake ninja-build git pkg-config curl ca-certificates

echo "==> Toolchain versions"
g++ --version | head -1
cmake --version | head -1
ninja --version || true

echo "==> Done. Native build toolchain ready."
echo "    (Node.js, Emscripten, and flatc are installed in later phases.)"
```

- [ ] **Step 2: Make it executable and run it**

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && chmod +x scripts/setup.sh && ./scripts/setup.sh"`
Expected: ends with "Native build toolchain ready." and prints g++ >= 13 and cmake >= 3.28.

- [ ] **Step 3: Verify compiler works**

Run: `wsl.exe -d Ubuntu -- bash -lc "echo 'int main(){return 0;}' | g++ -std=c++17 -x c++ - -o /tmp/t && echo COMPILE_OK"`
Expected: `COMPILE_OK`

- [ ] **Step 4: Commit**

```bash
git add scripts/setup.sh
git commit -m "Add WSL toolchain setup script"
git push origin main
```

---

### Task 0.2: CMake skeleton + engine interface library + README

**Files:**
- Create: `CMakeLists.txt`, `engine/CMakeLists.txt`, `engine/include/tradeflow/version.hpp`, `README.md`

- [ ] **Step 1: Write root CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.20)
project(TradeFlow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

option(TF_BUILD_TESTS "Build tests" ON)
option(TF_BUILD_BENCH "Build benchmark" ON)
option(TF_SANITIZE_THREAD "Build with ThreadSanitizer" OFF)

if(TF_SANITIZE_THREAD)
  add_compile_options(-fsanitize=thread -g -O1)
  add_link_options(-fsanitize=thread)
endif()

find_package(Threads REQUIRED)

add_subdirectory(engine)

if(TF_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  add_subdirectory(tests)
endif()

if(TF_BUILD_BENCH)
  add_subdirectory(bench)
endif()
```

- [ ] **Step 2: Write engine/CMakeLists.txt**

```cmake
add_library(tradeflow_engine INTERFACE)
target_include_directories(tradeflow_engine INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(tradeflow_engine INTERFACE Threads::Threads)
target_compile_features(tradeflow_engine INTERFACE cxx_std_17)
```

- [ ] **Step 3: Write engine/include/tradeflow/version.hpp**

```cpp
#pragma once
namespace tradeflow {
inline constexpr const char* kVersion = "0.1.0";
}
```

- [ ] **Step 4: Write README.md**

```markdown
# TradeFlow - Order Matching Engine

A low-latency order matching engine in C++ with a lock-free, thread-per-symbol
core, a WebSocket delta-streaming layer (FlatBuffers, zero-copy), and a
browser client that reconstructs the live order book in WebAssembly and renders
it with WebGL.

## Highlights

- Hand-rolled lock-free queues (Vyukov bounded MPSC, CAS-based) with a dedicated
  matching thread per market symbol.
- Benchmark harness comparing the lock-free engine against a coarse-locked
  baseline (single mutex + blocking queue).
- Incremental order-book delta streaming over WebSockets with zero-copy
  FlatBuffers serialization.
- Shared C++ order-book code compiled to WebAssembly so the browser client
  reconstructs the book identically from the delta stream, rendered with WebGL.

## Build (WSL2 / Linux)

```bash
./scripts/setup.sh                 # one-time toolchain install
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Benchmark

```bash
./build/bench/tradeflow_bench --symbols 8 --orders 2000000
```

Measured numbers are recorded in [docs/benchmarks.md](docs/benchmarks.md).

## Status

- [x] Phase 1: lock-free matching engine + benchmark
- [ ] Phase 2: WebSocket + FlatBuffers streaming
- [ ] Phase 3: WASM + WebGL client
```

- [ ] **Step 5: Configure to verify CMake parses**

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake -S . -B build -DTF_BUILD_TESTS=OFF -DTF_BUILD_BENCH=OFF"`
Expected: ends with "Generating done" / "Build files have been written".

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt engine/CMakeLists.txt engine/include/tradeflow/version.hpp README.md
git commit -m "Add CMake skeleton, engine interface lib, and README"
git push origin main
```

---

### Task 0.3: GoogleTest wiring + sanity test

**Files:**
- Create: `tests/CMakeLists.txt`, `tests/sanity_test.cpp`

- [ ] **Step 1: Write tests/CMakeLists.txt**

```cmake
include(GoogleTest)

function(tf_add_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE tradeflow_engine GTest::gtest_main)
  gtest_discover_tests(${name})
endfunction()

tf_add_test(sanity_test sanity_test.cpp)
```

- [ ] **Step 2: Write tests/sanity_test.cpp**

```cpp
#include <gtest/gtest.h>
#include "tradeflow/version.hpp"

TEST(Sanity, VersionIsSet) {
  EXPECT_STREQ(tradeflow::kVersion, "0.1.0");
}
```

- [ ] **Step 3: Build and run**

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake -S . -B build -DTF_BUILD_BENCH=OFF && cmake --build build -j && ctest --test-dir build --output-on-failure"`
Expected: `100% tests passed, 0 tests failed`

- [ ] **Step 4: Commit**

```bash
git add tests/CMakeLists.txt tests/sanity_test.cpp
git commit -m "Wire up GoogleTest with sanity test"
git push origin main
```

---

### Task 0.4: GitHub Actions CI

**Files:**
- Create: `.github/workflows/ci.yml`

- [ ] **Step 1: Write the CI workflow**

```yaml
name: CI
on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  build-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install toolchain
        run: sudo apt-get update -y && sudo apt-get install -y build-essential cmake ninja-build
      - name: Configure
        run: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
      - name: Build
        run: cmake --build build -j
      - name: Test
        run: ctest --test-dir build --output-on-failure
```

- [ ] **Step 2: Commit and push (CI runs on push)**

```bash
git add .github/workflows/ci.yml
git commit -m "Add GitHub Actions CI (build + test)"
git push origin main
```

- [ ] **Step 3: Verify CI is green**

Run: `gh run list --limit 1` then `gh run watch` (or check the Actions tab).
Expected: the latest run concludes `success`.

---

## Phase 1: Lock-free matching engine + benchmark

### Task 1.1: SPSC ring buffer (TDD)

**Files:**
- Create: `engine/include/tradeflow/spsc_ring.hpp`
- Test: `tests/spsc_ring_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/spsc_ring_test.cpp`:
```cpp
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "tradeflow/spsc_ring.hpp"

using tradeflow::SpscRing;

TEST(SpscRing, PushPopSingle) {
  SpscRing<int> q(4);
  int out = -1;
  EXPECT_FALSE(q.pop(out));        // empty
  EXPECT_TRUE(q.push(42));
  EXPECT_TRUE(q.pop(out));
  EXPECT_EQ(out, 42);
  EXPECT_FALSE(q.pop(out));        // empty again
}

TEST(SpscRing, FullRejectsPush) {
  SpscRing<int> q(2);              // capacity 2
  EXPECT_TRUE(q.push(1));
  EXPECT_TRUE(q.push(2));
  EXPECT_FALSE(q.push(3));         // full
  int out = 0;
  EXPECT_TRUE(q.pop(out));
  EXPECT_EQ(out, 1);
  EXPECT_TRUE(q.push(3));          // room now
}

TEST(SpscRing, FifoOrderUnderConcurrency) {
  constexpr int N = 1'000'000;
  SpscRing<int> q(1024);
  std::vector<int> got;
  got.reserve(N);
  std::thread consumer([&] {
    int v, count = 0;
    while (count < N) {
      if (q.pop(v)) { got.push_back(v); ++count; }
    }
  });
  for (int i = 0; i < N; ++i) {
    while (!q.push(i)) { /* spin */ }
  }
  consumer.join();
  ASSERT_EQ(static_cast<int>(got.size()), N);
  for (int i = 0; i < N; ++i) ASSERT_EQ(got[i], i);  // FIFO preserved
}
```

- [ ] **Step 2: Add the test target and run to verify it fails**

Add to `tests/CMakeLists.txt`:
```cmake
tf_add_test(spsc_ring_test spsc_ring_test.cpp)
```

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake -S . -B build -DTF_BUILD_BENCH=OFF && cmake --build build -j 2>&1 | tail -5"`
Expected: FAIL - `fatal error: tradeflow/spsc_ring.hpp: No such file or directory`

- [ ] **Step 3: Implement the SPSC ring**

`engine/include/tradeflow/spsc_ring.hpp`:
```cpp
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
```

Note: capacity 2 rounds to 2; with the `> mask_` test, usable slots == capacity. (For capacity 2, mask_=1, full when tail-head>1, i.e. 2 items held - matches the test.)

- [ ] **Step 4: Build and run tests**

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake --build build -j && ctest --test-dir build -R SpscRing --output-on-failure"`
Expected: `100% tests passed`

- [ ] **Step 5: Commit**

```bash
git add engine/include/tradeflow/spsc_ring.hpp tests/spsc_ring_test.cpp tests/CMakeLists.txt
git commit -m "Add lock-free SPSC ring buffer with tests"
git push origin main
```

---

### Task 1.2: Vyukov MPSC ring buffer (CAS) (TDD)

**Files:**
- Create: `engine/include/tradeflow/mpsc_ring.hpp`
- Test: `tests/mpsc_ring_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/mpsc_ring_test.cpp`:
```cpp
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include "tradeflow/mpsc_ring.hpp"

using tradeflow::MpscRing;

TEST(MpscRing, PushPopSingle) {
  MpscRing<int> q(4);
  int out = -1;
  EXPECT_FALSE(q.pop(out));
  EXPECT_TRUE(q.push(7));
  EXPECT_TRUE(q.pop(out));
  EXPECT_EQ(out, 7);
}

TEST(MpscRing, FullRejects) {
  MpscRing<int> q(2);
  EXPECT_TRUE(q.push(1));
  EXPECT_TRUE(q.push(2));
  EXPECT_FALSE(q.push(3));   // full
}

TEST(MpscRing, MultiProducerNoLossNoDup) {
  constexpr int kProducers = 4;
  constexpr int kPerProducer = 250'000;
  constexpr int kTotal = kProducers * kPerProducer;
  MpscRing<long> q(4096);

  std::vector<int> seen(kTotal, 0);
  std::atomic<bool> done{false};
  std::atomic<int> consumed{0};

  std::thread consumer([&] {
    long v;
    while (consumed.load(std::memory_order_relaxed) < kTotal) {
      if (q.pop(v)) {
        ++seen[static_cast<std::size_t>(v)];
        consumed.fetch_add(1, std::memory_order_relaxed);
      }
    }
    done.store(true);
  });

  std::vector<std::thread> producers;
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&, p] {
      const long base = static_cast<long>(p) * kPerProducer;
      for (int i = 0; i < kPerProducer; ++i) {
        while (!q.push(base + i)) { /* spin until room */ }
      }
    });
  }
  for (auto& t : producers) t.join();
  consumer.join();

  EXPECT_EQ(consumed.load(), kTotal);
  for (int i = 0; i < kTotal; ++i) {
    ASSERT_EQ(seen[i], 1) << "value " << i << " seen " << seen[i] << " times";
  }
}
```

- [ ] **Step 2: Add target and run to verify it fails**

Add to `tests/CMakeLists.txt`:
```cmake
tf_add_test(mpsc_ring_test mpsc_ring_test.cpp)
```

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake -S . -B build -DTF_BUILD_BENCH=OFF && cmake --build build -j 2>&1 | tail -5"`
Expected: FAIL - `mpsc_ring.hpp: No such file or directory`

- [ ] **Step 3: Implement the Vyukov bounded MPSC ring (CAS on enqueue)**

`engine/include/tradeflow/mpsc_ring.hpp`:
```cpp
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
```

- [ ] **Step 4: Build and run tests**

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake --build build -j && ctest --test-dir build -R MpscRing --output-on-failure"`
Expected: `100% tests passed`

- [ ] **Step 5: Verify correctness under ThreadSanitizer**

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake -S . -B build-tsan -DTF_SANITIZE_THREAD=ON -DTF_BUILD_BENCH=OFF && cmake --build build-tsan -j --target mpsc_ring_test spsc_ring_test && ctest --test-dir build-tsan -R Ring --output-on-failure"`
Expected: `100% tests passed`, no ThreadSanitizer "data race" warnings in output.

- [ ] **Step 6: Commit**

```bash
git add engine/include/tradeflow/mpsc_ring.hpp tests/mpsc_ring_test.cpp tests/CMakeLists.txt
git commit -m "Add lock-free Vyukov MPSC ring buffer (CAS) with stress + TSan tests"
git push origin main
```

---

### Task 1.3: Core types and clock

**Files:**
- Create: `engine/include/tradeflow/order.hpp`, `engine/include/tradeflow/delta.hpp`
- Test: `tests/types_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/types_test.cpp`:
```cpp
#include <gtest/gtest.h>
#include "tradeflow/order.hpp"
#include "tradeflow/delta.hpp"

using namespace tradeflow;

TEST(Types, OrderDefaults) {
  Order o{};
  o.id = 1; o.symbol = 0; o.side = Side::Buy; o.type = OrderType::Limit;
  o.price = 100; o.qty = 10;
  EXPECT_EQ(o.qty, 10);
  EXPECT_EQ(o.side, Side::Buy);
}

TEST(Types, NowMonotonic) {
  const auto a = now_ns();
  const auto b = now_ns();
  EXPECT_GE(b, a);
}

TEST(Types, LevelUpdateRemoveSemantics) {
  LevelUpdate u{Side::Sell, 101, 0};  // qty 0 means level removed
  EXPECT_EQ(u.qty, 0);
}
```

- [ ] **Step 2: Add target and run to verify it fails**

Add to `tests/CMakeLists.txt`:
```cmake
tf_add_test(types_test types_test.cpp)
```

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake --build build -j 2>&1 | tail -5"`
Expected: FAIL - `order.hpp: No such file or directory`

- [ ] **Step 3: Implement the types**

`engine/include/tradeflow/order.hpp`:
```cpp
#pragma once
#include <chrono>
#include <cstdint>

namespace tradeflow {

using Price = std::int64_t;     // price in integer ticks
using Qty = std::int64_t;
using OrderId = std::uint64_t;
using SymbolId = std::uint32_t;
using Seq = std::uint64_t;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };
enum class OrderType : std::uint8_t { Limit = 0, Market = 1, Cancel = 2 };

struct Order {
  OrderId id = 0;
  SymbolId symbol = 0;
  Side side = Side::Buy;
  OrderType type = OrderType::Limit;
  Price price = 0;            // ignored for Market and Cancel
  Qty qty = 0;               // for Cancel, target order id is in `id`
  std::uint64_t ts_ingress = 0;  // ns timestamp, set when submitted
};

struct Trade {
  Price price = 0;
  Qty qty = 0;
  OrderId taker = 0;
  OrderId maker = 0;
  std::uint64_t ts = 0;
};

// Monotonic nanosecond clock for latency measurement.
inline std::uint64_t now_ns() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

}  // namespace tradeflow
```

`engine/include/tradeflow/delta.hpp`:
```cpp
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
```

- [ ] **Step 4: Build and run tests**

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake --build build -j && ctest --test-dir build -R Types --output-on-failure"`
Expected: `100% tests passed`

- [ ] **Step 5: Commit**

```bash
git add engine/include/tradeflow/order.hpp engine/include/tradeflow/delta.hpp tests/types_test.cpp tests/CMakeLists.txt
git commit -m "Add core order/trade/delta types and monotonic clock"
git push origin main
```

---

### Task 1.4: OrderBook - limit orders and cancel (TDD)

**Files:**
- Create: `engine/include/tradeflow/order_book.hpp`
- Test: `tests/order_book_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/order_book_test.cpp`:
```cpp
#include <gtest/gtest.h>
#include <vector>
#include "tradeflow/order_book.hpp"

using namespace tradeflow;

static Order limit(OrderId id, Side s, Price p, Qty q) {
  Order o{}; o.id = id; o.side = s; o.type = OrderType::Limit; o.price = p; o.qty = q;
  return o;
}

TEST(OrderBook, RestingNoCrossUpdatesLevels) {
  OrderBook ob;
  std::vector<Trade> trades; std::vector<LevelUpdate> ch;
  ob.add_limit(limit(1, Side::Buy, 100, 10), trades, ch);
  EXPECT_TRUE(trades.empty());
  ASSERT_EQ(ch.size(), 1u);
  EXPECT_EQ(ch[0].side, Side::Buy);
  EXPECT_EQ(ch[0].price, 100);
  EXPECT_EQ(ch[0].qty, 10);
  EXPECT_EQ(ob.best_bid(), 100);
}

TEST(OrderBook, FullCrossGeneratesTradeAndRemovesLevel) {
  OrderBook ob;
  std::vector<Trade> trades; std::vector<LevelUpdate> ch;
  ob.add_limit(limit(1, Side::Sell, 100, 10), trades, ch);  // rest ask
  trades.clear(); ch.clear();
  ob.add_limit(limit(2, Side::Buy, 100, 10), trades, ch);   // crosses fully
  ASSERT_EQ(trades.size(), 1u);
  EXPECT_EQ(trades[0].price, 100);
  EXPECT_EQ(trades[0].qty, 10);
  EXPECT_EQ(trades[0].taker, 2u);
  EXPECT_EQ(trades[0].maker, 1u);
  // ask level 100 now removed -> qty 0
  bool ask_removed = false;
  for (auto& u : ch) if (u.side == Side::Sell && u.price == 100 && u.qty == 0) ask_removed = true;
  EXPECT_TRUE(ask_removed);
  EXPECT_EQ(ob.best_ask(), 0);     // 0 == no ask
  EXPECT_EQ(ob.best_bid(), 0);     // taker fully filled, nothing rested
}

TEST(OrderBook, PartialFillRestsRemainder) {
  OrderBook ob;
  std::vector<Trade> trades; std::vector<LevelUpdate> ch;
  ob.add_limit(limit(1, Side::Sell, 100, 4), trades, ch);
  trades.clear(); ch.clear();
  ob.add_limit(limit(2, Side::Buy, 100, 10), trades, ch);  // buys 4, rests 6
  ASSERT_EQ(trades.size(), 1u);
  EXPECT_EQ(trades[0].qty, 4);
  EXPECT_EQ(ob.best_bid(), 100);   // remainder rested as bid
  // bid level 100 should reflect qty 6
  Qty bid_qty = 0;
  for (auto& u : ch) if (u.side == Side::Buy && u.price == 100) bid_qty = u.qty;
  EXPECT_EQ(bid_qty, 6);
}

TEST(OrderBook, PriceTimePriorityFifo) {
  OrderBook ob;
  std::vector<Trade> trades; std::vector<LevelUpdate> ch;
  ob.add_limit(limit(1, Side::Buy, 100, 5), trades, ch);   // first in queue
  ob.add_limit(limit(2, Side::Buy, 100, 5), trades, ch);   // second
  trades.clear(); ch.clear();
  ob.add_limit(limit(3, Side::Sell, 100, 5), trades, ch);  // hits order 1 first
  ASSERT_EQ(trades.size(), 1u);
  EXPECT_EQ(trades[0].maker, 1u);   // FIFO: oldest order matched first
}

TEST(OrderBook, CancelRemovesResting) {
  OrderBook ob;
  std::vector<Trade> trades; std::vector<LevelUpdate> ch;
  ob.add_limit(limit(1, Side::Buy, 100, 5), trades, ch);
  ch.clear();
  EXPECT_TRUE(ob.cancel(1, ch));
  EXPECT_EQ(ob.best_bid(), 0);
  ASSERT_FALSE(ch.empty());
  EXPECT_EQ(ch.back().qty, 0);      // level removed
  EXPECT_FALSE(ob.cancel(1, ch));   // already gone
}
```

- [ ] **Step 2: Add target and run to verify it fails**

Add to `tests/CMakeLists.txt`:
```cmake
tf_add_test(order_book_test order_book_test.cpp)
```

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake --build build -j 2>&1 | tail -5"`
Expected: FAIL - `order_book.hpp: No such file or directory`

- [ ] **Step 3: Implement OrderBook (limit + cancel + market hook)**

`engine/include/tradeflow/order_book.hpp`:
```cpp
#pragma once
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
      remaining = cross_<std::less<Price>>(asks_, Side::Sell, o, remaining, true,
                                           trades);
    else
      remaining = cross_<std::greater<Price>>(bids_, Side::Buy, o, remaining,
                                              true, trades);
    if (remaining > 0) rest_(o, remaining);
    emit_touched_(changes);
  }

  void add_market(const Order& o, std::vector<Trade>& trades,
                  std::vector<LevelUpdate>& changes) {
    touched_buy_.clear();
    touched_sell_.clear();
    Qty remaining = o.qty;
    if (o.side == Side::Buy)
      cross_<std::less<Price>>(asks_, Side::Sell, o, remaining, false, trades);
    else
      cross_<std::greater<Price>>(bids_, Side::Buy, o, remaining, false, trades);
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
  template <typename Cmp, typename BookT>
  Qty cross_(BookT& book, Side maker_side, const Order& taker, Qty remaining,
             bool limited, std::vector<Trade>& trades) {
    Cmp cmp;
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
```

(Needs `#include <algorithm>` for `std::min`; add it at the top with the other includes.)

- [ ] **Step 4: Add the missing include and build/run**

Add `#include <algorithm>` to the include block of `order_book.hpp`.

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake --build build -j && ctest --test-dir build -R OrderBook --output-on-failure"`
Expected: `100% tests passed`

- [ ] **Step 5: Commit**

```bash
git add engine/include/tradeflow/order_book.hpp tests/order_book_test.cpp tests/CMakeLists.txt
git commit -m "Add price-time-priority OrderBook (limit, cancel, market) with tests"
git push origin main
```

---

### Task 1.5: OrderBook - market order tests (TDD)

**Files:**
- Test: `tests/order_book_market_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/order_book_market_test.cpp`:
```cpp
#include <gtest/gtest.h>
#include <vector>
#include "tradeflow/order_book.hpp"

using namespace tradeflow;

static Order lim(OrderId id, Side s, Price p, Qty q) {
  Order o{}; o.id=id; o.side=s; o.type=OrderType::Limit; o.price=p; o.qty=q; return o;
}
static Order mkt(OrderId id, Side s, Qty q) {
  Order o{}; o.id=id; o.side=s; o.type=OrderType::Market; o.qty=q; return o;
}

TEST(OrderBookMarket, SweepsMultipleLevels) {
  OrderBook ob;
  std::vector<Trade> tr; std::vector<LevelUpdate> ch;
  ob.add_limit(lim(1, Side::Sell, 100, 5), tr, ch);
  ob.add_limit(lim(2, Side::Sell, 101, 5), tr, ch);
  tr.clear(); ch.clear();
  ob.add_market(mkt(3, Side::Buy, 8), tr, ch);  // takes 5@100 then 3@101
  ASSERT_EQ(tr.size(), 2u);
  EXPECT_EQ(tr[0].price, 100); EXPECT_EQ(tr[0].qty, 5);
  EXPECT_EQ(tr[1].price, 101); EXPECT_EQ(tr[1].qty, 3);
  EXPECT_EQ(ob.best_ask(), 101);  // 2 remaining at 101
}

TEST(OrderBookMarket, DiscardsRemainderWhenBookEmpty) {
  OrderBook ob;
  std::vector<Trade> tr; std::vector<LevelUpdate> ch;
  ob.add_limit(lim(1, Side::Sell, 100, 3), tr, ch);
  tr.clear(); ch.clear();
  ob.add_market(mkt(2, Side::Buy, 10), tr, ch);  // only 3 available
  ASSERT_EQ(tr.size(), 1u);
  EXPECT_EQ(tr[0].qty, 3);
  EXPECT_EQ(ob.best_ask(), 0);   // book emptied
  EXPECT_EQ(ob.best_bid(), 0);   // market remainder NOT rested
}
```

- [ ] **Step 2: Add target and run to verify it fails (then passes)**

Add to `tests/CMakeLists.txt`:
```cmake
tf_add_test(order_book_market_test order_book_market_test.cpp)
```

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake --build build -j && ctest --test-dir build -R OrderBookMarket --output-on-failure"`
Expected: `100% tests passed` (the market path implemented in Task 1.4 satisfies these).

- [ ] **Step 3: Commit**

```bash
git add tests/order_book_market_test.cpp tests/CMakeLists.txt
git commit -m "Add market-order matching tests"
git push origin main
```

---

### Task 1.6: MatchingEngine (per-symbol wrapper) (TDD)

**Files:**
- Create: `engine/include/tradeflow/matching_engine.hpp`
- Test: `tests/matching_engine_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/matching_engine_test.cpp`:
```cpp
#include <gtest/gtest.h>
#include "tradeflow/matching_engine.hpp"

using namespace tradeflow;

static Order lim(OrderId id, SymbolId sym, Side s, Price p, Qty q) {
  Order o{}; o.id=id; o.symbol=sym; o.side=s; o.type=OrderType::Limit; o.price=p; o.qty=q; return o;
}

TEST(MatchingEngine, ProducesDeltaWithIncrementingSeq) {
  MatchingEngine eng(7);
  BookDelta d1, d2;
  eng.submit(lim(1, 7, Side::Buy, 100, 5), d1);
  eng.submit(lim(2, 7, Side::Sell, 100, 5), d2);
  EXPECT_EQ(d1.symbol, 7u);
  EXPECT_EQ(d1.seq, 1u);
  EXPECT_EQ(d2.seq, 2u);
  EXPECT_EQ(d2.trades.size(), 1u);
}

TEST(MatchingEngine, RoutesByOrderType) {
  MatchingEngine eng(0);
  BookDelta d;
  eng.submit(lim(1, 0, Side::Sell, 100, 5), d);
  d.clear();
  Order m{}; m.id=2; m.symbol=0; m.side=Side::Buy; m.type=OrderType::Market; m.qty=5;
  eng.submit(m, d);
  EXPECT_EQ(d.trades.size(), 1u);
  d.clear();
  Order c{}; c.id=3; c.symbol=0; c.type=OrderType::Cancel;  // nothing to cancel
  eng.submit(c, d);
  EXPECT_TRUE(d.trades.empty());
}
```

- [ ] **Step 2: Add target and run to verify it fails**

Add to `tests/CMakeLists.txt`:
```cmake
tf_add_test(matching_engine_test matching_engine_test.cpp)
```

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake --build build -j 2>&1 | tail -5"`
Expected: FAIL - `matching_engine.hpp: No such file or directory`

- [ ] **Step 3: Implement MatchingEngine**

`engine/include/tradeflow/matching_engine.hpp`:
```cpp
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
```

- [ ] **Step 4: Build and run tests**

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake --build build -j && ctest --test-dir build -R MatchingEngine --output-on-failure"`
Expected: `100% tests passed`

- [ ] **Step 5: Commit**

```bash
git add engine/include/tradeflow/matching_engine.hpp tests/matching_engine_test.cpp tests/CMakeLists.txt
git commit -m "Add per-symbol MatchingEngine wrapper with tests"
git push origin main
```

---

### Task 1.7: SymbolWorker - lock-free per-symbol thread runtime (TDD)

**Files:**
- Create: `engine/include/tradeflow/symbol_worker.hpp`
- Test: `tests/symbol_worker_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/symbol_worker_test.cpp`:
```cpp
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include "tradeflow/symbol_worker.hpp"

using namespace tradeflow;

TEST(SymbolWorker, ProcessesSubmittedOrders) {
  std::atomic<long> matched_qty{0};
  SymbolWorker w(0, 1024, [&](const Order&, const BookDelta& d) {
    for (auto& t : d.trades) matched_qty.fetch_add(t.qty, std::memory_order_relaxed);
  });
  w.start();

  Order s{}; s.id=1; s.symbol=0; s.side=Side::Sell; s.type=OrderType::Limit; s.price=100; s.qty=10;
  Order b{}; b.id=2; b.symbol=0; b.side=Side::Buy;  b.type=OrderType::Limit; b.price=100; b.qty=10;
  while (!w.submit(s)) {}
  while (!w.submit(b)) {}

  w.drain_and_stop();
  EXPECT_EQ(matched_qty.load(), 10);
  EXPECT_EQ(w.processed(), 2u);
}
```

- [ ] **Step 2: Add target and run to verify it fails**

Add to `tests/CMakeLists.txt`:
```cmake
tf_add_test(symbol_worker_test symbol_worker_test.cpp)
```

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake --build build -j 2>&1 | tail -5"`
Expected: FAIL - `symbol_worker.hpp: No such file or directory`

- [ ] **Step 3: Implement SymbolWorker**

`engine/include/tradeflow/symbol_worker.hpp`:
```cpp
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include "tradeflow/delta.hpp"
#include "tradeflow/matching_engine.hpp"
#include "tradeflow/mpsc_ring.hpp"
#include "tradeflow/order.hpp"

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
      }
      // else: spin (busy-poll for lowest latency)
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
```

- [ ] **Step 4: Build and run tests (including under TSan)**

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake --build build -j && ctest --test-dir build -R SymbolWorker --output-on-failure"`
Expected: `100% tests passed`

Run (TSan): `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake --build build-tsan -j --target symbol_worker_test && ctest --test-dir build-tsan -R SymbolWorker --output-on-failure"`
Expected: `100% tests passed`, no data-race reports.

- [ ] **Step 5: Commit**

```bash
git add engine/include/tradeflow/symbol_worker.hpp tests/symbol_worker_test.cpp tests/CMakeLists.txt
git commit -m "Add lock-free per-symbol worker thread runtime with tests"
git push origin main
```

---

### Task 1.8: Baseline coarse-locked engine (TDD)

**Files:**
- Create: `engine/include/tradeflow/baseline_engine.hpp`
- Test: `tests/baseline_engine_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/baseline_engine_test.cpp`:
```cpp
#include <gtest/gtest.h>
#include <atomic>
#include "tradeflow/baseline_engine.hpp"

using namespace tradeflow;

TEST(BaselineEngine, ProcessesAcrossSymbolsWithSingleLock) {
  std::atomic<long> matched{0};
  BaselineEngine eng(/*num_symbols=*/2, [&](const Order&, const BookDelta& d) {
    for (auto& t : d.trades) matched.fetch_add(t.qty, std::memory_order_relaxed);
  });
  eng.start();

  auto lim = [](OrderId id, SymbolId sym, Side s, Price p, Qty q) {
    Order o{}; o.id=id; o.symbol=sym; o.side=s; o.type=OrderType::Limit; o.price=p; o.qty=q; return o;
  };
  eng.submit(lim(1, 0, Side::Sell, 100, 5));
  eng.submit(lim(2, 0, Side::Buy, 100, 5));
  eng.submit(lim(3, 1, Side::Sell, 50, 7));
  eng.submit(lim(4, 1, Side::Buy, 50, 7));

  eng.drain_and_stop();
  EXPECT_EQ(matched.load(), 12);   // 5 + 7
}
```

- [ ] **Step 2: Add target and run to verify it fails**

Add to `tests/CMakeLists.txt`:
```cmake
tf_add_test(baseline_engine_test baseline_engine_test.cpp)
```

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake --build build -j 2>&1 | tail -5"`
Expected: FAIL - `baseline_engine.hpp: No such file or directory`

- [ ] **Step 3: Implement BaselineEngine (single mutex + blocking queue + one worker)**

`engine/include/tradeflow/baseline_engine.hpp`:
```cpp
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
```

- [ ] **Step 4: Build and run tests**

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && cmake --build build -j && ctest --test-dir build -R BaselineEngine --output-on-failure"`
Expected: `100% tests passed`

- [ ] **Step 5: Commit**

```bash
git add engine/include/tradeflow/baseline_engine.hpp tests/baseline_engine_test.cpp tests/CMakeLists.txt
git commit -m "Add coarse-locked baseline engine with tests"
git push origin main
```

---

### Task 1.9: Benchmark harness + run + record numbers

**Files:**
- Create: `bench/CMakeLists.txt`, `bench/src/histogram.hpp`, `bench/src/workload.hpp`, `bench/src/bench_main.cpp`, `docs/benchmarks.md`
- Modify: `README.md`

- [ ] **Step 1: Write the latency histogram helper**

`bench/src/histogram.hpp`:
```cpp
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
```

- [ ] **Step 2: Write the workload generator**

`bench/src/workload.hpp`:
```cpp
#pragma once
#include <cstdint>
#include <random>
#include <vector>
#include "tradeflow/order.hpp"

namespace tradeflow::bench {

struct WorkloadConfig {
  std::uint32_t num_symbols = 8;
  std::uint64_t num_orders = 2'000'000;
  unsigned seed = 12345;
  tradeflow::Price mid = 10000;     // center price in ticks
  tradeflow::Price band = 50;       // +/- ticks around mid
};

// Pre-generates orders WITHOUT timestamps (set ts_ingress at submit time).
// Mix: ~60% limit, ~20% market, ~20% cancel (cancel targets a prior order id).
inline std::vector<tradeflow::Order> generate(const WorkloadConfig& cfg) {
  using namespace tradeflow;
  std::mt19937_64 rng(cfg.seed);
  std::uniform_int_distribution<int> kind(0, 99);
  std::uniform_int_distribution<int> side(0, 1);
  std::uniform_int_distribution<int> off(-static_cast<int>(cfg.band),
                                         static_cast<int>(cfg.band));
  std::uniform_int_distribution<int> qty(1, 100);
  std::uniform_int_distribution<std::uint32_t> sym(0, cfg.num_symbols - 1);

  std::vector<Order> out;
  out.reserve(cfg.num_orders);
  std::vector<std::vector<OrderId>> live(cfg.num_symbols);  // resting per symbol
  OrderId next_id = 1;

  for (std::uint64_t i = 0; i < cfg.num_orders; ++i) {
    Order o{};
    o.id = next_id++;
    o.symbol = sym(rng);
    o.side = side(rng) ? Side::Sell : Side::Buy;
    const int k = kind(rng);
    if (k < 20 && !live[o.symbol].empty()) {            // ~20% cancel
      o.type = OrderType::Cancel;
      auto& v = live[o.symbol];
      std::uniform_int_distribution<std::size_t> pick(0, v.size() - 1);
      o.id = v[pick(rng)];                              // cancel a live order
    } else if (k < 40) {                                // ~20% market
      o.type = OrderType::Market;
      o.qty = qty(rng);
    } else {                                            // ~60% limit
      o.type = OrderType::Limit;
      o.price = cfg.mid + off(rng);
      o.qty = qty(rng);
      live[o.symbol].push_back(o.id);                   // may rest
    }
    out.push_back(o);
  }
  return out;
}

}  // namespace tradeflow::bench
```

- [ ] **Step 3: Write the benchmark main**

`bench/src/bench_main.cpp`:
```cpp
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "tradeflow/baseline_engine.hpp"
#include "tradeflow/order.hpp"
#include "tradeflow/symbol_worker.hpp"
#include "histogram.hpp"
#include "workload.hpp"

using namespace tradeflow;
using bench::Histogram;
using bench::WorkloadConfig;

namespace {

double secs(std::chrono::steady_clock::time_point a,
            std::chrono::steady_clock::time_point b) {
  return std::chrono::duration<double>(b - a).count();
}

struct Result {
  std::string name;
  double throughput = 0;
  std::uint64_t p50 = 0, p99 = 0, p999 = 0, pmax = 0, processed = 0;
};

Result run_tradeflow(const std::vector<Order>& orders, std::uint32_t num_symbols,
                     std::size_t ring_cap, unsigned producer_threads) {
  std::vector<std::unique_ptr<Histogram>> hists(num_symbols);
  for (auto& h : hists) h = std::make_unique<Histogram>();

  std::vector<std::unique_ptr<SymbolWorker>> workers;
  workers.reserve(num_symbols);
  for (std::uint32_t s = 0; s < num_symbols; ++s) {
    Histogram* h = hists[s].get();
    workers.emplace_back(std::make_unique<SymbolWorker>(
        s, ring_cap, [h](const Order& o, const BookDelta&) {
          h->add(now_ns() - o.ts_ingress);   // ingress -> match latency
        }));
  }
  for (auto& w : workers) w->start();

  const auto t0 = std::chrono::steady_clock::now();
  std::vector<std::thread> producers;
  const std::size_t n = orders.size();
  for (unsigned p = 0; p < producer_threads; ++p) {
    producers.emplace_back([&, p] {
      for (std::size_t i = p; i < n; i += producer_threads) {
        Order o = orders[i];
        o.ts_ingress = now_ns();
        while (!workers[o.symbol]->submit(o)) {}
      }
    });
  }
  for (auto& t : producers) t.join();
  for (auto& w : workers) w->drain_and_stop();
  const auto t1 = std::chrono::steady_clock::now();

  Histogram all;
  for (auto& h : hists) all.merge(*h);
  all.sort();

  Result r;
  r.name = "TradeFlow (lock-free, thread/symbol)";
  for (auto& w : workers) r.processed += w->processed();
  r.throughput = r.processed / secs(t0, t1);
  r.p50 = all.percentile(0.50);
  r.p99 = all.percentile(0.99);
  r.p999 = all.percentile(0.999);
  r.pmax = all.percentile(1.0);
  return r;
}

Result run_baseline(const std::vector<Order>& orders, std::uint32_t num_symbols,
                    unsigned producer_threads) {
  Histogram h;
  std::mutex hmu;
  BaselineEngine eng(num_symbols, [&](const Order& o, const BookDelta&) {
    const std::uint64_t lat = now_ns() - o.ts_ingress;
    std::lock_guard<std::mutex> lk(hmu);
    h.add(lat);
  });
  eng.start();

  const auto t0 = std::chrono::steady_clock::now();
  std::vector<std::thread> producers;
  const std::size_t n = orders.size();
  for (unsigned p = 0; p < producer_threads; ++p) {
    producers.emplace_back([&, p] {
      for (std::size_t i = p; i < n; i += producer_threads) {
        Order o = orders[i];
        o.ts_ingress = now_ns();
        eng.submit(o);
      }
    });
  }
  for (auto& t : producers) t.join();
  eng.drain_and_stop();
  const auto t1 = std::chrono::steady_clock::now();

  h.sort();
  Result r;
  r.name = "Baseline (mutex + blocking queue)";
  r.processed = eng.processed();
  r.throughput = r.processed / secs(t0, t1);
  r.p50 = h.percentile(0.50);
  r.p99 = h.percentile(0.99);
  r.p999 = h.percentile(0.999);
  r.pmax = h.percentile(1.0);
  return r;
}

void print_row(const Result& r) {
  std::printf("%-38s %12.0f %10llu %10llu %10llu %10llu\n", r.name.c_str(),
              r.throughput, (unsigned long long)r.p50, (unsigned long long)r.p99,
              (unsigned long long)r.p999, (unsigned long long)r.pmax);
}

}  // namespace

int main(int argc, char** argv) {
  WorkloadConfig cfg;
  std::size_t ring_cap = 1 << 16;
  unsigned producer_threads = 4;
  std::string csv_path = "bench/results/latest.csv";
  for (int i = 1; i < argc - 1; ++i) {
    if (!std::strcmp(argv[i], "--symbols")) cfg.num_symbols = std::stoul(argv[++i]);
    else if (!std::strcmp(argv[i], "--orders")) cfg.num_orders = std::stoull(argv[++i]);
    else if (!std::strcmp(argv[i], "--producers")) producer_threads = std::stoul(argv[++i]);
    else if (!std::strcmp(argv[i], "--ring")) ring_cap = std::stoull(argv[++i]);
    else if (!std::strcmp(argv[i], "--csv")) csv_path = argv[++i];
  }

  std::printf("Generating %llu orders across %u symbols (seed %u)...\n",
              (unsigned long long)cfg.num_orders, cfg.num_symbols, cfg.seed);
  const auto orders = bench::generate(cfg);

  std::printf("Running baseline...\n");
  const Result base = run_baseline(orders, cfg.num_symbols, producer_threads);
  std::printf("Running TradeFlow...\n");
  const Result tf = run_tradeflow(orders, cfg.num_symbols, ring_cap, producer_threads);

  std::printf("\n%-38s %12s %10s %10s %10s %10s\n", "config", "orders/sec",
              "p50(ns)", "p99(ns)", "p999(ns)", "max(ns)");
  print_row(base);
  print_row(tf);

  if (base.p99 > 0) {
    const double red = 100.0 * (double)(base.p99 - tf.p99) / (double)base.p99;
    std::printf("\nP99 latency reduction (TradeFlow vs baseline): %.1f%%\n", red);
    std::printf("Throughput speedup: %.2fx\n", tf.throughput / base.throughput);
  }

  std::ofstream csv(csv_path);
  if (csv) {
    csv << "config,throughput,p50,p99,p999,max,processed\n";
    auto row = [&](const Result& r) {
      csv << r.name << ',' << r.throughput << ',' << r.p50 << ',' << r.p99 << ','
          << r.p999 << ',' << r.pmax << ',' << r.processed << '\n';
    };
    row(base);
    row(tf);
    std::printf("Wrote %s\n", csv_path.c_str());
  }
  return 0;
}
```

- [ ] **Step 4: Write bench/CMakeLists.txt**

```cmake
add_executable(tradeflow_bench src/bench_main.cpp)
target_include_directories(tradeflow_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tradeflow_bench PRIVATE tradeflow_engine)
```

- [ ] **Step 5: Build the benchmark**

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && mkdir -p bench/results && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j --target tradeflow_bench 2>&1 | tail -10"`
Expected: builds `build/bench/tradeflow_bench` with no errors.

- [ ] **Step 6: Run the benchmark and capture real numbers**

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && ./build/bench/tradeflow_bench --symbols 8 --orders 2000000 --producers 4 | tee /tmp/bench.txt"`
Expected: prints a results table; TradeFlow throughput should be high (target >=240K orders/sec) and P99 reduction positive (target >=60%). Record the actual numbers.

If targets are not met, tune: increase `--symbols` (more parallelism), adjust `--producers`, raise `--ring`. Document whatever the machine actually produces - do NOT fake numbers.

- [ ] **Step 7: Write docs/benchmarks.md with the real results**

`docs/benchmarks.md` (fill in the ACTUAL measured values from Step 6):
```markdown
# TradeFlow Benchmarks

Machine: WSL2 Ubuntu 24.04, 16 cores, ~7.4 GB RAM. Build: Release (-O2).
Command: `./build/bench/tradeflow_bench --symbols 8 --orders 2000000 --producers 4`

| Config | Throughput (orders/sec) | P50 (ns) | P99 (ns) | P999 (ns) |
|--------|------------------------:|---------:|---------:|----------:|
| Baseline (mutex + blocking queue) | <fill> | <fill> | <fill> | <fill> |
| TradeFlow (lock-free, thread/symbol) | <fill> | <fill> | <fill> | <fill> |

- **P99 latency reduction:** <fill>%
- **Throughput speedup:** <fill>x

Numbers are reproducible with seed 12345. Re-run with the command above.
```

- [ ] **Step 8: Update README status and commit everything**

In `README.md`, replace the benchmark placeholder line with the headline numbers from Step 6.

```bash
git add bench/CMakeLists.txt bench/src/histogram.hpp bench/src/workload.hpp bench/src/bench_main.cpp docs/benchmarks.md README.md engine/include/tradeflow/symbol_worker.hpp engine/include/tradeflow/baseline_engine.hpp tests/symbol_worker_test.cpp tests/baseline_engine_test.cpp
git commit -m "Add benchmark harness comparing lock-free engine vs coarse-locked baseline"
git push origin main
```

- [ ] **Step 9: Full clean build + test gate**

Run: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/c/Users/2006t/OneDrive/Desktop/Trading && rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ctest --test-dir build --output-on-failure"`
Expected: all tests pass from a clean build. This is the Phase 1 completion gate.

---

## Phase 1 Done = resume bullet 1 proven

After Task 1.9, the repo builds clean, all unit + stress tests pass (incl. under TSan), and `docs/benchmarks.md` contains real measured throughput and P99 reduction. At that point we write the **Phase 2 plan** (FlatBuffers schema + uWebSockets streaming + WS throughput benchmark) as a separate document.

## Self-Review Notes

- **Spec coverage:** lock-free queues (Tasks 1.1, 1.2), CAS (1.2), thread-per-symbol (1.7), coarse-locked baseline + 60% P99 / 240K claim (1.8, 1.9), order book price-time priority (1.4, 1.5), matching engine (1.6). Streaming/WASM/WebGL are deferred to Phase 2/3 plans by design.
- **Type consistency:** `Order`, `Trade`, `LevelUpdate`, `BookDelta`, `Side`, `OrderType` defined in Task 1.3 and used unchanged after. The `DeltaSink` signature is `void(const Order&, const BookDelta&)` in both `SymbolWorker` (Task 1.7) and `BaselineEngine` (Task 1.8); the bench (Task 1.9) reads `Order::ts_ingress` in the sink to compute ingress->match latency. No later signature changes.
- **Latency measurement:** measured as `now_ns() - order.ts_ingress`, sampled inside each matching thread's sink, collected per-symbol and merged for percentiles. Both engines are driven by identical pre-generated, seeded workloads for an apples-to-apples comparison.
```

