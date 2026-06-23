# Lab 8 - Advanced DBMS: Timestamp-Based MVCC, 2PL, and Deadlock Handling

This project implements a concurrent database engine simulation in C++ featuring **Multi-Version Concurrency Control (MVCC)** with logical timestamps, **Strict Two-Phase Locking (2PL)**, and **Cycle-Based Deadlock Detection**. This serves as an evolution of the previous lab by utilizing a step-by-step synchronous execution model, which is perfect for demonstrating concepts during a viva.

## Project Structure

| File Name | Description |
|-----------|-------------|
| `db_engine.hpp` | Definitions for the `DatabaseSystem` API, `MVCCStore`, and `LockManager`. |
| `db_engine.cpp` | Core implementation of the MVCC logic, lock acquisitions, and deadlock resolution. |
| `main.cpp` | A demonstration showcasing a classic deadlock scenario between two transactions. |
| `CMakeLists.txt` | Build configuration for CMake. |
| `makefile` | Quick makefile for building and running. |

## How to Compile and Execute

**Using Makefile:**
```bash
cd "lab 8"
make run
```

**Using CMake:**
```bash
cd "lab 8"
mkdir build && cd build
cmake ..
make
./db_simulator
```

## System Architecture Overview

The system is composed of a main driver and the database engine core:
- `DatabaseSystem` (Main API)
  - `MVCCStore` (Manages record versions using timestamp ordering)
  - `LockManager` (Handles Strict 2PL and wait-for graph cycle detection)

### MVCC (Multi-Version Concurrency Control)
Every database record maintains a history of its states:
`RecordVersion { data_val, creator_id, ts_commit }`

- **Initialization**: Records (e.g., A=100, B=200) start with a base timestamp.
- **Reading**: Transactions read the latest version committed at or before their snapshot timestamp (`ts_commit <= snapshot_ts`).
- **Writing**: Modifications are stored locally (`pending_writes`) and only added to the global version chain upon successful commit, receiving a new logical timestamp.
This ensures transactions experience Snapshot Isolation.

### Strict Two-Phase Locking (2PL)
Locks are managed in two phases to guarantee serializability:
- **Growing Phase**: Transactions can request Read (Shared) or Write (Exclusive) locks.
- **Shrinking Phase**: Once a transaction commits or aborts, all its locks are released simultaneously. Requesting locks after this phase triggers an abort.

### Cycle Detection (Deadlocks)
Deadlocks are caught immediately:
1. When a transaction requests an unavailable lock, a dependency (waiter -> blockers) is added to the wait-for graph.
2. A Depth-First Search (DFS) looks for cycles.
3. If a cycle is detected, the transaction that caused it is aborted to resolve the deadlock.

## Demonstration Steps (`main.cpp`)

1. **Transaction 1 (T1)** reads and modifies `A`. (Gets Write lock on A).
2. **Transaction 2 (T2)** reads and modifies `B`. (Gets Write lock on B).
3. **T1** attempts to modify `B` (Blocked by T2).
4. **T2** attempts to modify `A` (Blocked by T1). A cycle is formed, causing T2 to abort.
5. **T1** successfully acquires the lock on `B` (since T2 aborted) and commits.
6. **Transaction 3 (T3)** reads `A` and `B` to observe the final committed state.

## Comparison: Lab 7 vs Lab 8

| Feature | Lab 7 | Lab 8 |
|---------|-------|-------|
| Architecture | Monolithic single file | Modular, multi-file with CMake |
| MVCC | xmin/xmax pointers | Logical timestamps |
| Concurrency | POSIX Threads (pthreads) | Synchronous, deterministic execution |
| Deadlocks | Pre-sleep detection | Immediate graph-cycle detection |
