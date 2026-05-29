# TradeFlow Benchmarks

**Machine:** WSL2, Ubuntu 24.04, 16 logical cores, ~7.4 GB RAM, g++ 13.3, Release (`-O2`).
**Workload:** 2,000,000 orders across 8 symbols, seed `12345`, mix ~60% limit / 20% market / 20% cancel.
**Command:** `./build/bench/tradeflow_bench --symbols 8 --orders 2000000 --producers 4 --rate 1000000`

Two engines are driven by the identical pre-generated, seeded order stream:

- **TradeFlow** - one lock-free MPSC ring (Vyukov, CAS) + one dedicated matching thread *per symbol*.
- **Baseline** - all symbols share a single `std::mutex` + `std::condition_variable` blocking queue, drained by one worker thread (coarse-grained locking).

Latency is measured end to end (order ingress -> match) with a monotonic
nanosecond clock. The harness runs two phases:

1. **Saturation** - producers inject as fast as possible; measures max sustainable throughput.
2. **Paced** - open-loop injection at a fixed sustained rate both engines can keep up with; measures tail latency under realistic load.

## Results

### Throughput (saturation)

| Config | Orders/sec | P50 | P99 |
|--------|-----------:|----:|----:|
| Baseline (mutex + blocking queue) | 2,298,917 | 413 ms | 549 ms |
| TradeFlow (lock-free, thread/symbol) | **28,234,076** | 3.7 ms | 18.2 ms |

- **Throughput speedup: 12.3x.** TradeFlow sustains **28M+ orders/sec**, far above the 240K+/sec target.
- Under max offered load the baseline's single consumer builds a multi-hundred-millisecond backlog; the per-symbol design keeps tails ~30x lower.

### Latency at 1,000,000 orders/sec sustained

| Config | P50 | P99 | P99.9 |
|--------|----:|----:|------:|
| Baseline (mutex + blocking queue) | 20.9 us | 596 us | 6.0 ms |
| TradeFlow (lock-free, thread/symbol) | **0.41 us** | **26 us** | 0.27 ms |

- **P99 latency reduction: ~95%** in this run; **P50 reduction: ~98%** (20.9 us -> 0.41 us).

### P99 reduction vs offered load

The advantage grows as load approaches the baseline's capacity (~2.3M/sec):

| Sustained rate | Baseline P99 | TradeFlow P99 | P99 reduction |
|---------------:|-------------:|--------------:|--------------:|
| 1.0M/sec | 0.46 - 0.82 ms | 26 - 112 us | ~80 - 96% |
| 1.5M/sec | 10.5 ms | 0.46 ms | ~96% |
| saturation | 549 ms | 18.2 ms | ~97 - 99% |

The resume figure of **60% P99 reduction** is a conservative point well inside
the measured range across all tested loads.

## Notes on honesty and reproducibility

- All numbers are produced by the committed harness on the machine above; none
  are hand-edited. Re-run with the command at the top (seed `12345` makes the
  order stream deterministic).
- This is a single-box, in-memory simulation. The absolute throughput (28M/sec)
  reflects a lightweight synthetic book; a deeper book or heavier matching would
  lower it. The *relative* lock-free vs coarse-locked comparison is the headline
  result.
- Tail latency (P99+) shows run-to-run variance on a shared developer machine
  (OS scheduling jitter across the 4 producer + 8 worker threads). P50 is stable
  at ~0.4 us for TradeFlow. The ranges above reflect several runs.
- Lock-free queues are verified race-free under ThreadSanitizer
  (`./scripts/run_tsan.sh`).
