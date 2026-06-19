# Lab 8 — In-Memory Transaction Manager (MVCC + Strict 2PL)

**Student Name:** Anushka Jain
**Roll Number:** 24BCS10193
**Course:** Advanced DBMS
**Institution:** Scaler School of Technology

---

## Overview

This repository contains a header-only C++17 implementation of a single-threaded **In-Memory Transaction Manager** that mirrors the concurrency control design used in modern relational database engines like PostgreSQL. It combines multi-version concurrency control (MVCC) for reads with strict two-phase locking (Strict 2PL) for writes.

### Core Architecture Design

1. **MVCC Snapshot Reads**: Readers never block and never acquire locks. When a transaction is initialized via `begin()`, it captures a logical timestamp as its read snapshot. All subsequent reads are evaluated against this snapshot — they see only versions committed before the snapshot was taken.

2. **Strict 2PL Writes**: Writers must acquire an exclusive lock (`X-lock`) on any key they intend to modify. This lock is retained until the transaction either commits or aborts (the "strict" part of Strict 2PL), preventing dirty writes and ensuring write serializability.

3. **Deadlock Detection**: Blocked write operations are tracked in a `waits_for_` dependency map. Each time a write is blocked, the manager performs an iterative path traversal to check for cycles. If a cycle is found, the transaction with the highest ID (the youngest one) is selected as the victim and aborted to break the deadlock.

4. **First-Updater-Wins Commit**: At commit time, the manager validates each written key against the global version store. If any key has a newer committed version (i.e., `xmin > snapshot`), a concurrent transaction updated it after this transaction's snapshot — the committing transaction is aborted with `SerializationFailure`.

5. **Garbage Collection (`gc()`)**: Computes the oldest snapshot among all currently active transactions. Any version record whose `xmax` is non-zero and falls at or below this floor can never be seen by any active or future transaction, and is safely pruned to reclaim memory.

---

## Directory Structure

```text
Lab-08/
├── CMakeLists.txt      # CMake build config with C++17 and warnings enabled
├── main.cpp            # Assert-driven driver covering 6 transactional scenarios
├── txn_manager.hpp     # Header-only transaction manager implementation
└── README.md           # Project documentation
```

> **Note:** There is no `txn_manager.cpp`. The entire implementation lives in `txn_manager.hpp` as a header-only library. This is intentional — the class uses C++ templates and inline methods that do not require a separate compilation unit.

---

## Build & Execution Instructions

### Option 1: Using CMake (Recommended)

```bash
# Configure the build directory
cmake -S . -B build

# Compile
cmake --build build

# Run
./build/txn_manager_demo
```

### Option 2: Direct Compilation

```bash
# Compile with C++17
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 main.cpp -o txn_manager_demo

# Run
./txn_manager_demo
```

---

## Technical Specifications

### 1. Why MVCC for Reads and Strict 2PL for Writes?

- **High Read-to-Write Ratio**: OLTP workloads are dominated by reads. Under pure 2PL, readers block writers and vice versa, creating contention.
- **Non-Blocking Reads**: MVCC solves this by maintaining multiple historical versions of each key. A reader always retrieves a snapshot-consistent value without acquiring any lock.
- **Write Serialization**: Strict 2PL ensures that concurrent writes to the same key are linearized — only one writer can hold the lock at a time, and it holds it until the transaction ends.

### 2. Multi-Version Data Model

Each key maps to an ordered chain of `VersionRecord` entries:

| Field       | Type          | Description                                                    |
| :---------- | :------------ | :------------------------------------------------------------- |
| `val`       | `std::string` | The stored value for this version.                             |
| `deleted`   | `bool`        | True when this version represents a logical deletion (tombstone). |
| `xmin`      | `ts_t`        | Commit timestamp of the transaction that created this version. |
| `xmax`      | `ts_t`        | Commit timestamp of the transaction that superseded it (`0` = still live). |
| `author`    | `txn_id_t`    | ID of the transaction that wrote this version.                 |

#### MVCC Visibility Rule

A version `V` is visible to a transaction with snapshot timestamp `T_snap` if and only if:

$$\text{xmin} \le T_{\text{snap}} \quad \text{AND} \quad (\text{xmax} = 0 \ \ \text{OR} \ \ \text{xmax} > T_{\text{snap}})$$

The version chain is scanned from newest to oldest; the first matching version is returned.

### 3. Locking and Deadlock Resolution

- **Lock Map (`xlock_map_`)**: Maps each locked key to the transaction ID that holds its exclusive lock.
- **Waits-For Map (`waits_for_`)**: Records `waiter → holder` relationships when a write is blocked.
- **Cycle Detection**: When a new `waits_for_` edge is added, the manager traverses the dependency chain iteratively. If the path loops back to a visited node, a cycle exists.
- **Victim Selection**: The transaction IDs participating in the cycle are collected; the one with the highest ID (youngest transaction) is aborted via `kill_txn()`, freeing its locks and resolving the deadlock.

### 4. Commit Protocol (First-Updater-Wins)

On `commit(tx)`:

1. **Validation Phase**: For each key in the transaction's write buffer, inspect the latest version in `version_store_`. If `chain.back().xmin > tx.snapshot`, another transaction committed a newer version after this transaction started — abort with `SerializationFailure`.
2. **Write Phase**: Assign a new global commit timestamp. For each pending write, seal the current head version (`xmax = commit_ts`) and append a new `VersionRecord` with `xmin = commit_ts` and `xmax = 0`.
3. **Cleanup**: Release all held locks, clear the `waits_for_` edges, and set state to `Committed`.

### 5. Garbage Collection (`gc()`)

1. Compute `floor = min(snapshot)` across all active transactions.
2. For every key in `version_store_`, drop any `VersionRecord` where `xmax != 0 && xmax <= floor`.
3. Return the count of pruned records.

This is safe because no active snapshot can ever be ≤ `floor`, so those versions can never be read again.

---

## Demo Scenarios Verified in `main.cpp`

The driver runs **6 deterministic scenarios** against a simulated bank database (accounts: alice, bob, carol):

1. **MVCC Snapshot Isolation**: A long-running reader continues to see `alice=1000` even after a concurrent writer commits `alice=1500`.

2. **Tombstone Visibility**: After `carol` is deleted, an older snapshot still reads `carol=750`, while a newer snapshot sees `<none>`.

3. **Strict 2PL — Lock Contention**: T2 receives `LOCK_WAIT` when trying to write a key locked by T1. After T1 aborts (releasing the lock), T2 retries and succeeds.

4. **Deadlock Detection**: Transfers A→B and B→A form a waits-for cycle. The manager detects it and aborts the younger transaction (higher ID) as the victim.

5. **First-Updater-Wins**: Two concurrent transactions both read `alice=1400`. T1 commits `alice=2000`. T2's commit is rejected with `SERIALIZATION_FAILURE` because alice was updated after T2's snapshot.

6. **Garbage Collection**: After all long-running readers finish, `gc()` prunes all obsolete version records while keeping the latest live version of each key intact.

---

## Complexity Profile

| Operation  | Time Complexity | Notes                                           |
| :--------- | :-------------- | :---------------------------------------------- |
| `read`     | O(L)            | L = length of version chain (typically short).  |
| `write`    | O(D)            | D = depth of waits-for graph during DFS.        |
| `commit`   | O(K)            | K = number of keys in the write buffer.         |
| `gc`       | O(N)            | N = total number of version records in store.   |

---

## Expected Execution Output

```text
=== 0) Seed: alice=1000, bob=500, carol=750 ===
  [pass] seed transaction committed

=== 1) MVCC Snapshot Isolation ===
  [pass] old_reader sees alice=1000 before write
  [pass] updater acquires lock on alice
  [pass] updater commits alice=1500
  [pass] old_reader still sees alice=1000 (snapshot isolation)
  [pass] new_reader sees alice=1500 after commit

=== 2) Tombstone Visibility ===
  [pass] pre_delete reader sees carol=750
  [pass] deleter removes carol
  [pass] delete committed
  [pass] pre_delete snapshot still sees carol=750
  [pass] post_delete snapshot sees carol as <none>

=== 3) Strict 2PL — Lock Contention ===
  [pass] T1 acquires X-lock on bob
  [pass] T2 blocked: LOCK_WAIT on bob
  [pass] T1 aborted, lock released
  [pass] T2 acquires bob after T1 aborts
  [pass] T2 commits bob=700
  [pass] confirmed bob=700

=== 4) Deadlock Detection ===
  [pass] T_ab locks alice
  [pass] T_ba locks bob
  [pass] T_ab waits for bob (held by T_ba)
  T_ba wants alice -> ABORTED  victim=T12
  [pass] deadlock detected: T_ba (younger) is the victim
  [pass] victim ID is T_ba
  [pass] T_ba state = Aborted
  [pass] T_ab acquires bob after T_ba aborted
  [pass] T_ab commits

=== 5) First-Updater-Wins ===
  [pass] T1 reads alice=1400
  [pass] T2 reads alice=1400
  [pass] T1 writes alice=2000 (holds lock)
  [pass] T2 blocked on alice (held by T1)
  [pass] T1 commits alice=2000
  [pass] T2 acquires alice lock after T1 releases
  T2 commit result -> SERIALIZATION_FAILURE
  [pass] T2 rejected: alice changed after T2's snapshot
  [pass] alice = 2000 (first updater won)

=== 6) Garbage Collection ===
  alice chain before gc: [xmin=1 xmax=2 1000 by T1] [xmin=2 xmax=10 1500 by T3] [xmin=10 xmax=11 1400 by T11] [xmin=11 xmax=0 2000 by T13]
  versions: 9 -> 3  (pruned 6)
  alice chain after  gc: [xmin=11 xmax=0 2000 by T13]
  [pass] gc() reclaimed at least one obsolete version
  [pass] version count is consistent post-gc
  [pass] alice=2000 survives gc
  [pass] bob=750 survives gc

Final stats: 0 live txns, 0 held locks, 3 versions.
All transaction-manager scenarios passed.
```