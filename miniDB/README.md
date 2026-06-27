# MiniDB Track B - Concurrency

MiniDB is an educational relational database engine for the Advanced DBMS capstone. The chosen extension is **Track B: Concurrency**, implemented by adding MVCC on top of the required core 2PL transaction path.

## Build

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## M1 Verification

```powershell
ctest --test-dir build --output-on-failure
```

## Milestone Status

- M1 Page manager + buffer pool integrated: implemented.
- M2 B+ tree + parser connected: planned.
- M3 Query execution engine with joins and aggregation: planned.
- M4 Transactions and locking: planned.
- M5 Recovery, benchmarking, and final demo: planned.
- Track B MVCC extension: planned after the required 2PL transaction baseline.

## Required Core Features

The M1 codebase currently includes the storage-engine foundation: page-based heap files, page manager behavior, page reads/writes, and buffer pool usage. Later milestones will add B+ tree indexing, SQL execution, cost-based optimization, 2PL transaction management, WAL recovery, and MVCC extension support.
