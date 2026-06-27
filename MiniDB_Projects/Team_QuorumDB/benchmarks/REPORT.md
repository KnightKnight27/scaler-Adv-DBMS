# MiniDB Benchmark Report

**Reproduce:** `python benchmarks/run_benchmarks.py [--scale N]`
Raw numbers are written to [`results.json`](results.json). The figures below
are from a representative run at the default scale (5,000-row dataset).

## Experimental setup

| | |
|---|---|
| Engine | MiniDB (pure Python 3.13, no external deps) |
| Page size | 4 KiB, slotted pages |
| Buffer pool | CLOCK replacement (size varied per experiment) |
| Index | B+Tree, fan-out 64, unique primary key |
| Durability | WAL, `fsync` on commit; STEAL / NO-FORCE |
| Dataset | 5,000 rows unless noted; warm OS cache |

Each query experiment averages over 2,000 lookups; throughput experiments
time the whole batch. Absolute milliseconds are interpreter-bound (CPython),
so the **ratios** between strategies are the meaningful results.

---

## 1. Index scan vs sequential scan (optimizer value)

Point lookups returning a single row, on an indexed column (`id`, primary key)
versus a non-indexed column (`k`, same values) that forces a full scan.

| Access path | Latency / lookup | Speedup |
|---|--:|--:|
| **B+Tree IndexScan** (`WHERE id = ?`) | **0.65 ms** | 22.7× |
| SeqScan (`WHERE k = ?`) | 14.7 ms | 1× |

**Analysis.** The index turns an O(N) heap scan into an O(log N) descent plus a
single RID fetch — a **22.7× reduction** in latency at 5,000 rows, and the gap
widens with N. This is exactly the trade-off the cost-based optimizer evaluates:
`EXPLAIN` confirms it picks `IndexScan` for the PK equality and `SeqScan` for
the non-indexed predicate (see `demos/demo_sql.py`).

## 2. Insert throughput: autocommit vs batched transaction

Inserting 5,000 rows, one transaction per row (autocommit) versus a single
explicit `BEGIN … COMMIT`.

| Mode | Rows/sec | |
|---|--:|--:|
| Autocommit (fsync per row) | 1,672 | 1× |
| **Batched transaction** (one fsync) | **24,190** | **14.5×** |

**Analysis.** Each commit forces the log to disk (`fsync`), which dominates the
autocommit cost. Amortising one `fsync` over the whole batch yields a **14.5×**
throughput gain — a direct, measurable demonstration of the write-ahead log's
group-commit behaviour and why transaction batching matters.

## 3. Buffer pool hit ratio vs pool size

Five repeated full scans of a 120-page table while shrinking the pool below the
working-set size.

| Pool frames | Resident vs data (120 pg) | Hit ratio |
|--:|---|--:|
| 4 | 3% | 0.00 |
| 16 | 13% | 0.00 |
| 64 | 53% | 0.01 |
| 256 | >100% | **0.83** |

**Analysis.** When the pool is smaller than the working set, CLOCK thrashes and
nearly every fetch misses. Once the pool exceeds the 120 data pages, the first
scan populates it and the remaining four scans are almost all hits
(≈4/5 = 0.8, matching the measured 0.83). Wall-clock time is *not* a clean
signal here because the dataset fits in the OS page cache and CPython tuple
deserialisation dominates; the **hit ratio** is the honest storage-layer metric,
and it behaves exactly as buffer-pool theory predicts.

## 4. Replication (Track D)

Shipping the primary's redo log to a fresh replica and applying it.

| Metric | Value |
|---|--:|
| Log records shipped | 5,004 |
| Apply time | 0.022 s |
| **Throughput** | **≈223,000 records/sec** |
| Primary rows / replica rows | 5,000 / 5,000 |
| Read consistency | ✅ identical |

**Analysis.** Because replication replays the same physiological WAL records the
recovery path uses (`page_ops.redo`, guarded by page LSN), applying a record is
a page fetch + in-place edit — no parsing or re-planning — hence the high apply
rate. The replica's reads match the primary exactly (read-after-replicate
consistency). The live socket path (`demos/demo_replication.py`) additionally
demonstrates streaming with near-zero lag and promotion-on-failover.

---

## Takeaways

- **Indexing**: 22.7× faster point lookups; the optimizer chooses it
  automatically based on estimated selectivity and cost.
- **WAL/transactions**: batching cuts `fsync` overhead 14.5×; commit durability
  is preserved (validated by the crash-recovery tests/demo).
- **Buffer pool**: hit ratio scales with pool size exactly as expected;
  CLOCK correctly evicts only unpinned, un-referenced pages.
- **Replication**: reusing WAL redo gives fast, consistent log shipping and a
  clean failover story.

## Threats to validity / limitations

- CPython makes absolute timings interpreter-bound; we report ratios.
- Datasets are warm in the OS cache, so disk-bound effects are understated.
- Single-machine, in-process replication for the throughput number (the socket
  path is correctness-tested separately).
