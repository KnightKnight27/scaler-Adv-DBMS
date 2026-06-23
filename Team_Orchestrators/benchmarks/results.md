# MiniDB Benchmarks

The benchmark harness is `apps/bench/main.cpp`, built as the `minidb_bench`
executable.

## How to run

```bash
./build.sh                  # builds minidb and minidb_bench (UCRT64 toolchain)
./build/minidb_bench 10000  # argument = number of rows (default 10000)
```

## Experimental setup

For a table `t(id INT, val INT)` with `N` rows and `K` random point lookups
(`K = min(N, 500)`), the harness measures, in order:

1. **Bulk insert** of `N` rows inside one transaction.
2. **Point lookups (sequential scan)** — `K` random key lookups before any index
   exists; each lookup scans the whole table.
3. **Point lookups (index scan)** — the same lookups after `CREATE INDEX` +
   `ANALYZE`; the cost-based planner now chooses the B+ Tree.
4. **Delete** of the lower half of the table.

Timing uses `std::chrono::steady_clock`. Built with the MSYS2 UCRT64 toolchain
(GCC 16, CMake, Ninja).

## Results (sample run, N = 10000, K = 500)

| scenario                   | total (ms) | throughput      |
|----------------------------|-----------:|-----------------|
| bulk insert                |      242.7 | 41,199 rows/s   |
| point lookup (seq scan)    |     3171.9 | 6.3437 ms/op    |
| point lookup (index scan)  |       25.4 | 0.0508 ms/op    |
| delete (lower half)        |      195.7 | 25,552 rows/s   |

**Index speedup on point lookups: ~125x.**

## Analysis

A sequential-scan point lookup is `O(N)` per query, while a B+ Tree lookup is
`O(log N)` plus the matching rows. The gap therefore widens as `N` grows: at
N = 5000 the speedup was ~115x and at N = 10000 it was ~125x.

The key result is that the **cost-based optimizer switches to the index
automatically**: once an index exists and `ANALYZE` shows the equality predicate
is selective, the planner's estimated index-scan cost (`selectivity * rows`)
falls below the sequential-scan cost (`rows`), so it emits an `IndexScan`. This
can be observed directly with `EXPLAIN`:

```sql
EXPLAIN SELECT val FROM t WHERE id = 1234;
-- Query plan (bottom-up):
-- IndexScan t.id = 1234  (rows~10000, sel=0.000100, idx_cost=2.000000, seq_cost=10000.000000)
-- Filter [1 predicate(s)]
-- Project [1 column(s)]
```

Numbers vary by machine; rerun `minidb_bench` to reproduce locally.
