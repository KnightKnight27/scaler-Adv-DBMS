# Lab 8 — In-memory Transaction Manager

**Name:** Shaurya Verma
**Roll No.:** 24BCS10151

## Goal

Build a compact transaction engine in C++17 that combines the
concurrency-control techniques a real DBMS uses, in one
`TransactionManager` class, and prove each one with a focused demo:

1. **MVCC reads** — multi-version storage so a transaction reads the
   database *as of its own snapshot*, regardless of what later commits.
2. **Strict 2PL** — Shared/eXclusive row locks taken before each
   operation and held until commit/abort (with S→X upgrade).
3. **Deadlock detection** — DFS over the waits-for graph; the youngest
   transaction on a cycle is aborted.
4. **Lost-update protection** — first-updater-wins on concurrent writes.
5. **`vacuum()`** — pruning versions no live snapshot can still see.

## Layout

```
Lab_8_shaurya_24bcs10151/
├── README.md      ← this file
└── main.cpp       ← TransactionManager + 7 demo scenarios
```

## Build & run

```bash
cd Lab_8_shaurya_24bcs10151
g++ -std=c++17 -Wall -Wextra -Wpedantic main.cpp -o txmgr
./txmgr
```

Compiles clean (no warnings). The demo is **single-threaded and
deterministic**: a lock conflict is reported as an explicit `BLOCKED`
status and then resolved by the script, instead of a real OS-level
thread wait. That keeps the output reproducible while still exercising
every path — blocking, upgrade, deadlock, abort, and vacuum.

## How it works

### Versioned storage (MVCC)

Each key owns a **chain of versions**. A version records the value, the
`creator` transaction id, and the `invalidator` id of the transaction
that later superseded it (`0` while still current):

```cpp
struct Version { int value; int creator; int invalidator; };
```

When a transaction begins it copies the set of already-committed txn ids
into its **snapshot**. A version is *visible* to transaction `T` when:

```
creator is visible      :=  creator == T   OR   creator ∈ T.snapshot
invalidator is visible  :=  invalidator != 0 AND
                            (invalidator == T OR invalidator ∈ T.snapshot)

version is visible to T  :=  creator is visible  AND  NOT invalidator is visible
```

A read walks the chain newest-first and returns the first visible
version — so a transaction never sees writes that committed after it
started.

### Locking (strict 2PL)

| Held \ Requested | S      | X      |
|------------------|--------|--------|
| **S**            | ✅ grant | ⛔ wait (or upgrade if sole holder) |
| **X**            | ⛔ wait | ⛔ wait |

`read` takes an **S** lock, `write` takes an **X** lock, and all locks
are held until commit/abort. If a transaction already holds **S** alone
and asks for **X**, it upgrades in place. MVCC governs *what a reader
sees*; the lock table governs *when conflicting writers may proceed*.

### Deadlock detection

Every time an acquire would block, the manager adds waits-for edges
`requester → {current holders}` and runs a DFS looking for a cycle that
returns to the requester. If one exists, the **youngest** transaction
(highest id) on the cycle is aborted, its locks and version edits are
rolled back, and the survivor proceeds.

### Lost-update protection

After taking the X lock, `write` re-scans the chain. If any version was
created by a transaction that **committed after this one's snapshot**
(i.e. a concurrent writer), the current transaction aborts —
*first-updater-wins*.

### `vacuum()`

A version is dead once its `invalidator` has committed and sits below
the **oldest live snapshot horizon** (the smallest id among still-active
transactions, à la Postgres `oldest_xmin`). Those versions can never be
visible again and are removed.

## The seven scenarios

| # | Scenario               | What it proves |
|---|------------------------|----------------|
| 1 | Snapshot isolation     | A reader keeps seeing its snapshot value even after another txn commits a newer one. |
| 2 | Shared locks           | Two readers hold S on the same key simultaneously. |
| 3 | Blocked writer         | A writer’s X request blocks behind a reader’s S lock, then proceeds once the reader commits. |
| 4 | Lock upgrade           | A sole S holder upgrades to X in place. |
| 5 | Deadlock               | A waits-for cycle is detected and the youngest txn aborted. |
| 6 | Lost update            | A concurrent second writer aborts (first-updater-wins). |
| 7 | Vacuum                 | Dead versions past the snapshot horizon are pruned. |

## Captured output

```text
Lab 8 - in-memory transaction manager (Shaurya Verma, 24BCS10151)
  T1 BEGIN  (snapshot = {})
  T1 write x <- 100 -> OK
  T1 write y <- 200 -> OK
  T1 write z <- 300 -> OK
  T1 COMMIT

=== 1. Snapshot isolation (MVCC: reader keeps its snapshot) ===
  T2 BEGIN  (snapshot = {T1})
  T3 BEGIN  (snapshot = {T1})
  T3 write x <- 111 -> OK
  T3 COMMIT
  T2 read x = 100
  T2 COMMIT

=== 2. Shared locks (two readers share an S lock) ===
  T4 BEGIN  (snapshot = {T1, T2, T3})
  T5 BEGIN  (snapshot = {T1, T2, T3})
  T4 read y = 200
  T5 read y = 200
  T4 COMMIT
  T5 COMMIT

=== 3. Writer blocked by a reader's S lock, then unblocked ===
  T6 BEGIN  (snapshot = {T1, T2, T3, T4, T5})
  T7 BEGIN  (snapshot = {T1, T2, T3, T4, T5})
  T6 read z = 300
  T7 write z <- 333 -> BLOCKED
  T6 COMMIT
  T7 write z <- 333 -> OK
  T7 COMMIT

=== 4. Lock upgrade S -> X (sole holder) ===
  T8 BEGIN  (snapshot = {T1, T2, T3, T4, T5, T6, T7})
  T8 read x = 111
  T8 upgrade S->X on x
  T8 write x <- 999 -> OK
  T8 COMMIT

=== 5. Deadlock detection (youngest aborted) ===
  T9 BEGIN  (snapshot = {T1, T2, T3, T4, T5, T6, T7, T8})
  T10 BEGIN  (snapshot = {T1, T2, T3, T4, T5, T6, T7, T8})
  T9 write x <- 1 -> OK
  T10 write y <- 2 -> OK
  T9 write y <- 1 -> BLOCKED
  deadlock: cycle {T10 -> T9} -> abort youngest T10
  T10 ABORT  (deadlock victim)
  T10 write x <- 2 -> ABORTED (deadlock)
  T9 write y <- 1 -> OK
  T9 COMMIT

=== 6. Lost update (first-updater-wins) ===
  T11 BEGIN  (snapshot = {T1, T2, T3, T4, T5, T6, T7, T8, T9})
  T12 BEGIN  (snapshot = {T1, T2, T3, T4, T5, T6, T7, T8, T9})
  T11 write y <- 10 -> OK
  T11 COMMIT
  T12 ABORT  (lost update)
  T12 write y <- 20 -> ABORTED (lost update)

=== 7. Vacuum (prune dead versions) ===
  VACUUM (horizon = T13) removed 6 dead version(s)

Done.
```

## What I took away

- **Snapshots make reads decision-free.** Once a transaction fixes the
  set of txns it can "see", every read is just a chain walk with a fixed
  visibility predicate — no coordination with writers needed.
- **Locks and MVCC answer different questions.** MVCC decides *what a
  reader sees*; 2PL decides *when conflicting writers run*. Conflating
  them is where most of the subtlety lives.
- **Deadlock handling is a graph problem.** The waits-for graph plus a
  cycle search is the whole mechanism; picking the youngest victim keeps
  older (more-invested) transactions alive.
- **Garbage is unavoidable in MVCC.** Every update leaves a dead
  version behind, so a vacuum/horizon step isn’t optional — it’s what
  keeps the version chains from growing without bound.
