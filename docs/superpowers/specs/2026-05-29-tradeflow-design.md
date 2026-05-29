# TradeFlow - Order Matching Engine: Design Spec

**Date:** 2026-05-29
**Status:** Approved (pending spec review)
**Repo:** https://github.com/khangpt2k6/Trading

## Goal

Build a real, runnable, benchmarkable order matching engine that genuinely
demonstrates the following resume claims (so they can be defended in interviews):

1. A trading engine using **lock-free queues** with **dedicated threads per
   market symbol**, reducing **P99 latency by ~60%** versus blocking queues and
   coarse-grained locking, while sustaining **240K+ orders/sec**.
2. Live order book delivery over **WebSockets** via **incremental delta updates**
   with **zero-copy serialization** (FlatBuffers), pushing **21K+ updates/sec**.

Tech to be represented: C++, WebSocket, WebAssembly (WASM), CAS (compare-and-swap
/ lock-free), FlatBuffers, WebGL.

This is a resume-backing demo. The benchmark prints REAL numbers measured on the
build machine; we design to meet or beat the targets above, and the resume should
cite whatever the real measured figures turn out to be (flagged when we run them).

## Environment

- Build host: WSL2, Ubuntu 24.04, 16 cores, ~7.4 GB RAM, passwordless sudo.
- Toolchain to install: build-essential (g++/make), CMake, Node.js, Emscripten SDK,
  FlatBuffers compiler (flatc).
- Native build for engine/server/bench; Emscripten build for the WASM client.

## Architecture

```
 load generator (C++)                         browser client
        |  NEW/CANCEL orders                          ^
        |  routed by symbol                            | binary FlatBuffers delta frames (WS)
        v                                              |
 per-symbol lock-free MPSC ring (CAS)          uWebSockets broadcaster
        |                                              ^
        v                                              | deltas (SPSC ring)
 matching thread PER SYMBOL  --- trades + deltas ------+
 (price-time-priority OrderBook)

 browser: WASM module (shared C++ book-apply logic, Emscripten)
          decodes FlatBuffers -> applies deltas -> exposes book as flat array
          -> WebGL price-ladder / depth render
```

Key idea: the **same C++ order-book + delta-apply code compiles natively (server)
and to WASM (client)**, so the browser reconstructs the book identically from the
delta stream.

## Components

### Core engine (`engine/`) - proves bullet 1
- `mpsc_ring.hpp`: bounded lock-free MPSC ring buffer. Power-of-two capacity,
  cache-line-padded head/tail to avoid false sharing, Vyukov-style per-slot
  sequence counter using `std::atomic::compare_exchange` (the literal CAS).
  Producers = feed handlers; consumer = the matching thread.
- `spsc_ring.hpp`: single-producer/single-consumer ring for egress
  (matching thread -> broadcaster), simpler and faster.
- `order.hpp`, `delta.hpp`: Order, Trade, LevelUpdate, BookDelta types.
- `order_book.hpp`: price-time priority. `std::map` of price levels (bids
  descending, asks ascending), each level a FIFO of resting orders with an
  aggregate quantity. Flat-array-of-ticks optimization documented as an
  alternative; `std::map` is sufficient for the target throughput.
- `matching_engine.hpp`: order types = limit, market, cancel. Crosses incoming
  orders against the opposite side, generates trades, rests remainder, emits
  book deltas. One dedicated matching thread per symbol.

### Benchmark harness (`bench/`) - proves the numbers
- Synthetic order flow: configurable symbol count, target rate, order mix
  (default 60% limit / 20% market / 20% cancel), price distribution around mid,
  seeded PRNG for reproducibility.
- Two configurations measured back-to-back:
  - **A (TradeFlow):** per-symbol lock-free MPSC ingress + dedicated matching
    thread per symbol.
  - **B (baseline):** single global `std::mutex` + `std::condition_variable`
    blocking queue, single matching thread (coarse-grained locking).
- Metrics: aggregate throughput (orders/sec); end-to-end latency per order
  (ingress -> match timestamp via high-resolution clock) reported as
  P50/P99/P999/max; **% P99 reduction of A vs B**.
- Output: comparison table to stdout + CSV report under `bench/results/`.
- Targets: >=240K orders/sec aggregate, >=60% P99 reduction vs baseline.

### Streaming layer (`schema/`, `server/`) - proves bullet 2
- FlatBuffers schema `tradeflow.fbs`:
  - `LevelUpdate { side: Side; price: long; qty: long; }`
  - `Trade { price: long; qty: long; ts: ulong; }`
  - `BookDelta { symbol: string; seq: ulong; levels: [LevelUpdate]; trades: [Trade]; }`
  - `Snapshot { symbol: string; seq: ulong; levels: [LevelUpdate]; }`
- uWebSockets server: sends a `Snapshot` on connect, then incremental
  `BookDelta` frames. **Zero-copy:** server transmits the FlatBufferBuilder's
  raw bytes; the client reads generated accessors directly over the received
  `ArrayBuffer` with no parse/copy step.
- WS throughput benchmark: a counting WebSocket client measures deltas/sec
  (target >=21K).

### WASM + WebGL client (`wasm/`, `web/`) - proves WASM/WebGL/FlatBuffers
- The shared `order_book` + delta-apply logic compiles to WASM via Emscripten,
  exposing a small C-API: `apply_frame(ptr, len)`, `levels_ptr()`,
  `level_count()`.
- The browser receives WS binary frames, copies into the WASM heap, calls
  `apply_frame`, and reads the resulting levels array back as a `Float32Array`
  view over WASM linear memory.
- WebGL renders a live price ladder + depth chart from that array. (Visual layout
  to be mocked/approved at the start of Phase 3.)

## Testing strategy (TDD)

- Matching logic: tests written before implementation - price-time priority,
  partial fills, market-order multi-level sweeps, cancels, empty-book and
  crossed-book edge cases.
- Lock-free queues: multi-threaded stress tests (no lost or duplicated items,
  ordering guarantees) run under ThreadSanitizer (TSan).
- Test framework: GoogleTest via CMake FetchContent.

## Build & tooling

- CMake (>=3.20) with FetchContent for third-party deps (flatbuffers,
  uWebSockets/uSockets, googletest).
- A `scripts/setup.sh` installs the toolchain inside WSL (build-essential, cmake,
  nodejs, emsdk, flatc).
- GitHub Actions CI: build + test on ubuntu-latest, plus a WASM build job. Makes
  the repo build reproducibly and look professional.

## Phasing (each phase committed + pushed; NO Claude attribution in any commit)

- **Phase 0:** repo scaffold, CMake, `scripts/setup.sh`, CI workflow, real README.
- **Phase 1:** lock-free queues + order book + matching engine + per-symbol
  threading + benchmark harness. Proves bullet 1.
- **Phase 2:** FlatBuffers schema + uWebSockets streaming server + WS throughput
  benchmark. Proves bullet 2.
- **Phase 3:** WASM build of shared book logic + WebGL frontend. Proves
  WASM/WebGL/FlatBuffers client.

## Non-goals (YAGNI)

- No persistence / database; books are in-memory.
- No authentication, accounts, or real market data feeds.
- No exotic order types (stop, iceberg, FOK) in v1 - limit/market/cancel only.
- No multi-machine distribution; single-process engine.
