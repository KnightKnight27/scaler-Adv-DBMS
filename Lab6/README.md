# Lab 6 — Transaction Manager: MVCC + Strict 2PL + Deadlock Detection

A working transaction manager that combines the two main concurrency
control mechanisms used in real databases:

1. **MVCC** (Multi-Version Concurrency Control) — every write creates a
   new row version. Readers see a consistent snapshot pinned at their
   transaction id, so readers never block writers.
2. **Strict 2PL** (Two-Phase Locking) — shared/exclusive row locks are
   acquired during the *growing* phase. All locks are released at once
   in a single *shrinking* event at commit/abort. Prevents cascading
   aborts.
3. **Deadlock detection** — a waits-for graph + DFS cycle check on
   every conflicting lock acquisition. On a cycle, the requesting
   transaction throws and is aborted (younger-victim policy).

Mirrors the architecture of PostgreSQL's concurrency layer. Follows
`lab_sessions/lab_6.txt` from sir's repo.

## Build & run

```bash
cd Lab6
cmake -S . -B build
cmake --build build
./build/txmgr
```

Requires CMake ≥ 3.10, C++17, and a pthread-capable compiler.

## What the demo shows

`main()` runs four scenarios:

1. **Snapshot Isolation** — `T2` begins before `T3` writes; after `T3`
   commits, `T2`'s read still sees the *old* value. That's MVCC in
   action.
2. **Concurrent shared locks** — two readers grab `SHARED` on the same
   row simultaneously and both succeed (no R/R conflict).
3. **Exclusive lock + waiting** — a writer holds `EXCLUSIVE`; a reader
   on another thread blocks until the writer commits, then sees the
   new value.
4. **Deadlock detection** — `T8` holds `A` and wants `B`; `T9` holds
   `B` and wants `A`. The waits-for graph has a cycle; one of them
   aborts.

## Architecture

```
TransactionManager
   ├── LockManager (Strict 2PL)
   │     growing phase  -> acquire SHARED / EXCLUSIVE on row key
   │     shrinking phase-> single release event at commit/abort
   │     deadlock check -> DFS on g_waits_for (O(V+E) per acquire)
   │
   └── MVCC Heap (per-row version chain)
         INSERT -> push_front {value, xmin=xid, xmax=0}
         UPDATE -> stamp old visible xmax=xid; push_front new version
         DELETE -> stamp old visible xmax=xid
         READ   -> walk chain; return first version where
                   (xmin committed AND xmin < snapshot)
                   AND (xmax == 0 OR xmax > snapshot OR xmax aborted)
```

## MVCC visibility rule

A row version with creator `xmin` and deleter `xmax` is visible to
transaction `T` (snapshot `S`) iff:

- `xmin == T`  *(own write)*, **or** `xmin` committed AND `xmin < S`
- AND not invalidated:
  - `xmax == 0`  *(still live)*, **or**
  - `xmax > S`   *(deleter not in snapshot)*, **or**
  - `xmax` aborted

## MVCC vs 2PL — why both?

| Property               | MVCC alone                  | 2PL alone        | MVCC + Strict 2PL |
|------------------------|-----------------------------|------------------|--------------------|
| Reader blocks writer?  | No                          | Yes              | No                 |
| W/W contention         | Last write wins (lost write)| Serializable     | Serializable       |
| Serializability        | Snapshot Isolation only     | Yes              | Yes                |
| Deadlock possible      | No                          | Yes              | Yes — needs detection |
| Old versions cleanup   | Needs vacuum/GC             | N/A              | Needs vacuum/GC    |

Combining MVCC for reads with 2PL for writes is exactly what PostgreSQL
does (with extra refinements like Serializable Snapshot Isolation).

## File layout

| File             | Purpose |
|------------------|---------|
| `main.cpp`       | Tx state + MVCC heap + lock manager + 4 demo scenarios |
| `CMakeLists.txt` | C++17 + pthread |
| `.gitignore`     | excludes `build/` and the binary |
