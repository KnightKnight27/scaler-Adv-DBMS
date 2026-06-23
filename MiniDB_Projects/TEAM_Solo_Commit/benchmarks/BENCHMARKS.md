# MiniDB Benchmark Report

All numbers below were produced by the two harnesses in this folder, compiled at `-O2` and run
locally. They are real measurements, not estimates.

**Environment**

| | |
|---|---|
| Machine | Apple M4, macOS (Darwin arm64) |
| Compiler | Apple clang 17, C++17, `-O2` |
| Build | `cmake -B build -S . && cmake --build build` |

Reproduce with:

```bash
./build/bench_index_vs_scan
./build/bench_mvcc_vs_2pl
```

---

## 1. B+Tree index scan vs sequential scan

**Harness:** `bench_index_vs_scan.cpp`. Loads 50,000 rows into `t(id PRIMARY KEY, payload)`,
then issues 2,000 point queries two ways: `WHERE id = ?` (equality on the indexed primary key,
which the optimizer turns into an index scan) and `WHERE payload = ?` (a non-indexed column,
forcing a full sequential scan).

| Access path | Total (2000 queries) | Per query | Result |
|---|---|---|---|
| Index scan (`WHERE id = ?`) | 3.59 ms | 0.0018 ms | 2000 hits |
| Sequential scan (`WHERE payload = ?`) | 6987.52 ms | 3.4938 ms | 2000 hits |
| **Speedup from indexing** | | **~1949x** | |

**Analysis.** A point lookup through the B+Tree touches `O(log n)` nodes; the sequential scan
reads every one of the 50,000 rows and tests the predicate. The gap grows with table size,
which is exactly why the cost-based optimizer prefers the index when an equality predicate
covers an indexed column and the estimated cost is lower. On a tiny table the optimizer still
chooses a sequential scan (the index descent is not worth it), which the optimizer test checks.

---

## 2. Extension Track B: MVCC vs Strict 2PL under contention

**Harness:** `bench_mvcc_vs_2pl.cpp`. One writer holds a single hot row in a write transaction
for 150 ms while 8 reader threads repeatedly read that same row, observed over a 200 ms window.
We record reads completed during the window and the latency of one read issued while the write
is in flight.

| Concurrency control | Reads in window | Latency of a read issued during the write |
|---|---|---|
| Strict 2PL | 1,972,936 | **148.1 ms** (blocked until the writer released the lock) |
| MVCC | 4,869,696 | **0.001 ms** (read the last committed version, never blocked) |

MVCC completed about **2.5x** more reads, and the single contended read was roughly **5 orders
of magnitude** faster.

**Analysis.** Under Strict 2PL a read needs a shared lock, which conflicts with the writer's
exclusive lock, so the read stalls for the entire write transaction (here ~148 ms). Under MVCC
the writer creates a new version and the reader simply returns the newest version committed
before its snapshot, so reads never wait on writers. This is the headline property of snapshot
isolation: read throughput stays high under write contention, and readers never block. The
within-window read count for 2PL is still large only because the readers flood the lock during
the ~50 ms after the writer releases; the contended-read latency is the honest measure of the
blocking difference.

**Trade-off.** MVCC keeps old versions around, so it trades the lock-wait cost for storage and
for background version cleanup (garbage collection of versions no longer visible to any active
snapshot). Our store retains all versions; GC is documented as future work.
