# MiniDB

## Advanced Database Management Systems Capstone Project

A lightweight relational database management system implemented in Java that demonstrates the core internal components of modern database systems, including storage management, indexing, query processing, transaction management, recovery, and distributed replication.

---

# Team Information

## Team Name

TEAM_BufferPoolers

## Team Members

| Name         | Roll Number | Email                            |
| ------------ | ----------- | -------------------------------- |
| Payel Manna  | 24BCS10400  | payel.24bcs10400@sst.scaler.com  |
| Ardi Jasmine | 24BCS10557  | ardhi.24bcs10349@sst.scaler.com  |
| Namami Verma | 24BCS349    | namami.24bcs10349@sst.scaler.com | 


---

# Project Overview

MiniDB is an educational relational database engine built from first principles to demonstrate how modern database systems work internally.

The project integrates multiple database subsystems including:

* Storage Engine
* Buffer Pool
* B+ Tree Indexing
* SQL Query Processing
* Query Optimization
* Transaction Management
* Deadlock Detection
* Write Ahead Logging (WAL)
* Crash Recovery
* Distributed Replication

The goal is not to compete with production databases such as MySQL or PostgreSQL, but to understand and implement the fundamental concepts behind database systems.

---

# Chosen Extension Track

## Track D — Distributed Systems

The project extends MiniDB with a simple distributed replication architecture.

Implemented features:

* Primary Node
* Replica Node
* Replication Messages
* WAL-based Replication
* Replica Catch-up
* Read Consistency Verification
* Failure Recovery Demonstration

---

# System Architecture

## High-Level Architecture

```text
                   SQL Query
                        |
                        v
                  Tokenizer
                        |
                        v
                    Parser
                        |
                        v
                      AST
                        |
                        v
                  Optimizer
                        |
                        v
                   Executor
                        |
         --------------------------------
         |              |               |
         v              v               v
      Storage       B+ Tree       Transaction
       Engine         Index         Manager
         |                              |
         v                              v
     Heap Files                  Lock Manager
     Page Manager              Deadlock Detector
     Buffer Pool
         |
         v
       WAL
         |
         v
    Recovery Manager
         |
         v
     Replication
   (Primary/Replica)
```

---

# Project Structure

```text
MiniDB/
├── src/
│   ├── main/java/com/example/minidb
│   │   ├── storage/
│   │   ├── index/
│   │   ├── sql/
│   │   ├── optimizer/
│   │   ├── transaction/
│   │   ├── recovery/
│   │   ├── replication/
│   │   ├── catalog/
│   │   └── demo/
│   └── test/
├── database/
├── pom.xml
└── README.md
```

---

# Storage Layer

## Components

### Page

Represents the smallest unit of storage.

Responsibilities:

* Store raw bytes
* Fixed-size page abstraction

### Page Manager

Responsible for:

* Page allocation
* Page reads
* Page writes

### Heap File

Stores records using page-based storage.

Responsibilities:

* Record insertion
* Record retrieval

### Buffer Pool

Caches pages in memory to reduce disk I/O.

Features:

* Page lookup
* Page caching
* Page eviction

---

# Indexing

## B+ Tree

MiniDB implements a B+ Tree based index.

Supported operations:

* Insert
* Search
* Delete

Components:

* Node
* InternalNode
* LeafNode
* BPlusTree
* IndexManager

### Benefits

* Faster lookups
* Reduced table scans
* Supports optimizer decisions

---

# SQL Query Processing

## Supported Statements

### INSERT

Example:

```sql
INSERT INTO users VALUES (1,Alice);
```

### SELECT

Example:

```sql
SELECT * FROM users;
```

### SELECT with WHERE

Example:

```sql
SELECT * FROM users WHERE id = 2;
```

### DELETE

Example:

```sql
DELETE FROM users WHERE id = 2;
```

---

## Query Execution Pipeline

```text
SQL
 ↓
Tokenizer
 ↓
Parser
 ↓
AST
 ↓
Optimizer
 ↓
Executor
 ↓
Storage Engine
```

---

# Query Optimizer

MiniDB includes a simple cost-based optimizer.

## Supported Decisions

### Table Scan

Used when:

* No index exists
* Index access cost is higher

### Index Scan

Used when:

* Matching index exists
* Estimated index cost is lower

---

## Cost Model

Uses:

* Table page count
* Estimated index lookup cost

to select the execution strategy.

---

# Transactions & Concurrency

## Serializable Isolation

Transactions are executed under serializable semantics.

## Two Phase Locking (2PL)

Implemented using:

* Shared Locks
* Exclusive Locks

### Lock Manager

Responsible for:

* Lock acquisition
* Lock release

### Deadlock Detection

Implemented using a Wait-For Graph.

Detects:

* Circular wait conditions
* Transaction deadlocks

---

# Recovery

## Write Ahead Logging (WAL)

MiniDB follows the WAL protocol.

Rule:

```text
Log record must reach disk before data page.
```

### WAL Manager

Responsibilities:

* Append log records
* Persist operations

---

## Crash Recovery

Implemented using:

* WAL Replay
* Recovery Manager

Recovery procedure:

```text
Read WAL
↓
Reconstruct operations
↓
Replay committed actions
↓
Restore database state
```

---

# Distributed Replication

## Architecture

```text
Primary
   |
   | Replication Message
   v
Replica
```

---

## Primary Server

Responsibilities:

* Accept writes
* Generate WAL records
* Ship replication messages

---

## Replica Server

Responsibilities:

* Receive replication messages
* Replay operations
* Serve read requests

---

## Read Consistency

Replica state is verified against the primary using automated tests.

---

## Failure Recovery

Implemented using:

* Replication logs
* Catch-up mechanism
* Log replay

---

# Benchmarking

Benchmarks were implemented to evaluate major MiniDB subsystems.

## Storage Benchmark

Measures:

* Insert throughput

## Index Benchmark

Measures:

* B+ Tree lookup performance

## Recovery Benchmark

Measures:

* WAL replay time

## Replication Benchmark

Measures:

* Replication latency
* Replica synchronization

---

## Sample Benchmark Results

```text
[Storage Benchmark]
Inserted 1000 rows in 32.34 ms

[Index Benchmark]
1000 index lookups in 3.57 ms

[Recovery Benchmark]
Recovered 1000 WAL entries in 27.96 ms

[Replication Benchmark]
Replicated 1000 rows in 48.64 ms
Replica row count = 1000
```

---

# Testing

Execute:

```bash
mvn test
```

Current status:

```text
Tests run: 17
Failures: 0
Errors: 0
BUILD SUCCESS
```

Covered modules:

* Storage
* Indexing
* SQL Processing
* Optimizer
* Transactions
* Recovery
* Replication

---

# Demonstrations

## Query Execution Demo

```bash
mvn exec:java \
-Dexec.mainClass="com.example.minidb.demo.QueryDemo"
```

Demonstrates:

* INSERT
* SELECT
* WHERE
* DELETE

---

## Crash Recovery Demo

```bash
mvn exec:java \
-Dexec.mainClass="com.example.minidb.demo.CrashRecoveryDemo"
```

Demonstrates:

* WAL Logging
* Simulated Crash
* Recovery Replay

---

## Benchmark Demo

```bash
mvn exec:java \
-Dexec.mainClass="com.example.minidb.demo.BenchmarkDemo"
```

Demonstrates:

* Storage Benchmark
* Index Benchmark
* Recovery Benchmark
* Replication Benchmark

---

# Limitations

Current limitations include:

* No aggregation functions
* No GROUP BY support
* No ORDER BY support
* No secondary indexes
* JOIN operator not implemented
* Single-node write execution

These are potential areas for future work.

---

# Future Improvements

Potential enhancements:

* Full JOIN support
* MVCC Concurrency Control
* Secondary Indexes
* Query Planner Enhancements
* Cost-Based Statistics
* Distributed Consensus
* Multi-Replica Replication

---

# How To Build

```bash
mvn clean compile
```

---

# How To Run Tests

```bash
mvn test
```

---

# How To Run Benchmarks

```bash
mvn exec:java \
-Dexec.mainClass="com.example.minidb.demo.BenchmarkDemo"
```

---

# Conclusion

MiniDB successfully integrates storage management, indexing, query execution, optimization, transaction management, recovery, and distributed replication into a cohesive relational database system. The project demonstrates the fundamental concepts used in real-world database engines while maintaining a modular and understandable architecture suitable for educational purposes.
