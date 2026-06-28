# Lab 6 — Transaction Manager: MVCC + Strict Two-Phase Locking + Deadlock Detection

**Student:** Indrajeet Yadav | **Roll No:** 23BCS10199

---

## Objective

Build a complete single-node transaction manager that combines three mechanisms used in production databases (especially PostgreSQL):

1. **MVCC** — every write creates a new row version; readers see a consistent snapshot without blocking or being blocked by writers.
2. **Strict Two-Phase Locking (S2PL)** — lock acquisition is limited to the "growing phase"; all locks are released atomically at commit/abort (the "shrinking phase"), guaranteeing serializability and preventing cascading aborts.
3. **Deadlock Detection** — a waits-for graph with DFS cycle detection; the transaction that completes a cycle is aborted.

---

## Build & Run

```bash
g++ -std=c++17 -pthread -Wall -Wextra -O2 txmgr.cpp -o txmgr
./txmgr
```

---

## Core Concepts

### MVCC — Multi-Version Concurrency Control

The fundamental insight: instead of updating a row in-place, every write creates a **new row version**. Old versions are kept alongside new ones until they are no longer needed (until no active transaction has a snapshot old enough to see them — this cleanup is called **VACUUM** in PostgreSQL).

#### Version Chain

```
g_heap["balance"] (a std::list<RowVersion>, newest first):

  RowVersion { value="3000", xmin=T3, xmax=0   }  ← T3 created this; it's the live version
  RowVersion { value="2000", xmin=T2, xmax=T3  }  ← T2 created; T3 superseded it
  RowVersion { value="1000", xmin=T1, xmax=T2  }  ← T1 created; T2 superseded it
```

#### Visibility Rule

A version `(xmin, xmax)` is **visible** to transaction T with snapshot `S` if:

```
xmin_ok:  (xmin == T.id)                        ← T's own write
        OR (committed(xmin) AND xmin < S)        ← older committed write in snapshot

xmax_ok:  xmax == 0                              ← version not deleted
        OR xmax == T.id                          ← T deleted it (not visible to T itself)
        OR NOT (committed(xmax) AND xmax < S)    ← deleter committed after our snapshot
```

#### Snapshot Isolation in Action

```
T1: BEGIN, INSERT balance=1000, COMMIT    (xid=1)
T2: BEGIN                                  (xid=2, snapshot=2: sees xids < 2 committed)
T3: BEGIN, UPDATE balance=2000, COMMIT    (xid=3)
T2: READ balance → sees 1000             (T3.xid=3 ≥ T2.snapshot=2 → T3 not in snapshot)
T2: COMMIT
```

T2 sees a consistent snapshot of the database as it existed when T2 began. T3's write, even though committed before T2's READ, happened after T2's snapshot boundary.

#### MVCC vs Locking for Reads

| Approach | Read behavior | Write behavior |
|----------|--------------|----------------|
| Lock-based | Readers block on writers (shared lock) | Writers block on readers |
| MVCC only | Readers never block (see snapshot) | Last writer wins → lost update |
| MVCC + S2PL | Readers never block writers (snapshot) | Writers hold X locks → serializable |

Pure MVCC without locking has the "lost update" anomaly: two transactions reading the same value and both writing their update based on it — one update is silently lost. Adding exclusive locks for writes prevents this.

---

### Two-Phase Locking (2PL)

#### The Two Phases

```
GROWING PHASE                    SHRINKING PHASE
─────────────────────────────    ──────────────────────────────
Transaction may:                 Transaction may:
  ✓ acquire SHARED lock            ✓ release locks (only at commit/abort)
  ✓ acquire EXCLUSIVE lock         ✗ acquire NEW locks
  ✗ release any lock
```

#### Strict 2PL

In basic 2PL, a transaction enters the shrinking phase as soon as it releases its first lock. This can cause **cascading aborts**: if T2 read data written by T1 (under T1's X lock, briefly released), and T1 aborts, T2 must also abort.

**Strict 2PL** simplifies this: the shrinking phase happens only at commit or abort. All locks are held until the very end. Result:
- No cascading aborts — other transactions can only read committed data (via MVCC snapshots).
- The commit/abort is a single atomic moment: data becomes visible AND all locks are released at once.

#### Lock Compatibility Matrix

| | Held: S | Held: X |
|--|---------|---------|
| **Want: S** | ✓ granted | ✗ blocked |
| **Want: X** | ✗ blocked | ✗ blocked |

Multiple readers can hold shared locks simultaneously. An exclusive lock requires exclusive access — any other holder (S or X) blocks the request.

#### Lock Queue

Each row key has a lock queue — an ordered list of `LockRequest` objects:

```
g_lock_table["balance"].requests:
  { xid=T4, mode=S, granted=true }   ← T4 holds a shared lock
  { xid=T5, mode=S, granted=true }   ← T5 holds a shared lock (S compatible with S)
  { xid=T6, mode=X, granted=false }  ← T6 waiting for X (blocked by T4 and T5)
```

When T4 and T5 commit and release their S locks, the queue is notified (condition variable), T6's request can be granted.

---

### Deadlock Detection via Waits-For Graph

#### The Problem

Transaction T8 holds X lock on "A". Transaction T9 holds X lock on "B".
- T8 tries to lock "B" → blocked by T9
- T9 tries to lock "A" → blocked by T8
- Circular wait: T8 → T9 → T8 → ... → deadlock

No automatic timeout would help here — both transactions would wait forever.

#### Waits-For Graph

```
g_waits_for:
  T8 → {T9}    (T8 is waiting for T9 to release B)
  T9 → {T8}    (T9 is waiting for T8 to release A)
```

A **cycle** in this graph = a deadlock. Detection runs a DFS on the waits-for graph every time a transaction adds a waiting edge.

#### DFS Cycle Detection

```cpp
bool dfs_cycle(node, graph, visited, recursion_stack):
    visited.add(node)
    recursion_stack.add(node)
    for each neighbor in graph[node]:
        if neighbor not in visited:
            if dfs_cycle(neighbor, ...): return true
        elif neighbor in recursion_stack:
            return true  // back edge = cycle
    recursion_stack.remove(node)
    return false
```

Time complexity: O(V + E) where V = active transactions, E = waiting edges. In practice, deadlock cycles involve few transactions and are detected in microseconds.

#### Victim Selection

This implementation aborts the transaction that **completes** the cycle (the one calling `acquire_lock` when the cycle is detected). PostgreSQL uses a more sophisticated policy: abort the "youngest" (highest XID) transaction in the cycle to maximize the amount of work preserved.

---

### ABORT — Undoing MVCC Writes

When a transaction aborts, its MVCC writes must be made invisible:

```
For each RowVersion v in the entire heap:
  if v.xmin == aborted_xid:
    v.xmax = aborted_xid    // hide our INSERTs: xmax=own xid → not visible to anyone
  if v.xmax == aborted_xid AND v.xmin != aborted_xid:
    v.xmax = 0              // undo our DELETEs: restore the version to live
```

This makes the rollback visible to other transactions immediately — they will see the version chain without the aborted transaction's effects.

---

## Data Structures

```cpp
// Transaction table
std::unordered_map<TxID, Transaction>      g_transactions;
//  Transaction: { id, snapshot_xid, status (ACTIVE/COMMITTED/ABORTED), in_shrinking }

// MVCC heap
std::unordered_map<RowKey, std::list<RowVersion>> g_heap;
//  RowVersion: { value, xmin (creator tx), xmax (deleter tx, 0=live) }

// Lock table
std::unordered_map<RowKey, LockQueue>      g_lock_table;
//  LockQueue: { list<LockRequest>, mutex, condition_variable }
//  LockRequest: { xid, mode (S/X), granted }

// Waits-for graph
std::unordered_map<TxID, std::unordered_set<TxID>> g_waits_for;
//  xid → set of xids that this tx is waiting for
```

---

## Demonstration Scenarios

| Scenario | Concept Demonstrated |
|----------|---------------------|
| 1 | MVCC snapshot isolation — T2 sees pre-T3 data despite T3 committing first |
| 2 | Dirty read prevention — uncommitted writes invisible to concurrent readers |
| 3 | Concurrent shared locks — S compat with S, multiple readers at once |
| 4 | X lock blocks S — reader must wait for writer to commit (2PL + MVCC interaction) |
| 5 | Delete + rollback — aborted delete is invisible to later readers |
| 6 | Deadlock detection — T13⇒T14⇒T13 cycle detected, one tx aborted |
| 7 | 2PL violation — cannot acquire new lock after entering shrinking phase |

---

## MVCC + 2PL — Why Both?

| Property | MVCC only | 2PL only | MVCC + Strict 2PL |
|----------|-----------|----------|-------------------|
| Read-write contention | None (snapshots) | High (S blocks X) | None (snapshots) |
| Write-write contention | Lost update anomaly | Serializable | Serializable |
| Cascading aborts | N/A | Possible (early release) | Impossible (S2PL) |
| Deadlock | N/A | Possible | Possible → detected |
| Old version cleanup | Needed (VACUUM) | N/A | Needed (VACUUM) |
| Isolation level | Snapshot Isolation | Serializable | Serializable SI |

---

## Two-Phase Locking Phase Diagram

```
Time →

TX T1: GROW ──────────────────────────── SHRINK ── done
         acquire S(A)  acquire X(B)  acquire S(C)  release all at COMMIT

TX T2: GROW ─────── SHRINK ──────────────── done
         acquire S(A)  release all at ABORT (rolled back)

TX T3:          GROW ─────────────────────────────── SHRINK ── done
                  acquire X(D)  acquire S(E)  acquire S(A)   release all at COMMIT

Note: T3 acquires S(A) after T1 already holds S(A) — this is fine (S compat S).
      If T3 needed X(A), it would block until T1 commits and releases S(A).
```

---

## Key Takeaways

1. **MVCC creates a new row version per write.** Readers walk the version chain and apply the visibility rule against their snapshot XID — no blocking between readers and writers.

2. **Strict 2PL holds ALL locks until commit or abort.** This eliminates cascading aborts and guarantees serializability of concurrent write operations.

3. **The combination (MVCC + S2PL) eliminates read-write contention while preserving write serializability.** This is exactly PostgreSQL's concurrency model (PostgreSQL uses SSI — Serializable Snapshot Isolation — which extends MVCC to detect dangerous anti-dependencies, achieving full serializability without requiring X locks for reads).

4. **Deadlock detection via waits-for graph DFS runs in O(V+E).** PostgreSQL runs a background deadlock detector (backend/storage/lmgr/deadlock.c) that checks periodically rather than on every lock request, trading some deadlock detection latency for lower per-request overhead.

5. **ABORT must undo MVCC writes:** own inserts are hidden (set xmax=own xid), own deletes are reversed (clear xmax to 0). This makes the rollback immediately visible to concurrent transactions.

6. **The shrinking phase in Strict 2PL is instantaneous at commit/abort.** Unlike basic 2PL where a transaction can release some locks early (causing cascading abort risk), in S2PL all locks are released in a single step, making the commit atomic from the perspective of lock visibility.
