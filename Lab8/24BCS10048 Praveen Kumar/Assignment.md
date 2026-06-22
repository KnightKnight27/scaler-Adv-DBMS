# Lab 8: Transaction Manager (MVCC + Strict 2PL)

**Course:** Advanced DBMS (Scaler)
**Author:** Praveen Kumar 24bcs10048
**Date:** 2026-06-22

---

## 1. Objective

Implement a compact in-memory transaction manager in C++ combining:
1. MVCC (Multi-Version Concurrency Control) for reads
2. Strict Two-Phase Locking (S2PL) for writes
3. Deadlock detection via DFS on a waits-for graph
4. Lost-update prevention (first-updater-wins)
5. Version garbage collection

---

## 2. Build and Run

```bash
g++ -std=c++17 -O2 -pthread -o txn_manager txn_manager.cpp
./txn_manager
```

Or: `make run`

---

## 3. Background: Why Combine MVCC and 2PL?

### Problem 1: Concurrent Reads and Writes

Without any concurrency control, two transactions running simultaneously can interfere:
- **Dirty read**: T2 reads data written by T1 before T1 commits. T1 later aborts -- T2 read garbage.
- **Non-repeatable read**: T2 reads a row, T1 updates it and commits, T2 reads the row again and gets a different value.
- **Lost update**: T1 and T2 both read a value, both compute a new value based on the old one, and both write back -- T1's update is overwritten by T2.

### Solution: MVCC + 2PL

MVCC solves the read side: instead of overwriting rows, writes create new versions. A reader gets a consistent snapshot of all data as of the moment its transaction started. Reads never block on writes, and writes never block on reads.

2PL solves the write side: before modifying a row, a transaction must acquire an exclusive lock. Before reading, it acquires a shared lock. This prevents two writers from racing, and prevents a writer from modifying data a reader is currently using.

---

## 4. Data Structures

### TxnRecord

```
struct TxnRecord {
    tid          : TxnId          -- unique transaction ID
    snap         : Stamp          -- commit counter at start()
    commit_stamp : Stamp          -- set when the transaction commits
    state        : Running | Committed | Aborted
    shrinking    : bool           -- 2PL shrinking phase active
}
```

### RowVersion (per key, stored as newest-first list)

```
struct RowVersion {
    data        : string    -- the stored value
    creator     : TxnId     -- which transaction wrote this version
    invalidator : TxnId     -- which transaction superseded it (0 = live)
    deleted     : bool      -- true = tombstone (row was erased)
}
```

### LockEntry

```
struct LockEntry {
    holder : TxnId
    kind   : Shared | Exclusive
}
```

### Waits-For Graph

`map<TxnId, set<TxnId>>`: maps each waiting transaction to the set of transactions it is blocked by. Used for deadlock detection.

---

## 5. MVCC Visibility Rule

A version V is **visible to reader transaction R** if and only if:

**Creator is visible:**
- `V.creator == R` (R reads its own uncommitted writes), OR
- V.creator has committed AND `V.creator.commit_stamp <= R.snap`

**AND invalidator is not visible:**
- `V.invalidator == 0` (version is still live), OR
- `V.invalidator == R` (NOT: R invalidated it, so it's hidden from R itself), meaning R should not see it either... actually: if invalidator is R, the version was superseded by R's own write, so R sees the newer version instead.
- More precisely: invalidator is NOT visible when: `invalidator == R` OR (`invalidator.state == Committed AND invalidator.commit_stamp <= R.snap`)

In formula:

```
visible(V, R) =
    ( V.creator == R OR (V.creator.committed AND V.creator.commitStamp <= R.snap) )
    AND
    NOT ( V.invalidator == R
          OR (V.invalidator != 0
              AND V.invalidator.committed
              AND V.invalidator.commitStamp <= R.snap) )
```

The `snap` is the global commit counter at the moment `start()` runs. The key insight: snapshotting on the **commit counter** (not the transaction ID) handles out-of-order commits correctly. A transaction with a smaller ID that commits later will have a larger `commit_stamp` and will correctly be excluded from earlier snapshots.

### Example

```
Global stamp at T2.start(): 3
T1 commits at stamp 4 (after T2 started).

T1's version has commit_stamp = 4.
T2's snap = 3.
4 > 3, so T2 cannot see T1's write. Correct.
```

---

## 6. Strict Two-Phase Locking (S2PL)

### The Two Phases

```
Growing phase:   transaction can acquire new locks (S or X)
Shrinking phase: transaction can only release locks, no new acquisitions
```

In Strict 2PL, the shrinking phase begins only at commit or abort (when all locks are released at once). This prevents cascading aborts -- if T1 writes data and T2 reads it under T1's lock, T1 must hold the lock until it commits. T2 cannot read garbage.

### Lock Compatibility Matrix

| Held \ Requested | Shared (S) | Exclusive (X) |
|------------------|-----------|---------------|
| Shared (S)       | Compatible | Conflict |
| Exclusive (X)    | Conflict | Conflict |

Multiple shared locks on the same key are allowed (many readers). An exclusive lock blocks everyone.

### Lock Upgrade

If a transaction holds a Shared lock and wants to write the same key, it upgrades to Exclusive. The upgrade is allowed only if no other transaction holds a Shared lock on that key.

---

## 7. Deadlock Detection

### Waits-For Graph

When transaction A requests a lock held by transaction B, we record the edge `A -> B` in the waits-for graph. A cycle means A is waiting for B which is (directly or indirectly) waiting for A -- a deadlock.

```
Example: T1 holds X on "a", T2 holds X on "b"
T1 requests X on "b" -> edge T1 -> T2
T2 requests X on "a" -> edge T2 -> T1

Waits-for graph:
  T1 -> T2
  T2 -> T1   <- cycle! Deadlock.
```

### Detection: DFS

Starting from the waiting transaction, perform a depth-first search over the waits-for graph. If we reach the starting node again, a cycle exists.

```
dfs(start, cur):
    mark cur visited
    for each nxt in waits_for[cur]:
        if nxt == start: return CYCLE FOUND
        if nxt not visited: dfs(start, nxt)
```

### Victim Selection

When a cycle is detected, the **youngest transaction** (highest TxnId) is killed. Youngest transactions have done the least work on average, so killing them minimizes wasted effort. The victim's state is set to `Aborted`, its locks are released, and it receives a `TxnFailure` exception.

---

## 8. Lost Update Prevention

Strict 2PL on its own does not prevent all lost updates under snapshot isolation. The scenario:

```
T1 and T2 both start at snap=5.
T1 writes "counter = 11", commits at stamp=6.
T2 also wants to write "counter = 11" (it read 10 at snap=5).
T2 takes the X lock (T1 already released it at commit).
T2 writes without knowing T1 already changed counter.
T1's update is lost.
```

**First-updater-wins rule**: after acquiring the X lock, `store_()` rescans the version chain. If any version was written by a committed transaction whose `commit_stamp > my.snap`, it throws:

```
could not serialize access due to concurrent update
```

This is the same error message PostgreSQL uses. The caller is expected to abort and retry.

---

## 9. Garbage Collection

Every committed write creates a new version and marks the old one with an `invalidator`. Over time, old versions accumulate. GC removes versions that are invisible to all currently running transactions.

A version is **dead** if its `invalidator` has committed AND the invalidator's `commit_stamp` is less than the minimum snapshot of all running transactions. No running transaction will ever see this version again.

```
min_snap = min(T.snap for all running T)

dead(V) = V.invalidator != 0
          AND V.invalidator.committed
          AND V.invalidator.commit_stamp < min_snap
```

---

## 10. Demo Scenarios

| Demo | What is tested |
|------|----------------|
| 1 | Snapshot isolation: T3 (started before T1) cannot see T1's write; T2 (started after) can |
| 2 | Dirty read blocked: T2 cannot see T1's write before T1 commits |
| 3 | Repeatable read: T1 sees same value both times even after T2 commits a new version |
| 4 | S2PL write serialization: second writer cannot clobber first's value |
| 5 | Lost update caught: first-updater-wins rejects second writer after snap mismatch |
| 6 | Tombstone: T2 (started before T1's delete committed) still sees the row; T3 (after) does not |
| 7 | GC: 3 writes create 3 versions; after all txns commit, GC prunes old ones |

---

## 11. How PostgreSQL Does It

| Concept | This implementation | PostgreSQL |
|---------|-------------------|-----------|
| Version chain | `list<RowVersion>` per key | Heap tuples with xmin/xmax fields |
| Snapshot | `global_stamp` at start | Transaction ID + xid snapshot array |
| Visibility | commit_stamp vs snap | xmin <= snapshot AND (xmax == 0 OR xmax > snapshot) |
| Locking | in-memory LockEntry map | `pg_locks` in shared memory |
| Deadlock detection | DFS, youngest victim | `DeadLockCheck()` in `lock.c`, random victim |
| GC | version pruning | VACUUM/autovacuum reclaims dead tuples |
| Lost update | first-updater-wins check | `CheckForSerializationFailure()` |

The core ideas are identical. PostgreSQL adds multi-process safety (everything in shared memory), durability (WAL), and scale (millions of concurrent transactions), but the algorithms are the same.

---

## 12. Sample Output

```
[Demo 1] Basic read/write + snapshot isolation
------------------------------------------------------------
    [TXN 1] started  (snap=0)
    [TXN 2] started  (snap=0)
    [TXN 2] committed (stamp=1)
    [TXN 3] started  (snap=1)
  T2 reads x (T1 committed before T2 started) = "hello"
  T3 reads x (T3 started before T1 committed)  = NULL

[Demo 2] Dirty read prevention
------------------------------------------------------------
    [TXN 1] started  (snap=1)
    [TXN 2] started  (snap=1)
  T2 reads balance (T1 not committed -- must be NULL) = NULL
    [TXN 1] aborted

[Demo 5] Lost update prevention (first-updater-wins)
------------------------------------------------------------
  T1 reads counter = "10"
  T2 reads counter = "10"
  T1 committed counter = 11
  T2 caught: could not serialize access due to concurrent update
    [TXN 2] aborted

[Demo 7] Garbage collection
------------------------------------------------------------
  Versions before GC: 3
  Versions pruned   : 2
  Versions after GC : 1
```

---

## 13. Files in This Submission

| File | Description |
|------|-------------|
| `txn_manager.cpp` | Full transaction manager implementation |
| `Makefile` | Build instructions |
| `README.md` | Quick-start guide |
| `Assignment.md` | This document |

---

## 14. References

- Bernstein, P.A. & Goodman, N. "Concurrency Control in Distributed Database Systems." ACM Computing Surveys, 1981.
- Weikum, G. & Vossen, G. *Transactional Information Systems*, Ch. 5 (2PL), Ch. 7 (MVCC)
- PostgreSQL documentation: https://www.postgresql.org/docs/current/mvcc.html
- PostgreSQL source: `src/backend/storage/lock/lock.c`, `src/backend/storage/lmgr/deadlock.c`
- Ramakrishnan, R. & Gehrke, J. *Database Management Systems*, Ch. 17 (Concurrency Control)
