# Lab 7 — Transaction Manager: MVCC + Two-Phase Locking + Deadlock Detection

**Author:** Rohan Singh Chauhan (24BCS10240)  
**Course:** Advanced Database Management Systems  
**Language:** C++17

A single-file C++ program that demonstrates how a database can combine **MVCC** (readers use snapshots), **Strict 2PL** (writers take exclusive locks until commit), and **deadlock detection** (waits-for graph cycle check).

---

## Files

| File | Purpose |
|------|---------|
| `txmgr.cpp` | Full implementation + four demo scenarios |
| `makefile` | Build and run helpers |

---

## Build & Run

```bash
make          # builds ./txmgr
make run      # build + run demos
make clean    # remove binary
```

Or manually:

```bash
g++ -std=c++17 -pthread -o txmgr txmgr.cpp
./txmgr
```

---

## How It Works (Viva Notes)

### 1. MVCC — Multi-Version Concurrency Control

Each row key stores a **chain of versions**. Every insert/update creates a new version at the front.

```
Version { value, created_by, removed_by }
         created_by = writer txn id  (like PostgreSQL xmin)
         removed_by = 0 if alive, else deleter txn id (xmax)
```

**Visibility rule** for transaction `T` with snapshot `read_view`:

- `created_by` must be committed and `< read_view`, OR it is `T` itself (own write).
- `removed_by` must be `0`, OR not yet committed at snapshot time.

Readers never block writers because they walk the version chain using their snapshot — no lock needed for the actual read from heap (the demo still takes a shared lock to show 2PL interaction).

### 2. Strict Two-Phase Locking (2PL)

Each transaction has two phases:

| Phase | Allowed | Not allowed |
|-------|---------|-------------|
| **Growing** | Acquire read/write locks | Release locks |
| **Shrinking** | Release locks | Acquire new locks |

This implementation uses **Strict 2PL**: all locks are held until `commit()` or `abort()`, then released together. That keeps schedules serializable for writes.

- **Read lock** — multiple transactions can read the same key.
- **Write lock** — exclusive; blocks other readers and writers.

### 3. Deadlock Detection

When a transaction waits for a lock, we record edges in a **waits-for graph**:

```
waiter → { holders blocking it }
```

Before sleeping, we run a cycle check (DFS). If a cycle exists, the requesting transaction is aborted immediately.

---

## Demo Scenarios

| # | Test | Expected behaviour |
|---|------|--------------------|
| 1 | MVCC snapshot isolation | `t2` started before `t3` committed → still reads `1000` |
| 2 | Concurrent read locks | Two shared locks on `balance` granted at once (value = `2000` after scenario 1) |
| 3 | Write blocks read | Reader waits until writer commits, then sees `3000` |
| 4 | Deadlock | Circular wait on keys `A` and `B` → one txn aborted |

---

## Code Layout (`txmgr.cpp`)

```
txn_table   → transaction ids, snapshots, commit/abort status
store       → MVCC version chains (insert / update / delete / undo)
locks       → strict 2PL queue + waits-for graph + cycle detection
TxManager   → public API wired for the demo
main()      → four scenarios
```

---

## MVCC vs 2PL — Why Both?

| | MVCC only | 2PL only | MVCC + Strict 2PL |
|---|-----------|----------|-------------------|
| Read/write blocking | None | Readers block writers | Reads use snapshots |
| Write conflicts | Lost updates possible | Serialized | Exclusive write locks |
| Deadlocks | Unlikely | Yes | Yes — needs detection |

PostgreSQL uses MVCC plus additional checks (SSI) for full serializability without heavy 2PL on reads.

---

## Key Takeaways

1. **Snapshot** = transaction sees all commits finished before its `read_view` id.
2. **Strict 2PL** = shrinking phase only at end → simpler and avoids cascading aborts.
3. **Abort rollback** = hide own inserts (`removed_by = self`) and restore own deletes (`removed_by = 0`).
4. Deadlock detection cost is O(V+E) per blocking wait — production systems often batch this check.
