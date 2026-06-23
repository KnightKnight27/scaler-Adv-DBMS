# Benchmark Report — Team THE BOYS

**Extension Track:** Track A — Performance (Batch / Columnar Execution)  
**Team:** THE BOYS  
**Date:** June 2026

---

## 1. Objective

Compare **row-at-a-time** execution against **batch + columnar filter** execution on the same row-store heap, measuring:

- Query latency (min / avg / p50)
- Throughput (queries per second)
- Resource utilization (tuples scanned, buffer hit rate, columnar vector bytes)

---

## 2. Experimental Setup

| Parameter | Value |
|-----------|-------|
| **Hardware** | Apple Silicon Mac (developer machine) |
| **OS** | macOS |
| **Compiler** | g++ / clang++, C++17 |
| **Storage** | Row-store slotted pages (`minidb.dat`) |
| **Buffer pool** | 64 frames (256 KB) |
| **Table schema** | `users(id INT PRIMARY KEY, name STRING, age INT)` |
| **Query** | `SELECT * FROM users WHERE age = 30;` |
| **Selectivity** | ~2% (10 of 500 rows match; scales proportionally) |
| **Batch size** | 256 rows (`BATCH_SIZE`) |
| **Execution modes** | `SET EXEC_MODE ROW` vs `SET EXEC_MODE BATCH` |

### Benchmark commands

```bash
cd miniDB
make benchmark

./benchmark          # quick: 500 rows, 10 measured iterations (~1s)
./benchmark --full   # extended: 200/500/1000 rows, 10 iterations (~15s)
```

Each mode runs **warmup iterations** first, then measured runs with buffer pool counters reset.

---

## 3. Results

### 3.1 Quick run (500 rows)

| Metric | Row Store | Batch + Columnar |
|--------|-----------|------------------|
| Latency min | 1.626 ms | 1.793 ms |
| Latency avg | 1.692 ms | 1.850 ms |
| Latency p50 | 1.690 ms | 1.853 ms |
| Throughput | 590.8 QPS | 540.6 QPS |
| Tuples scanned / query | 500 | 500 |
| Tuples output / query | 10 | 10 |
| Batches / query | 0 | 2 |
| Columnar filter | no | yes |
| Columnar vector bytes | 0 | 4,000 |
| Buffer hit rate | 100% | 100% |
| Est. bytes read / query | 32,000 | 36,000 |
| **Speedup (latency)** | — | **0.91×** (row faster) |
| **Speedup (throughput)** | — | **0.91×** |

### 3.2 Extended run — multi-scale

#### 200 rows

| Metric | Row Store | Batch + Columnar |
|--------|-----------|------------------|
| Latency avg | 0.718 ms | 0.769 ms |
| Throughput | 1,393 QPS | 1,300 QPS |
| Buffer hit rate | 100% | 100% |
| Speedup | — | 0.93× |

#### 500 rows

| Metric | Row Store | Batch + Columnar |
|--------|-----------|------------------|
| Latency avg | 1.795 ms | 1.811 ms |
| Throughput | 557 QPS | 552 QPS |
| Buffer hit rate | 100% | 100% |
| Speedup | — | 0.99× |

#### 1,000 rows

| Metric | Row Store | Batch + Columnar |
|--------|-----------|------------------|
| Latency avg | 28.961 ms | 29.442 ms |
| Throughput | 34.5 QPS | 34.0 QPS |
| Buffer hit rate | 93.7% | 93.7% |
| Batches / query | 0 | 4 |
| Speedup | — | 0.98× |

---

## 4. Analysis

### 4.1 Query latency

At **200–500 rows**, both modes complete in **under 2 ms** because the working set fits entirely in the buffer pool (100% hit rate). Row mode has a slight edge because it avoids batch partitioning and column vector allocation.

At **1,000 rows**, latency rises to **~29 ms** as the scan touches more pages and buffer hit rate drops to **93.7%** (some disk reads via buffer pool misses). Both modes scale similarly because the dominant cost is **heap scan I/O**, not filter dispatch.

### 4.2 Throughput

Throughput mirrors latency: row store achieves **590 QPS** at 500 rows vs **540 QPS** for batch mode. At 1,000 rows, both drop to **~34 QPS** due to larger scan cost.

### 4.3 Resource utilization

| Observation | Row Store | Batch + Columnar |
|-------------|-----------|------------------|
| Tuples scanned | Full table scan | Full table scan (same) |
| Extra memory | None | Columnar INT vector (4 KB at 500 rows) |
| CPU pattern | Per-row predicate eval | Batch loop + columnar INT equality |
| Buffer pressure | Low until 1K+ rows | Same |

Batch mode adds **columnar vector bytes** (one `int64_t` per row in batch) but does not reduce pages read — both paths perform a **full heap scan** for this query because `age` is not indexed.

### 4.4 Why batch mode does not win yet

1. **No index on `age`** — optimizer chooses sequential scan; batch path cannot skip pages.
2. **Small/medium datasets** — entire table fits in buffer pool; row iteration overhead is negligible.
3. **Batch overhead** — partitioning rows into batches of 256 and building column vectors adds cost at small scale.
4. **Same materialization** — both paths read every tuple from the heap before filtering.

### 4.5 When batch mode is expected to help

Batch + columnar execution should show benefits when:

- Tables exceed buffer pool size (more I/O-bound scans)
- Multiple predicates are evaluated over INT columns in SIMD-friendly loops
- A true columnar storage layer avoids deserializing full rows

The infrastructure (batch executor, columnar filter, metrics, A/B benchmark harness) is in place for these future experiments.

---

## 5. Conclusion

We implemented Track A with a **dual execution engine** (row vs batch+columnar) and a reproducible benchmark harness. At current scale (200–1,000 rows), **row-store execution is slightly faster** (0.91–0.99× batch speedup). The benchmark confirms both paths are correct and measurable, and identifies that **index selection and dataset scale** are the main factors governing performance — not batch size alone.

---

## 6. Reproducing Results

```bash
make clean && make -j4
./benchmark          > results_quick.txt
./benchmark --full   > results_full.txt
./tests/run_tests.sh # includes benchmark smoke test
```

Raw output is printed to stdout; progress messages go to stderr during data load.
