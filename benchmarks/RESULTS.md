# MiniDB Benchmark Results

## Setup

- **Benchmark:** `benchmarks/bench_index.cpp` (build with CMake, run `./build/bench_index`)
- **Workload:** 50,000 rows in `t(id INT, v INT)`; 2,000 random point queries `SELECT v FROM t WHERE id = ?`
- **Method:** the same query set is run twice — once with no index (sequential scan) and once after `CREATE INDEX t_id ON t (id)` (cost-based optimizer selects the index scan). Buffer pool: 128 frames (default). Release build (`-O` via CMake `Release`).
- **Machine:** Apple Silicon (macOS, single process). Numbers vary by machine; re-run locally to reproduce.

## Results (representative run)

| Metric | Value |
| --- | --- |
| Insert throughput (batched, 100 rows/stmt) | ~363,000 rows/sec |
| Sequential scan latency | **7.41 ms / query** |
| Index scan latency | **0.003 ms / query** |
| **Speedup (seq ÷ index)** | **~2,100× ** |
| Buffer pool hit rate | 99.9% |

## Analysis

- **Index vs sequential scan.** A point query under a sequential scan is O(N): every
  one of the 50,000 tuples is decoded and tested, so latency grows linearly with
  table size (~7.4 ms here). The B+ tree turns the same lookup into an O(log N)
  root-to-leaf descent plus a single heap fetch — ~0.003 ms, three to four orders
  of magnitude faster. This is exactly the access-path difference the cost-based
  optimizer is choosing between: for a selective equality on a large table it picks
  the index; on a tiny table (where a scan is one page) it correctly stays with the
  sequential scan.
- **Selectivity matters.** The optimizer's decision uses `est_match = N / NDV`. With
  unique `id` (NDV = N) every probe estimates one matching row, so the index always
  wins on a 50k-row table. A low-cardinality column (few distinct values) would make
  `est_match` large and tip the cost model back toward a sequential scan — the
  intended behaviour.
- **Buffer pool.** The 99.9% hit rate reflects the working set fitting comfortably in
  128 frames during the query phase; misses are dominated by the initial load. This
  is also why the simplified, redo-only recovery (no-steal in practice) holds for
  these workloads: dirty pages are not evicted mid-run.
- **Insert throughput.** ~363k rows/sec includes tuple encoding, heap append, and WAL
  append per row (the commit fsync is amortised across the 100-row batch). Index
  maintenance is not included in this figure (the index is built after the load); a
  build-as-you-go index would add a near-constant per-row tree insert.

## Reproducing

```bash
cmake -S . -B build && cmake --build build --target bench_index -j
./build/bench_index
```
