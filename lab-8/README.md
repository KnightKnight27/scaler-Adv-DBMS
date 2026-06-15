# Lab 8: Transaction Manager — MVCC + Two-Phase Locking in C++

## Overview

A complete transaction manager implementation combining:
- **MVCC (Multi-Version Concurrency Control)** — every write creates a new row version; readers see consistent snapshots without blocking writers
- **Two-Phase Locking (Strict 2PL)** — transactions acquire locks in a growing phase and release them all at once during shrinking phase (commit/abort)
- **Deadlock Detection** — waits-for graph cycle detection using DFS to abort younger transactions when deadlock is detected

This mirrors the core of PostgreSQL's concurrency architecture.

---

## Key Concepts

### MVCC Version Visibility Rule

A row version created by transaction `xmin` and invalidated by `xmax` is visible to transaction `T` if:
```
✓ xmin is committed AND xmin <= T.snapshot_xid
✓ xmax is 0 (not yet deleted) OR xmax > T.snapshot_xid OR xmax is aborted
```

### Two-Phase Locking Phases

```
GROWING PHASE:    Transaction may acquire new locks, may NOT release any
SHRINKING PHASE:  Transaction may release locks, may NOT acquire new ones
                  (Strict 2PL: shrinking phase only at commit/abort)
```

### Deadlock Detection

When a transaction tries to acquire a lock:
1. Build waits-for graph edges
2. Check for cycles using DFS
3. If cycle found, abort the requesting transaction
4. Clear waits-for edges on lock acquisition or release

---

## Architecture

```
TransactionManager (Public API)
    ├── begin()      → TxID
    ├── read()       → Optional<value>
    ├── insert()
    ├── update()
    ├── remove()
    ├── commit()
    └── abort()
        │
        ├─► MVCC Heap (per-row version chains)
        │   - INSERT  → new version {value, xmin=xid, xmax=0}
        │   - UPDATE  → mark old xmax=xid, new version
        │   - DELETE  → mark visible version xmax=xid
        │   - READ    → walk version chain, apply visibility rule
        │
        ├─► Lock Manager (Strict 2PL)
        │   - acquire_lock() → SHARED or EXCLUSIVE
        │   - release_locks() → all at once on commit/abort
        │   - Deadlock detection → waits-for graph DFS
        │
        └─► Global Transaction Table
            - Track XID, snapshot, status, 2PL phase
```

---

## Compilation & Running

```bash
cd lab-8
make
./txmgr
```

Or with full flags:
```bash
g++ -std=c++17 -pthread -o txmgr main.cpp && ./txmgr
```

---

## Demo Scenarios

### Scenario 1: MVCC Snapshot Isolation
- T1 inserts balance = "1000" and commits
- T2 begins (snapshot sees T1's commit)
- T3 begins and updates balance to "2000", commits
- T2 reads balance — sees "1000" (pre-T3 snapshot, not blocked by T3's write)
- T2 commits

**Key insight:** MVCC readers never block writers; they read from consistent snapshots.

### Scenario 2: Concurrent Shared Locks
- T4 and T5 both read the same key with SHARED locks
- Both locks are granted immediately (no conflict)
- Both commit

**Key insight:** Shared locks don't conflict with each other.

### Scenario 3: Exclusive Lock Blocking
- T6 holds EXCLUSIVE lock on "balance"
- T7 (on separate thread) tries to read → blocked waiting for SHARED lock
- T6 commits → releases lock
- T7 acquires lock and reads (sees T6's committed value)
- T7 commits

**Key insight:** EXCLUSIVE locks block all other locks; condition variable wakes waiters on release.

### Scenario 4: Deadlock Detection
- T8 holds lock on "A", T9 holds lock on "B"
- T8 tries to acquire lock on "B" (waits for T9)
- T9 tries to acquire lock on "A" (waits for T8)
- Waits-for graph has cycle: T8 → T9 → T8
- One transaction (e.g., T8) is aborted with DeadlockException
- Other transaction commits

**Key insight:** DFS cycle detection in waits-for graph prevents deadlocks.

---

## Expected Output

```
=== Scenario 1: MVCC Snapshot Isolation ===
[TX 3] COMMITTED
[TX 4] COMMITTED
  [TX 2] READ balance = 1000

=== Scenario 2: Concurrent Shared Locks ===
  [TX 5] READ balance = 3000
  [TX 6] READ balance = 3000
[TX 5] COMMITTED
[TX 6] COMMITTED

=== Scenario 3: Exclusive Lock + Waiting ===
  [TX 8] waiting for shared lock on balance...
[TX 7] COMMITTED
  [TX 8] READ balance = 3000
[TX 8] COMMITTED

=== Scenario 4: Deadlock Detection ===
  Deadlock detected, aborting tx 10
[TX 10] ABORTED
[TX 11] COMMITTED

All scenarios complete.
```

---

## Implementation Details

### Global State
- `g_next_xid`: atomic counter for transaction IDs
- `g_transactions`: map of TxID → Transaction (status, snapshot, 2PL phase)
- `g_heap`: map of RowKey → version chain (MVCC)
- `g_lock_table`: map of RowKey → lock queue
- `g_waits_for`: graph for deadlock detection

### Synchronization
- `g_tx_mutex`: protects transaction table
- `g_heap_mutex`: protects MVCC heap
- `g_lm_mutex`: protects waits-for graph
- `LockQueue.mu` + `cv`: per-key lock coordination

### MVCC + 2PL Interaction
- MVCC eliminates read-write contention: readers use snapshots, don't block writers
- Strict 2PL ensures write serializability: exclusive locks prevent write-write conflicts
- Combined: serializable schedule with minimal blocking

### Deadlock Handling
- **Detection:** Cycle in waits-for graph → throw DeadlockException
- **Resolution:** Catch exception, abort transaction, release locks
- **Performance:** O(V+E) per lock request where V = # transactions, E = # waits-for edges

---

## MVCC vs 2PL Comparison

| Property               | MVCC alone                                  | 2PL alone                              | MVCC + Strict 2PL                        |
|------------------------|---------------------------------------------|----------------------------------------|------------------------------------------|
| Read-write contention  | None — readers never block writers          | Readers block writers (shared lock)    | None — readers use snapshots             |
| Write-write contention | Last writer wins (lost update problem)      | Serializable via exclusive locks       | Serializable — exclusive lock on write   |
| Serializability        | Snapshot Isolation only (not always serial) | Serializable                           | Serializable                             |
| Deadlock               | N/A                                         | Possible                               | Possible — needs detection               |
| Old version cleanup    | Needs vacuum/GC                             | N/A                                    | Needs vacuum/GC                          |

---

## Key Takeaways

1. **MVCC creates versions**, not locks for reads → readers never block writers
2. **Strict 2PL holds locks until commit/abort** → simpler than standard 2PL, avoids cascading aborts
3. **Waits-for graph + DFS** → efficient deadlock detection
4. **Abort must undo MVCC writes**: own inserts hidden (xmax=xid), own deletes reversed (xmax=0)
5. **Snapshot XIDs ensure consistency** → reader sees all commits before their snapshot, none after

---

## Files

- `main.cpp` — Complete implementation
- `Makefile` — Build configuration

Run `make run` to compile and execute all demo scenarios.
