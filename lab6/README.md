# Lab 6: Transaction Manager — MVCC + Strict 2PL + Deadlock Detection

**Name:** Rachit S  
**Roll Number:** 24bcs10139  
**Course:** Advanced Database Management Systems (AdvDBMS)

---

## 1. Overview
Modern relational database management engines combine multiple concurrency control mechanisms to guarantee ACID properties. This lab presents a C++ **Transaction Manager** combining:
1. **Multi-Version Concurrency Control (MVCC):** Reads are separated from writes. Every write creates a new, timestamped version of a row, allowing readers to view a consistent snapshot without lock blocking.
2. **Strict Two-Phase Locking (2PL):** Guarantees serializability of concurrent write operations by acquiring locks in a growing phase and releasing them together at commit/abort time.
3. **Deadlock Detection:** Monitors transactions using a waits-for dependency graph. Cycle detection via Depth-First Search (DFS) aborts the transaction causing a dependency loop.

---

## 2. Component Architecture

```
                       Transaction Requests
                               │
                               ▼
               +───────────────────────────────+
               |      Transaction Manager      |
               +───────────────────────────────+
                 /                           \
                v                             v
  +─────────────────────────+     +─────────────────────────+
  | Lock Manager (2PL/S2PL) |     |  MVCC Heap Engine       |
  +─────────────────────────+     +─────────────────────────+
  | - SHARED / EXCLUSIVE    |     | - Row Version Chains    |
  | - Lock queues per key   |     | - Visibility rules      |
  | - Waits-for Graph       |     |   (xmin/xmax checks)    |
  | - Cycle DFS Detection   |     +─────────────────────────+
  +─────────────────────────+
```

---

## 3. Concurrency Rules & Invariants

### MVCC Snapshot Visibility Invariant
A version `V` created by `xmin` and deleted by `xmax` is visible to a reader transaction `T` (at snapshot time `snapshot_xid`) if and only if:
- `xmin` is committed AND `xmin < snapshot_xid` (or `xmin == T.id` for own writes).
- `xmax` is `0` (not deleted) OR `xmax` is aborted OR `xmax` is committed and `xmax >= snapshot_xid` (not deleted at snapshot time).

### Strict Two-Phase Locking (Strict 2PL) Invariant
1. **Growing Phase:** Locks can be acquired but never released.
2. **Shrinking Phase:** Locks can be released but never acquired.
3. **Strict Constraint:** The shrinking phase is deferred to the end of the transaction (Commit or Abort), avoiding cascading rollbacks.

### Deadlock Resolution
- The dependency graph `waits_for` records when transaction $T_1$ waits for a lock held by $T_2$.
- When a new edge is added, DFS looks for a cycle.
- If a cycle is detected, the transaction that requested the conflicting lock is aborted (`DeadlockException`) and rolled back.

---

## 4. Compilation & Verification

To compile the transaction manager (requires `-pthread` for multi-threaded thread testing):
```bash
g++ -std=c++17 -pthread lab6/txmgr.cpp -o txmgr_test
./txmgr_test
```
This runs four scenario tests, verifying snapshot isolation, shared locks, lock block queuing, and cycles resolution.
