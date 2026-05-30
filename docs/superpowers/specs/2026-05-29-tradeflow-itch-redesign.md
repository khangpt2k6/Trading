# TradeFlow v2 - NASDAQ ITCH Order Book Reconstruction Engine (Redesign)

**Date:** 2026-05-29
**Supersedes the headline use case of** `2026-05-29-tradeflow-design.md` (Phase 1
lock-free core is reused, not discarded).

## Why this redesign

A closed-loop matching *simulation* proves nothing against the real world: the
inputs are invented. This redesign points the same lock-free engine at **real
NASDAQ market data** and gives it a real job: reconstruct the complete exchange
order book from the raw TotalView-ITCH 5.0 protocol at line rate. The resume
claims get stronger and become *defensible with real data*:

- "lock-free queues with dedicated threads per symbol, reducing P99 latency ~60%
  vs coarse locking, sustaining 240K+ msgs/sec" -> now measured rebuilding the
  **real NASDAQ book** at millions of messages/sec.
- "WebSocket delta streaming with zero-copy FlatBuffers serialization, 21K+
  updates/sec" -> replay the reconstructed book to the browser.

## Data

- Source: NASDAQ's public ITCH 5.0 sample files
  (`https://emi.nasdaq.com/ITCH/Nasdaq ITCH/01302019.NASDAQ_ITCH50.gz`, ~4.76 GB
  gz). Verified reachable.
- Acquisition: `scripts/fetch_itch.sh` does an HTTP **Range** download of the
  first ~1 GB (configurable) into `$HOME/tradeflow_data/` (WSL-native ext4, NOT
  the OneDrive-synced repo, NOT committed). ITCH is time-ordered and front-loads
  the full stock directory + market open, so a 1 GB prefix is real data covering
  every symbol's open - enough for a real benchmark and demo.
- Decode: stream-decompress with zlib while parsing, so the ~12 GB uncompressed
  form is never stored.
- CI/tests: a small spec-accurate ITCH **generator** produces deterministic
  bytes so unit tests and CI never need the big download.

## ITCH 5.0 framing and messages

File framing: each message is preceded by a 2-byte big-endian length, then the
payload whose first byte is the message-type char. All multi-byte fields are
big-endian; timestamps are 48-bit ns since midnight; prices are u32 with 4
implied decimals.

Book-relevant messages (lengths include the type byte):
- `S` System Event (12)
- `R` Stock Directory (39) - stock_locate -> ticker mapping
- `A` Add Order, no MPID (36): order_ref u64, side char, shares u32, stock 8, price u32
- `F` Add Order, MPID (40): `A` + 4-byte attribution
- `E` Order Executed (31): order_ref u64, executed_shares u32, match u64
- `C` Order Executed With Price (36): `E` + printable char + exec_price u32
- `X` Order Cancel (23): order_ref u64, cancelled_shares u32
- `D` Order Delete (19): order_ref u64
- `U` Order Replace (35): orig_ref u64, new_ref u64, shares u32, price u32
- `P` Trade non-cross (44), `Q` Cross Trade (40) - optional, for trade tape/validation

## Architecture

```
 fetch_itch.sh (HTTP Range) -> $HOME/tradeflow_data/*.gz
        |
        v
 [reader/decoder thread]  zlib stream -> length-prefixed frames -> parse fields
        |  Event{type, stock_locate, order_ref, side, price, shares, new_ref}
        |  routed by stock_locate -> shard
        v
 [per-shard lock-free MPSC ring (CAS)]   (symbols sharded across N workers
        |                                  = "dedicated threads per symbol")
        v
 [shard worker thread] applies events to per-stock books (order_ref tracking)
        |  -> L2 BookDelta per stock
        v
 [WebSocket replay broadcaster] FlatBuffers zero-copy -> browser
        |
        v
 browser: WASM book reconstruction (shared C++) -> WebGL live book for a symbol
```

### Book builder (the new core)
Maintains `order_ref -> {stock_locate, side, price, shares}` plus a per-stock
aggregate book (price level -> shares). Event application:
- `A`/`F`: insert order; book[stock][side][price] += shares.
- `E`/`C`: reduce order by executed_shares; book -= executed; drop if 0.
- `X`: reduce by cancelled_shares; book -= cancelled; drop if 0.
- `D`: remove order; book -= remaining.
- `U`: delete orig_ref (book -= remaining), insert new_ref at new price/shares.
Reuses the Phase 1 price-level structures. Emits L2 deltas for the streaming layer.

### Zero-copy parsing
Fields are read directly from the decode buffer (memcpy + byte-swap of the exact
field, no per-message heap allocation, no full-message copy). This mirrors the
zero-copy FlatBuffers decode on the output side.

## Reused from Phase 1
Lock-free SPSC/MPSC rings (CAS), price-level/FIFO structures, the per-symbol
threading model, the benchmark harness shape (TradeFlow vs coarse-locked
baseline, P50/P99/P999), GoogleTest + CMake + CI + TSan workflow.

## Phasing (each phase committed + pushed; NO Claude attribution)
- **Phase A:** ITCH 5.0 parser (zero-copy, big-endian) + message structs +
  `fetch_itch.sh` (Range download) + spec-accurate generator. TDD on known bytes.
- **Phase B:** book builder (order_ref tracking) + symbol-sharded lock-free
  pipeline + reader (zlib stream) + throughput/latency benchmark on real ITCH
  vs coarse-locked baseline. Real-data headline numbers.
- **Phase C:** WebSocket replay + FlatBuffers schema + WASM book reconstruction +
  WebGL live book visualization. One tasteful analytic overlay (order-book
  imbalance or trade tape).

## Non-goals (YAGNI)
- Not a real-time live feed (ITCH samples are historical; deterministic replay is
  a feature, not a limitation).
- No NOII/auction/halt handling in v1 beyond what book building needs.
- No persistence; books are in-memory.

## Integrity
All benchmark numbers are produced by the committed code on real (or clearly
labeled generated) ITCH data; none are hand-edited. Reconstruction correctness is
cross-checked against ITCH execution/trade messages.
