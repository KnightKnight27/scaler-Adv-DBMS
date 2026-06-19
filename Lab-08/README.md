# Lab 8 — In-Memory Transaction Manager (MVCC + Strict 2PL)

**Student Name:** Ayush Singh  
**Roll Number:** 24BCS10199  
**Course:** Advanced DBMS  
**Institution:** Scaler School of Technology

---

## Overview

This repository contains a header-only C++17 implementation of a single-threaded **In-Memory Transaction Manager** that mimics the concurrency control design of modern database engines like PostgreSQL. It combines multi-version concurrency control (MVCC) for reads with strict two-phase locking (Strict 2PL) for writes.

### Core Architecture Design
1. **MVCC Snapshot Reads**: Readers never block and never take locks. When a transaction is initialized (`begin()`), it captures a logical snapshot of the database state. All subsequent reads are evaluated against this snapshot.
2. **Strict 2PL Writes**: Writers must acquire an exclusive lock (`X-lock`) on any key they modify. This lock is held until the transaction finishes (commit or abort).
3. **Deadlock Detection**: Blocked writes are tracked in a waits-for dependency graph. DFS traversal detects cycles immediately. When a cycle is detected, the youngest transaction (highest transaction ID) is aborted as the victim to break the cycle.
4. **First-Updater-Wins**: During commit, the manager validates that no written key has been updated and committed by another transaction since the committing transaction's snapshot. If a conflict exists, the transaction aborts with `SerializationFailure`.
5. **Garbage Collection (`gc()`)**: Prunes obsolete version records that are older than the snapshot of the oldest active transaction, reclaiming memory while maintaining correctness.

---

## Directory Structure

```text
Lab-08/
├── CMakeLists.txt     # CMake build configuration with warnings enabled (-Wall -Wextra, C++17)
├── main.cpp           # Assert-driven driver demonstrating 6 transactional scenarios
├── txn_manager.hpp    # Header-only implementation of the transaction manager
└── README.md          # Comprehensive project documentation
```

---

## Build & Execution Instructions

You can build and run this project using either CMake or a direct compiler command.

### Option 1: Using CMake (Recommended)
```bash
# Configure the build directory
cmake -S . -B build

# Compile the executable
cmake --build build

# Run the test suite
./build/txn_manager_demo
```

### Option 2: Direct Compilation
```bash
# Compile with C++17 and optimization options
clang++ -std=c++17 -Wall -Wextra -Wpedantic -O3 main.cpp -o txn_manager_demo

# Run the test suite
./txn_manager_demo
```

---

## Technical Specifications & Details

### 1. Why MVCC for Reads and 2PL for Writes?
* **High Read-to-Write Ratio**: In transactional systems (OLTP), reads significantly outnumber writes. Under pure 2PL, readers and writers block each other, leading to high latency and contention.
* **Non-Blocking Reads**: MVCC resolves this by keeping multiple historical versions of data. Readers can retrieve snapshot-consistent data without acquiring locks or blocking writers.
* **Write Serialization**: Strict 2PL ensures that writes on the same key are executed sequentially and safely, preventing dirty writes and write-write conflicts.

### 2. Multi-Version Version Model
Every key map resolves to a linked chain of historical versions. Each version is represented as follows:

| Field | Type | Description |
| :--- | :--- | :--- |
| `value` | `std::string` | The actual string value stored in this version. |
| `tombstone` | `bool` | Flag indicating whether this version represents a logical deletion. |
| `xmin` | `ts_t` (Timestamp) | Commit timestamp of the transaction that created this version. |
| `xmax` | `ts_t` (Timestamp) | Commit timestamp of the transaction that superseded/deleted this version (`0` if current). |
| `creator` | `txn_id_t` | The unique ID of the transaction that created this version. |

#### MVCC Visibility Rule
A version `V` is visible to a transaction with snapshot timestamp `T_snap` if and only if:
$$\text{xmin} \le T_{\text{snap}} \quad \text{AND} \quad (\text{xmax} = 0 \ \ \text{OR} \ \ \text{xmax} > T_{\text{snap}})$$

### 3. Locking & Deadlock Resolution
* **Lock Management**: Exclusive locks (`X-locks`) are managed in a central lock map (`xlock_`). They prevent concurrent writers from modifying the same record.
* **Dependency Graph**: A `waits_for_` map dynamically records transaction blocks (e.g. `T_waiter -> T_holder`).
* **DFS Cycle Detection**: Every time a write is blocked, a Depth-First Search is initiated. If a path returns to the originating transaction, a deadlock is detected.
* **Youngest Victim Abort**: The DFS gathers all transaction IDs involved in the cycle. The transaction with the largest ID (the youngest one) is chosen as the victim and automatically aborted (`abort_internal`), freeing its held locks and resolving the deadlock.

### 4. Commit Protocol (First-Updater-Wins)
On a request to `commit(tx)`:
1. **Validation Phase**: For each key in the local buffer, the manager inspects the latest version in the global store. If the latest version has `xmin > tx.snapshot`, it implies another transaction committed a write to this key after `tx` started.
2. **Conflict Actions**: The transaction is immediately aborted and returns `Result::SerializationFailure` to prevent lost updates.
3. **Write Phase**: If validation passes, a new global commit timestamp is assigned. Pending writes are appended to the store (marking the `xmax` of the old versions), all locks are released, and the transaction transitions to `Committed`.

### 5. Garbage Collection (`gc()`)
The `gc()` routine runs periodically to clean up dereferenced, obsolete versions:
1. The manager identifies the oldest snapshot timestamp among all active transactions (`oldest_snapshot`).
2. Any version `V` where `xmax != 0` and `xmax <= oldest_snapshot` is safely pruned, since no current or future transaction will ever need to read it.

---

## Demo Scenarios Verified in `main.cpp`

The driver runs **6 comprehensive scenarios** replicating real-world database transactions:

1. **MVCC Snapshot Isolation**: Verification that a long-running reader continues to see initial data after another writer commits updates to the same record.
2. **Tombstone Visibility**: Verification that a logically deleted record disappears for new transactions but remains visible to older active transactions.
3. **Strict 2PL**: Verification that a second transaction trying to write to a locked key blocks with a `LOCK_WAIT` result, and can only succeed after the lock-holding transaction finishes.
4. **Deadlock Detection**: Replicating a circular wait (transferring Alice $\rightarrow$ Bob and Bob $\rightarrow$ Alice simultaneously) and confirming that the youngest transaction is aborted.
5. **First-Updater-Wins**: Simulating two concurrent transactions reading the same balance, where only the first commit succeeds while the second fails with a `SERIALIZATION_FAILURE`.
6. **Garbage Collection (`gc()`)**: Demonstrating that older versions are successfully swept and reclaimed after long-running readers complete, while keeping live versions intact.

---

## Complexity Profile

| Operation | Time Complexity | Details |
| :--- | :--- | :--- |
| **Read** | $O(L)$ | $L$ is the length of the version chain (typically very short). |
| **Write** | $O(1 + V_D)$ | $V_D$ is the size of the dependency graph searched during DFS. |
| **Commit** | $O(K)$ | $K$ is the number of keys modified by the transaction. |
| **GC** | $O(N)$ | $N$ is the total number of versions stored in the database. |

---

## Execution Output

All scenarios execute and pass invariants successfully:

```text
=== 0) Seed alice=1000, bob=500, carol=750 ===
  [pass] seed committed

=== 1) MVCC snapshot isolation — readers see their own snapshot ===
  [pass] reader snapshot sees 1000
  [pass] writer locks alice
  [pass] writer commits alice=1500
  [pass] reader still sees alice=1000
  [pass] fresh reader sees alice=1500

=== 2) Tombstones — delete is visible to new readers only ===
  [pass] older reader sees carol=750
  [pass] delete carol
  [pass] delete committed
  [pass] old snapshot still sees carol
  [pass] new snapshot sees carol as gone

=== 3) Strict 2PL — second writer blocks until first finishes ===
  [pass] T1 takes X-lock on bob
  [pass] T2 blocks -> LOCK_WAIT
  [pass] T1 aborted -> released lock
  [pass] T2 retries -> Ok
  [pass] T2 commits bob=700
  [pass] bob = 700

=== 4) Deadlock — A->B and B->A transfers form a cycle ===
  [pass] T_AB locks alice
  [pass] T_BA locks bob
  [pass] T_AB waits for bob
  T_BA wants alice -> ABORTED; victim = T12
  [pass] younger txn is the deadlock victim
  [pass] victim is T_BA (higher id)
  [pass] T_BA aborted
  [pass] T_AB grabs bob
  [pass] T_AB commits

=== 5) First-updater-wins — second commit hits SERIALIZATION_FAILURE ===
  [pass] T1 reads alice=1400
  [pass] T2 reads alice=1400
  [pass] T1 writes alice=2000
  [pass] T2 write blocked by T1's lock
  [pass] T1 commits
  [pass] T2 takes lock after T1 release
  T2 commit -> SERIALIZATION_FAILURE
  [pass] T2's commit rejected: alice changed under its snapshot
  [pass] alice = 2000 (first updater won)

=== 6) gc() — reclaim dead versions below the oldest snapshot ===
  alice chain before gc: [xmin=1 xmax=2 1000 by T1] [xmin=2 xmax=10 1500 by T3] [xmin=10 xmax=11 1400 by T11] [xmin=11 xmax=0 2000 by T13] 
  versions: 9 -> 3  (pruned 6)
  alice chain after  gc: [xmin=11 xmax=0 2000 by T13] 
  [pass] gc reclaimed at least one dead version
  [pass] version count is consistent after gc
  [pass] alice survives gc
  [pass] bob survives gc

Manager stats: 0 live txns, 0 held locks, 3 versions.
All transaction-manager checks passed.
```
