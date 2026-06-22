# LAB 6 — TRANSACTION MANAGER: MVCC + STRICT 2PL + DEADLOCK DETECTION

> Roll Number: **10075** &nbsp;&nbsp;|&nbsp;&nbsp; Name: **Nase Anishka**

This lab builds a transaction manager that mirrors the core of PostgreSQL's concurrency architecture:
1. **MVCC** — every write creates a new row version; readers see a consistent snapshot without blocking writers.
2. **Strict Two-Phase Locking (2PL)** — lock acquisition is bounded to the "growing" phase; all locks released atomically at commit/abort.
3. **Deadlock detection** — waits-for graph cycle detection to abort one transaction when a cycle forms.

---

## WHAT I BUILT

| Component | Implementation |
|-----------|----------------|
| Transaction table | `g_transactions` (TxID → status + snapshot_xid + in_shrinking flag) |
| MVCC heap | `g_heap` (RowKey → `list<RowVersion>`) — version chain per row, newest first |
| Lock manager | `g_lock_table` (RowKey → `LockQueue`) — FIFO queue of SHARED/EXCLUSIVE requests |
| Waits-for graph | `g_waits_for` (TxID → set of blocking TxIDs) — DFS cycle detection |
| Public API | `TransactionManager`: `begin`, `read`, `insert`, `update`, `remove`, `commit`, `abort` |

---

## FILES IN THIS FOLDER

| File | Purpose |
|------|---------|
| `main.cpp` | Full transaction manager implementation + 4 demo scenarios |
| `CMakeLists.txt` | CMake build config (links `-pthread`) |
| `.gitignore` | Ignore build artefacts |
| `run_output.txt` | Captured output from a real run |
| `README.md` | This file |

---

## HOW TO BUILD AND RUN

```bash
# Direct g++
g++ -std=c++17 -pthread -Wall -o txmgr main.cpp && ./txmgr

# CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/txmgr
```

---

## CONCEPTS

### MVCC version visibility rule

A version `{value, xmin, xmax}` is visible to transaction T if:

```
xmin_ok  =  (xmin == T.id)                           // own write
          || (committed(xmin) && xmin < T.snapshot)  // committed before my snapshot

visible  =  xmin_ok
          && (xmax == 0                               // not yet deleted
           || xmax == T.id                            // deleted by me
           || !(committed(xmax) && xmax < T.snapshot))// delete not yet committed
```

### Two-Phase Locking phases

```
GROWING phase:   acquire SHARED or EXCLUSIVE locks — no releases
SHRINKING phase: release all locks at commit/abort — no new acquisitions
```

Strict 2PL collapses the shrinking phase to a single instant at commit/abort. This avoids cascading aborts.

### Deadlock
T1 holds lock on A, wants B; T2 holds lock on B, wants A → cycle in waits-for graph → abort the newer transaction.

---

## ARCHITECTURE

```
Application
    │
    ▼
TransactionManager.begin() / read() / insert() / update() / remove() / commit() / abort()
    │
    ├─► Lock Manager (Strict 2PL)
    │       acquire_lock(): growing phase check → FIFO queue → conflict check → wait / grant
    │       release_locks(): mark in_shrinking, remove all requests, notify_all waiters
    │       Deadlock: g_waits_for graph updated on every wait → DFS cycle check → DeadlockException
    │
    └─► MVCC Heap (version chain per row)
            INSERT  → push {value, xmin=xid, xmax=0}
            UPDATE  → stamp old xmax=xid, push new version
            DELETE  → stamp old xmax=xid
            READ    → walk chain, return first version satisfying visibility rule
            ABORT   → hide own inserts (xmax=xid), undo own deletes (xmax=0)
```

---

## DEMO SCENARIOS

### Scenario 1 — MVCC Snapshot Isolation

```
t1: INSERT balance=1000 → COMMIT
t2: BEGIN (snapshot after t1)
t3: BEGIN
t3: UPDATE balance=2000 → COMMIT
t2: READ balance → 1000  (t3 committed after t2 started — not in t2's snapshot)
t2: COMMIT
```

Output:
```
[TX 1] COMMITTED
[TX 3] COMMITTED
  [TX 2] READ balance = 1000
[TX 2] COMMITTED
```

t2 sees 1000, not 2000 — its snapshot predates t3. This is snapshot isolation: readers never block on concurrent writers.

### Scenario 2 — Concurrent Shared Locks

Two transactions hold SHARED locks on the same key simultaneously — no conflict. Both see the latest committed value (2000 from Scenario 1's t3).

### Scenario 3 — Exclusive Lock Blocks Shared (thread demo)

t6 holds EXCLUSIVE on "balance" (from `UPDATE`). A reader thread t7 calls `acquire_lock` and blocks on `cv.wait`. When t6 commits, `release_locks` removes t6's entry and calls `notify_all`, unblocking t7. t7 then sees the new value 3000.

```
[TX 7] waiting for shared lock on balance...
[TX 6] COMMITTED
[TX 7] READ balance = 3000
[TX 7] COMMITTED
```

### Scenario 4 — Deadlock Detection

```
t8 holds EXCLUSIVE on "A"
t9 holds EXCLUSIVE on "B"

Thread: t8 tries to get EXCLUSIVE on "B" → blocked (t9 holds it)
         → g_waits_for[t8] = {t9}
         → no cycle yet (t9 not waiting)

Main: t9 tries to get EXCLUSIVE on "A" → blocked (t8 holds it)
       → g_waits_for[t9] = {t8}
       → DFS from t9: t9→t8→t9 → CYCLE DETECTED
       → throw DeadlockException(t9)
       → t9 aborted

Thread: t8's wait unblocked (t9 released B via abort)
       → t8 gets B, commits
```

Output:
```
  Deadlock detected, aborting tx 11
[TX 11] ABORTED
[TX 10] COMMITTED
```

---

## MVCC vs 2PL — WHY BOTH?

| Property | MVCC alone | 2PL alone | MVCC + Strict 2PL |
|----------|------------|-----------|-------------------|
| Read-write contention | None (readers use snapshots) | Readers block writers | None (readers use snapshots) |
| Write-write contention | Lost update possible | Serializable via exclusive locks | Serializable |
| Serializability | Snapshot Isolation only | Full serializability | Full serializability |
| Deadlock | N/A | Possible | Possible — needs detection |
| Old version cleanup | Needs VACUUM | N/A | Needs VACUUM |

PostgreSQL uses MVCC + Serializable Snapshot Isolation (SSI) — a refinement that detects dangerous read-write anti-dependencies without the throughput cost of full 2PL.

---

## KEY TAKEAWAYS

- MVCC creates a new row version per write; readers walk the version chain and apply the visibility rule against their snapshot XID — no blocking between readers and writers.
- Strict 2PL holds all locks until commit/abort. The shrinking phase is instantaneous — this avoids cascading aborts.
- The combination eliminates read-write contention (MVCC) while preserving write-write serializability (2PL).
- Deadlock detection via waits-for graph DFS is O(V+E) per lock request. PostgreSQL runs a similar check periodically (not per-request) for performance.
- ABORT must undo MVCC writes: own inserts are hidden (xmax=xid), own deletes are reversed (xmax=0).
