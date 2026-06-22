# Lab 8: Transaction Manager (MVCC + Strict 2PL)

**Course:** Advanced DBMS (Scaler)
**Author:** Praveen Kumar 24bcs10048

An in-memory transaction manager combining MVCC for reads, Strict Two-Phase Locking for writes, and DFS deadlock detection.

## Build and Run

```bash
g++ -std=c++17 -O2 -pthread -o txn_manager txn_manager.cpp
./txn_manager
```

Or:

```bash
make
make run
```

## What It Demonstrates

| Demo | Scenario |
|------|----------|
| 1 | Snapshot isolation -- reader sees committed-before-start data only |
| 2 | Dirty read prevention -- uncommitted write invisible |
| 3 | Repeatable read -- same snapshot, same result |
| 4 | Write-write conflict serialization via S2PL |
| 5 | Lost update prevention (first-updater-wins) |
| 6 | Tombstone delete and visibility rules |
| 7 | Garbage collection of old versions |

## Requirements

- g++ with C++17 and POSIX threads (`-pthread`)

## Documentation

See [Assignment.md](Assignment.md) for architecture details, visibility formula, lock protocol, deadlock detection, and how PostgreSQL implements the same concepts.

Praveen Kumar
24bcs10048
