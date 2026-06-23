# MiniDB Performance Benchmark Report

This report compares the performance of **Strict Two-Phase Locking (2PL)** and **Multi-Version Concurrency Control (MVCC)** on a concurrent read-write workload.

## Experimental Setup
- **Workload**: 100 accounts initialized with 1000 balance each.
- **Workers**: 4 reader threads (point SELECT queries) and 2 writer threads (transfer transactions that read, delete, and insert 2 account rows).
- **Run Duration**: 5 seconds.
- **Hardware/Software**: Linux, Python 3.14, multi-threaded (GIL-limited).

## Benchmark Results

| Metric | Strict 2PL Mode | MVCC Mode | Ratio (2PL / MVCC) |
| :--- | :---: | :---: | :---: |
| **Total Completed Transactions** | 663 | 440 | 1.51x |
| **Throughput (Transactions/sec)** | 132.60 | 88.00 | 1.51x |
| **Avg Read Latency (ms)** | 32.27 ms | 53.63 ms | 1.66x |
| **Avg Write Latency (ms)** | 76.65 ms | 134.53 ms | 1.76x |
| **Abort Count (Deadlocks/Conflicts)** | 7 | 1 | - |

## Analysis and Findings

1. **Throughput Comparison**:
   - In this workload, **2PL Mode** achieved higher throughput (132.6 TPS) than **MVCC Mode** (88.0 TPS). While MVCC's theoretical advantage is non-blocking reads, several factors explain this result:
     - The workload is **write-heavy**: each writer transaction performs 2 DELETEs + 2 INSERTs (a full transfer). In both modes, writers must acquire exclusive locks for write-write conflict prevention. MVCC's read-non-blocking advantage is diminished when writes dominate.
     - **MVCC version overhead**: each DELETE+INSERT creates a new tuple version, increasing buffer pool pressure and page fragmentation. The soft-deleted tuples remain in pages, causing SeqScans to iterate over more records.
     - **Full-page WAL logging**: each update logs a complete 4096-byte page image (before + after = 8KB per record), adding significant I/O overhead proportional to the number of updates. MVCC's delete+insert pattern generates 4 WAL records per transfer vs 2 for 2PL's hard-delete.
     - **Python GIL**: CPython's Global Interpreter Lock prevents true parallel execution of threads, reducing MVCC's concurrency advantage. In a C/C++ implementation with true parallelism, MVCC would likely scale better.

2. **Latency Analysis**:
   - 2PL read latency (32.3ms) is lower than MVCC (53.6ms) because 2PL's hard-delete keeps pages compact, while MVCC's soft-deleted tuples inflate scan costs.
   - 2PL write latency (76.7ms) is lower than MVCC (134.5ms) because 2PL's hard-delete + index removal is cheaper than MVCC's soft-delete + new-version insert + new index entry.

3. **Transaction Aborts**:
   - Under **2PL**, 7 aborts occurred from deadlocks (readers holding S-locks while writers wait for X-locks, creating wait-for cycles). The deadlock detector resolves these by aborting the youngest transaction.
   - Under **MVCC**, only 1 abort occurred (write-write conflict). MVCC eliminates reader-writer deadlocks entirely since readers don't acquire locks.

4. **When MVCC Would Win**:
   - MVCC is expected to outperform 2PL in **read-dominated** workloads (e.g., 90% reads, 10% writes) where its non-blocking snapshot reads eliminate lock contention entirely. The current benchmark's 67% read / 33% write ratio is not sufficiently read-heavy to showcase MVCC's strengths.
