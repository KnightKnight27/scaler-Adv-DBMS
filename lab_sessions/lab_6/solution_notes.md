# Lab 6 — Transaction Manager: MVCC + Strict 2PL + Deadlock Detection

## Concept

A database must let multiple transactions run concurrently without corrupting data. Two complementary mechanisms handle this:

- **MVCC (Multi-Version Concurrency Control)** — instead of locking rows on read, every write creates a new version. Readers see a consistent snapshot taken at transaction start. Readers never block writers and writers never block readers.
- **Strict 2PL (Two-Phase Locking)** — writers acquire exclusive locks before modifying rows and hold them until commit/abort. This serializes concurrent writes and prevents lost updates.
- **Deadlock detection** — when two transactions each hold a lock the other needs, neither can proceed. We detect this by building a waits-for graph and running DFS to find cycles.

Together these form the core of how PostgreSQL handles concurrency.

## Approach

### MVCC version chain
Each row key maps to a linked list of `RowVersion` structs: `{data, xmin (creator), xmax (invalidator), deleted}`.

A version is **visible** to transaction T if:
- `xmin` is T itself (own write) OR `xmin` committed before T's snapshot
- `xmax` is 0 (still live) OR `xmax` committed after T's snapshot OR `xmax` is aborted

On write: stamp the old visible version's `xmax = tx`, push new version to front.
On abort: undo own writes by marking `xmax = xid` on own inserts (hides them).

### Strict 2PL lock manager
Lock table maps each row key to a queue of `LockRequest {xid, mode, granted}`.
- **Shared (read) locks** are compatible with each other.
- **Exclusive (write) locks** conflict with everything.
- A transaction in the **shrinking phase** (post-commit/abort) cannot acquire new locks.

### Deadlock detection
Every time a transaction blocks, it adds edges `tx → {blocking holders}` to the waits-for graph. DFS from the blocked transaction checks if it can reach itself through the graph. If yes → cycle → throw `TxnFailure` → caller aborts the transaction.

## Solution

`txmgr.cpp` implements `TxManager` with: `start()`, `fetch()`, `put()`, `erase()`, `commit()`, `abort()`.

### 4 scenarios verified:

**Scenario 1 — Snapshot isolation:**
T2 starts before T3 commits. T3 writes `balance=2000`. T2 still reads `balance=1000` because T3's commitStamp is beyond T2's snapshot.

**Scenario 2 — Concurrent shared locks:**
T4 and T5 both get shared locks on `balance` simultaneously — no conflict.

**Scenario 3 — Exclusive lock blocks reader:**
T6 holds exclusive lock on `balance`. T7 (reader thread) blocks. T6 commits → lock released → T7 unblocks and reads T6's value.

**Scenario 4 — Deadlock:**
T8 holds lock on A, T9 holds lock on B. T8 tries to lock B, T9 tries to lock A → cycle in waits-for graph → younger transaction aborted.

## Key Takeaway

MVCC eliminates read-write contention (readers don't block writers). Strict 2PL serializes write-write conflicts. The combination gives snapshot isolation with serializability for writes. Deadlock detection via DFS is O(V+E) — PostgreSQL runs a similar check on a background timer rather than per lock request.
