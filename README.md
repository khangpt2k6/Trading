# TradeFlow - NASDAQ Order Book Reconstruction Engine

TradeFlow reconstructs the **complete NASDAQ order book from raw
TotalView-ITCH 5.0** market data at line rate, using a lock-free,
symbol-sharded C++ pipeline. It then replays the reconstructed book to the
browser over WebSockets (zero-copy FlatBuffers), where a WebAssembly module
rebuilds the book and WebGL renders it live.

It is built on real exchange data, not a simulation: the parser and book builder
consume genuine NASDAQ ITCH files and decode real tickers (RDS.B, SAP, ASML, ...).

## Highlights (measured on real NASDAQ 01302019 data)

- **~10 M messages/sec** zero-copy ITCH parse, single thread.
- **16.5 M+ events/sec** full order-book reconstruction with a symbol-sharded,
  lock-free (Vyukov MPSC, CAS) pipeline - **~12x** a coarse-locked baseline.
- **~90-99% lower P99 latency** than the coarse-locked baseline at a sustained
  1 M events/sec (P50 0.53 us vs 859 us).
- Sharded result proven **identical** to a single-threaded reference; lock-free
  rings are ThreadSanitizer-clean.

Full methodology and tables: [docs/benchmarks-itch.md](docs/benchmarks-itch.md).
(The original synthetic matching-engine benchmark lives in
[docs/benchmarks.md](docs/benchmarks.md); its lock-free core is reused here.)

## Architecture

```
 fetch_itch.sh (HTTP Range)  ->  *.NASDAQ_ITCH50.gz  (WSL-native, not committed)
        |
        v
 reader (zlib stream) -> zero-copy parse -> ItchEvent, routed by symbol
        |
        v
 per-shard lock-free MPSC ring (CAS)   x N        <- "dedicated threads per symbol"
        |
        v
 shard book-builder thread  ->  per-symbol L2 book + deltas
        |
        v
 WebSocket replay (FlatBuffers, zero-copy)  ->  browser WASM book + WebGL  (Phase C)
```

## Build (WSL2 / Linux)

```bash
./scripts/setup.sh                 # one-time toolchain install (incl. zlib)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Get real data and run

```bash
./scripts/fetch_itch.sh 200                  # first 200 MB of a real ITCH file
./build/tools/itch_inspect  $HOME/tradeflow_data/01302019.NASDAQ_ITCH50.partial.gz
./build/bench/itch_bench --file $HOME/tradeflow_data/01302019.NASDAQ_ITCH50.partial.gz \
    --events 5000000 --shards 8 --producers 4 --rate 1000000
```

No download? Everything runs on a spec-accurate generated stream too
(`itch_bench --gen-symbols 64 --events 5000000`).

## Tests

```bash
ctest --test-dir build --output-on-failure   # unit + lock-free stress tests
./scripts/run_tsan.sh                         # same suite under ThreadSanitizer
```

## Status

- [x] Reusable lock-free core (SPSC/MPSC rings, threading, benchmark harness)
- [x] Phase A: zero-copy ITCH 5.0 parser, generator, real-data fetch + inspect
- [x] Phase B: book builder + symbol-sharded lock-free pipeline + real-data benchmark
- [ ] Phase C: WebSocket replay + FlatBuffers + WASM + WebGL live book
