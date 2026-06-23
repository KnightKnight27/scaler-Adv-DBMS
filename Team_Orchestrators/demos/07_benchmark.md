# Demo 7 — Benchmark harness

The `minidb_bench` executable measures insert throughput and point-lookup
latency with and without a B+Tree index, plus delete throughput.

## Running

```bash
./build.sh                 # builds minidb and minidb_bench
./build/minidb_bench 10000 # argument = number of rows (default 10000)
```

## What it measures

1. **Bulk insert** of N rows inside one transaction (rows/sec).
2. **Point lookups (sequential scan)** — K random key lookups before any index
   exists; each is a full table scan.
3. **Point lookups (index scan)** — the same lookups after `CREATE INDEX` +
   `ANALYZE`; the cost-based planner now chooses the B+Tree.
4. **Delete throughput** for removing the lower half of the table.

The headline number is the index speedup on point lookups, which grows with N
because the sequential scan is O(N) per lookup while the index scan is
O(log N) + match. On a sample run with N = 5000 the index lookups were on the
order of 100x faster than sequential scans.
