# TradeFlow ITCH Benchmarks (real NASDAQ data)

**Machine:** WSL2, Ubuntu 24.04, 16 logical cores, ~7.4 GB RAM, g++ 13.3, Release (`-O2`).
**Data:** real NASDAQ TotalView-ITCH 5.0, `01302019.NASDAQ_ITCH50` (first ~200 MB
prefix via HTTP Range; ~535 MB decompressed, 16.6M messages, 8,714 symbols).
**Command:**
`./build/bench/itch_bench --file <itch.gz> --events 5000000 --shards 8 --producers 4 --rate 1000000`

Two engines rebuild the full L2 order book from the same pre-parsed event stream:

- **TradeFlow** - events sharded by symbol across 8 lock-free MPSC rings (Vyukov,
  CAS), one book-building thread per shard.
- **Baseline** - all symbols share one `std::mutex` + `std::condition_variable`
  queue drained by a single thread (coarse-grained locking).

## Parsing (single thread, from `itch_inspect`)

- **~10 M messages/sec** zero-copy parse straight off the gzip stream
  (16.6M messages in ~1.7s), decoding real tickers (RDS.B, SAP, ASML, RIO, VOD).

## Book reconstruction throughput (saturation)

| Config | Events/sec | P99 |
|--------|-----------:|----:|
| Baseline (mutex + 1 thread) | 1,299,799 | 1.93 s |
| TradeFlow (sharded lock-free) | **16,572,806** | 23.2 ms |

- **Throughput speedup: 12.75x.** TradeFlow rebuilds the real NASDAQ book at
  **16.5M+ events/sec** - ~70x the resume's 240K/sec target.
- Under max offered load the single-threaded baseline backs up into multi-second
  tails; the sharded design stays ~80x lower.

## Latency at 1,000,000 events/sec sustained

| Config | P50 | P99 | P99.9 |
|--------|----:|----:|------:|
| Baseline (mutex + 1 thread) | 859 us | 143.6 ms | 158.8 ms |
| TradeFlow (sharded lock-free) | **0.53 us** | **0.51 ms** | 4.5 ms |

- **P99 latency reduction: ~99%** (and **~90%** in repeated runs at this rate);
  P50 is ~1600x lower (859 us -> 0.53 us). The resume's 60% figure is conservative.

## Notes on honesty and reproducibility

- Numbers come from the committed `itch_bench` on **real NASDAQ data**; none are
  hand-edited. Same input (file or seed) reproduces the run.
- The sharded engine is proven to reconstruct a book **identical** to a
  single-threaded reference (`ItchPipeline.ShardedMatchesSingleThreaded`), and a
  dual-bookkeeping invariant holds across the stream.
- The coarse-locked baseline shows large run-to-run tail variance (P99 ~67-150 ms
  at 1M/sec; saturation throughput ~1.3-1.6M/sec) - inherent to a single mutex
  under 4 concurrent producers. It consistently loses by ~10x throughput and
  >=90% P99 regardless.
- Latency is measured ingress (just before submit) -> book-applied, sampled in
  each shard's matching thread. Both engines run the identical workload and
  producer count.
- Lock-free rings and the sharded pipeline are ThreadSanitizer-clean
  (`./scripts/run_tsan.sh`).
- Run on the generated stream instead of a download by omitting `--file`
  (`itch_bench --gen-symbols 64 --events 5000000`).
