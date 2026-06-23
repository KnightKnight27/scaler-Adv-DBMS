# MiniDB Benchmark Report

## Goal

This benchmark is meant to prove that MiniDB can execute the required capstone operations end to end and show the Track A performance path: table scan versus primary-key B+ tree index lookup, plus a batch-processing scan comparison.

## Environment

- OS: Windows
- Compiler: `g++` with C++17
- Build command: `.\build.ps1`
- Benchmark command: `.\benchmarks\run_benchmark.ps1`
- Data directory: `bench_data`

## Workload

The benchmark script runs:

1. `CREATE TABLE users (id, name, age)`
2. `CREATE TABLE orders (id, user_id, amount)`
3. Several `INSERT` statements, including one order below the join filter threshold.
4. Full table scan: `SELECT * FROM users`
5. Primary-key lookup: `SELECT name FROM users WHERE id = 1`
6. Index visibility: `INDEX_DEMO users`
7. Join query: `SELECT name,amount FROM users JOIN orders ON users.id = orders.user_id WHERE amount > 100`
8. Storage page visibility: `STORAGE_DEMO`
9. Deadlock demo: `LOCK_DEMO`
10. Track A batch processing demo: `PERF_DEMO`
11. Delete path: `DELETE FROM users WHERE id = 2`

## Observed Plans

| Query | Expected Plan |
| --- | --- |
| `SELECT * FROM users` | table scan |
| `SELECT name FROM users WHERE id = 1` | primary B+ tree index scan |
| `INDEX_DEMO users` | sorted primary-key B+ tree leaf entries |
| join on `users.id = orders.user_id` | nested loop join or index nested-loop join, with chosen join order |
| `STORAGE_DEMO` | heap file and page count report |
| `LOCK_DEMO` | wait-for graph deadlock detection |
| `PERF_DEMO` | row-at-a-time scan versus 128-row batch scan |

## Latest Local Result

The latest local run completed successfully:

```text
MiniDB benchmark/demo completed
Commands: 17
Latency: about 100-250 ms total
Throughput: printed by the benchmark runner
Heap bytes: printed by the benchmark runner
WAL bytes: printed by the benchmark runner
```

The exact values vary because this is a small local workload and includes process startup and console I/O. The runner also writes `benchmarks/benchmark_output.txt` locally with the full CLI transcript. For viva, the important result is that the query plan output shows the optimizer choosing an index scan for primary-key equality and a table scan for broader reads.

## Analysis

The B+ tree index avoids scanning every row for `id = 1`. This matters more as table size grows because primary-key lookup follows the tree search path, while table scan checks every row. The join query estimates the `amount > 100` predicate, chooses the smaller estimated outer input, and reports that join order in the plan message. `PERF_DEMO` uses the same predicate over the same synthetic rows for row-at-a-time and 128-row batch filtering, proving the Track A batch path is functionally equivalent while exposing timing output. The current benchmark is intentionally small for easy demonstration; the code paths are still distinct and visible in the returned plan message.

Resource utilization is reported as heap bytes and WAL bytes. This is intentionally simple, but it gives a concrete storage footprint for the demo workload and links benchmark behavior back to storage and recovery.

## Limitations

- This is a functional benchmark, not a production-grade microbenchmark.
- Dataset size is small by default.
- Timings include CLI startup and output rendering.
- The B+ tree implementation is sufficient for demonstration but does not rebalance after delete.
