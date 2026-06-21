# MiniDB Relational Engine Benchmark Results

Benchmarks ran on a simulation environment.

| Benchmark Metric | Value | Details / Comparison |
|------------------|-------|----------------------|
| **Insert Throughput** | 10388.06 rows/sec | Inserting 10000 rows to heap & primary index |
| **SeqScan p50 Latency** | 82.49 ms | Range scan/exact search without index |
| **SeqScan p95 Latency** | 92.36 ms | Range scan/exact search without index |
| **IndexScan p50 Latency** | 184.17 ms | Exact search on B+ Tree primary key index |
| **IndexScan p95 Latency** | 204.33 ms | Exact search on B+ Tree primary key index |
| **Concurrency Throughput** | 7586.44 txns/sec | 5 threads executing 100 txns each |
| **Concurrency Deadlock Rate** | 0.00 % | Percentage of transactions aborted due to deadlocks |
| **2PL Read Response Time** | 1.14 ms | Reader latency with concurrent writer (blocked by locks) |
| **MVCC Read Response Time** | 1.42 ms | Reader latency with concurrent writer (never blocks) |
| **Crash Recovery Time** | 36.70 ms | Analysis, Redo, and Undo passes for 1000 inserts |

