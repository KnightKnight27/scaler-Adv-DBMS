# MiniDB Benchmark Report

## Advanced Database Management Systems Capstone Project

### Team: TEAM_BufferPoolers

---

# 1. Introduction

The purpose of this benchmark study is to evaluate the performance of the major subsystems implemented in MiniDB.

The benchmark focuses on four critical database components:

1. Storage Engine
2. B+ Tree Indexing
3. Recovery Subsystem
4. Distributed Replication

The goal is not to compare MiniDB against industrial databases such as PostgreSQL or MySQL, but rather to validate that the implemented database components function correctly and efficiently under a representative workload.

---

# 2. Experimental Setup

## Hardware Environment

Benchmark executed on:

* Personal Laptop
* Linux Operating System
* Local File System Storage

## Software Environment

* Java 21
* Maven Build System
* JUnit 5
* MiniDB Version 1.0

## Measurement Method

Execution time was measured using:

```java
long start = System.nanoTime();

/* operation */

long end = System.nanoTime();
```

and converted into milliseconds.

Because benchmarks are executed on a general-purpose operating system, timing results may vary slightly across runs due to:

* JVM warmup
* Garbage collection
* CPU scheduling
* File system caching
* Background processes

---

# 3. Benchmark Workloads

The following workloads were executed.

## Storage Benchmark

Workload:

* Insert 1000 rows
* Persist rows through the Heap File layer
* Allocate pages through the Page Manager

Subsystems involved:

* TableStorage
* HeapFile
* PageManager
* BufferPool

---

## Index Benchmark

Workload:

* Insert 1000 keys into B+ Tree
* Perform 1000 key lookups

Subsystems involved:

* BPlusTree
* IndexManager

---

## Recovery Benchmark

Workload:

* Generate 1000 WAL records
* Simulate system crash
* Replay WAL records using RecoveryManager

Subsystems involved:

* WALManager
* RecoveryManager
* TableStorage

---

## Replication Benchmark

Workload:

* Insert 1000 rows into Primary Server
* Replicate operations to Replica Server
* Verify row consistency

Subsystems involved:

* PrimaryServer
* ReplicaServer
* ReplicationMessage
* WALManager

---

# 4. Benchmark Results

## Raw Results

| Benchmark       | Workload         | Time (ms) |
| --------------- | ---------------- | --------- |
| Storage Insert  | 1000 Rows        | 26.82     |
| B+ Tree Lookup  | 1000 Lookups     | 2.67      |
| Recovery Replay | 1000 WAL Records | 20.65     |
| Replication     | 1000 Rows        | 37.66     |

---

# 5. Storage Engine Analysis

## Objective

Evaluate insertion performance of the storage subsystem.

## Result

```text
Inserted 1000 rows in 26.82 ms
```

## Discussion

The storage engine successfully inserted all records while maintaining page-based organization.

The benchmark demonstrates successful integration of:

* Page allocation
* Heap-file storage
* Buffer pool management

The measured latency indicates that the storage layer is capable of handling sustained insertion workloads with low overhead.

---

# 6. Indexing Analysis

## Objective

Evaluate lookup performance using the B+ Tree index.

## Result

```text
1000 index lookups in 2.67 ms
```

## Discussion

The B+ Tree significantly reduces search cost compared to a full table scan.

Advantages observed:

* Fast point lookups
* Logarithmic search complexity
* Efficient key navigation

This benchmark validates the correctness and effectiveness of the implemented indexing layer.

---

# 7. Recovery Analysis

## Objective

Evaluate crash recovery performance.

## Result

```text
Recovered 1000 WAL entries in 20.65 ms
Recovered rows: 1000
```

## Discussion

The recovery subsystem successfully replayed all WAL records and reconstructed database state.

The benchmark confirms:

* WAL records were persisted correctly
* RecoveryManager successfully replayed log entries
* No committed records were lost

This behavior satisfies the fundamental durability requirement of database systems.

---

# 8. Replication Analysis

## Objective

Evaluate replication performance between the primary and replica nodes.

## Result

```text
Replicated 1000 rows in 37.66 ms
Replica row count = 1000
```

## Discussion

The distributed subsystem successfully replicated all records from the primary server to the replica.

The benchmark demonstrates:

* Correct replication message delivery
* Replica log replay
* Read consistency between nodes

The final row count on the replica exactly matched the primary node, confirming replication correctness.

---

# 9. Comparative Analysis

The benchmark results show clear differences among subsystem workloads.

| Component      | Time (ms) |
| -------------- | --------- |
| Index Lookup   | 2.67      |
| Recovery       | 20.65     |
| Storage Insert | 26.82     |
| Replication    | 37.66     |

Observations:

1. B+ Tree indexing is the fastest subsystem due to efficient search complexity.
2. Recovery performance scales linearly with WAL size.
3. Storage insertion includes page allocation and disk persistence overhead.
4. Replication incurs additional communication and replay overhead but remains efficient for the tested workload.

---

# 10. Validation of Project Requirements

The benchmark validates the successful operation of the following project requirements:

| Requirement             | Status                         |
| ----------------------- | ------------------------------ |
| Storage Engine          | Implemented and Benchmarked    |
| B+ Tree Indexing        | Implemented and Benchmarked    |
| SQL Execution           | Demonstrated through QueryDemo |
| Cost-Based Optimizer    | Implemented and Tested         |
| Transaction Management  | Implemented and Tested         |
| Deadlock Detection      | Implemented and Tested         |
| WAL Recovery            | Implemented and Benchmarked    |
| Distributed Replication | Implemented and Benchmarked    |

---

# 11. Limitations

Current benchmark limitations include:

* Single-machine execution
* Small dataset size (1000 rows)
* No network latency simulation
* No concurrent client workload
* No stress testing under heavy contention

These limitations are acceptable because the primary goal of the capstone project is to demonstrate database architecture and subsystem integration rather than production-scale performance.

---

# 12. Future Improvements

Potential future enhancements include:

* Larger benchmark datasets
* Concurrent transaction benchmarks
* Multi-replica deployments
* Network-based replication benchmarks
* Advanced optimizer cost models
* Secondary indexes
* MVCC-based concurrency control

---

# 13. Conclusion

The benchmark results demonstrate that MiniDB successfully integrates storage management, indexing, recovery, and distributed replication into a functioning relational database system.

All benchmarked subsystems completed successfully and produced correct results. The project satisfies the core requirements of the Advanced DBMS Capstone while maintaining a modular and understandable architecture.

Key achievements include:

* Page-based storage engine
* B+ Tree indexing
* Cost-based query optimization
* Two-Phase Locking and deadlock detection
* Write Ahead Logging
* Crash recovery
* Primary–Replica replication
* End-to-end SQL query execution

The benchmark results confirm that the implemented MiniDB system operates correctly and provides a practical demonstration of modern database internals.
