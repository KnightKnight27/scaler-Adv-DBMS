# MiniDB Raw Benchmark Execution Log

This log displays the raw console output from compiling and running `benchmark.cpp` on Windows:

```text
============================================
      MINIDB ENGINE BENCHMARK RUNNER        
============================================

=== Running Slotted Page Benchmark ===
Inserted 200 tuples in 0.2017 ms (991572 ops/sec)
Compacted 100 tombstoned slots in 0.0822 ms

=== Running B+ Tree vs Table Scan Benchmark ===
Table Scan lookup time for 200 queries: 2.148 ms (Avg: 0.01074 ms/query)
B+ Tree Index lookup time for 200 queries: 0.2008 ms (Avg: 0.001004 ms/query)
Speedup ratio: 10.6972x

=== Running Query Optimizer DP Scaling Benchmark ===
Optimized 3-way join query in 41.5 microseconds
Optimized 4-way join query in 129.8 microseconds
Optimized 5-way join query in 435.5 microseconds
Optimized 6-way join query in 1374.8 microseconds

=== Running ARIES Recovery Benchmark ===
Recovered 5000 log operations in 48.216 ms (103700 ops/sec)

=== Running Log Replication Benchmark ===
Synchronous replication of 500 logs: 221.417 ms (2258.18 ops/sec)
Asynchronous replication of 500 logs: 25.109 ms (19913.2 ops/sec)
Async throughput improvement: 8.81823x

BENCHMARK SUITE EXECUTED SUCCESSFULLY!
```
