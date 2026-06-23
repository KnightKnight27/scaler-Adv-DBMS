# Lab 8 — Transaction Manager: MVCC + Strict 2PL + Deadlock Detection
**Name:** Tirth Shah
**Roll Number:** 24BCS10347

## Problem statement
Build an in-memory transaction manager over a simple `key -> value` store
(`key = std::string`, `value = long long`) that combines three classic
mechanisms of a real transactional storage engine:

1. **MVCC version chains** — every write creates a new version (never an
   in-place overwrite); reads see a consistent *snapshot*.
2. **Strict Two-Phase Locking (Strict 2PL)** — writes take exclusive locks that
   are held until commit/abort.
3. **Deadlock detection** — a wait-for graph is maintained and cycles are broken
   by aborting a victim.

The demo is a single-threaded, explicitly interleaved **simulation** so every
scenario (including the deadlock) is fully deterministic and reproducible.

---

## Concurrency model chosen: **MV2PL** (multi-version 2PL)

> **Reads = lock-free MVCC snapshot reads. Writes = exclusive (X) locks under
> Strict 2PL.**

**Why this model.** This is how InnoDB / PostgreSQL-style engines actually
behave and is the recommended option in the brief. Readers never block writers
and writers never block readers, because a reader simply walks the version chain
and reads the version that was committed as of its snapshot. Only *writers*
contend, and they do so through X locks held to end-of-transaction, which gives
serializable-style write isolation and makes deadlock the only blocking hazard —
exactly what we want to demonstrate.

The lock manager still implements the **full S/X compatibility matrix and S→X
upgrade** so it is a general Strict-2PL lock manager; in this model the `read`
path simply does not request S locks. (A plain Strict-2PL-with-S-locks variant
would also be valid; we chose MV2PL and document it here explicitly.)

---

## MVCC version-chain design

Each key owns a singly-linked chain of `Version` nodes, **newest first**. A
version stores:

| field       | meaning                                                            |
|-------------|--------------------------------------------------------------------|
| `value`     | payload (ignored for a tombstone)                                  |
| `tombstone` | `true` if this version is a DELETE                                 |
| `creator`   | id of the txn that created the version                             |
| `begin_ts`  | commit timestamp of the creator (`INF` while uncommitted)          |
| `end_ts`    | timestamp at which a newer committed version superseded it (`INF` if still live) |
| `prev`      | pointer to the older version (the chain link)                      |

- **WRITE** prepends a new version linked to the prior head (`prev`). Its
  `begin_ts` is `INF` (pending) until the txn commits.
- **DELETE** prepends a tombstone version (same mechanism).
- **COMMIT** stamps each pending version with the txn's `commit_ts` and sets the
  superseded older version's `end_ts = commit_ts`.
- **ABORT** pops the txn's prepended head versions off their chains (it still
  holds the X lock on each, so its version is guaranteed to be the head).

A single monotonically increasing counter (`clock_`) issues both **begin-ts**
(snapshot, taken at `begin()`) and **commit-ts** (taken at `commit()`).

### Exact visibility rule (snapshot isolation)
Walk the chain newest-first. A version `V` is **visible** to transaction `t` iff:

```
(a) V.creator == t.id                              // t sees its own pending writes
 OR
(b) V.begin_ts != INF                              // V is committed
    AND V.begin_ts <= t.begin_ts                   // committed at/before t's snapshot
    AND V.end_ts   >  t.begin_ts                   // not yet superseded as of snapshot
```

The first version satisfying this is what `t` reads. If that version is a
tombstone (or no version qualifies), the key is **logically absent** for `t`.
Consequently: uncommitted versions are visible only to their own writer; a txn
whose snapshot predates another txn's commit does **not** see that txn's writes.

---

## Strict 2PL: lock compatibility matrix

Requested mode vs. mode held by **another** transaction:

|            | held **S** | held **X** |
|------------|:----------:|:----------:|
| request **S** |   OK    |   WAIT     |
| request **X** |  WAIT   |   WAIT     |

- **S/S** compatible; anything involving **X** is incompatible.
- **Upgrade S→X**: allowed iff no *other* txn holds any lock on the item (we drop
  our own S and take X).

**Why locks are held until the end.** "Strict" 2PL means a transaction releases
**none** of its locks before it commits or aborts — at that point *all* its locks
are released together (`LockManager::release_all`). Holding X locks to end-of-txn
prevents other transactions from reading/overwriting uncommitted data and avoids
cascading aborts, guaranteeing recoverability and a serializable write schedule.

---

## Wait-for graph + deadlock detection

When a write cannot get its X lock, the requester must **wait**. We add edges
`requester -> holder` for every txn currently holding an incompatible lock, then
run **DFS cycle detection** from the requester.

- **No cycle** → the request is reported as `Blocked` (a benign wait); in the
  deterministic sim the operation simply does not complete now and is retried
  after the blocker commits/aborts.
- **Cycle found** → **deadlock**. 

**Victim selection policy:** abort the transaction that *closed the cycle*
(the youngest / most-recently-blocked requester). This is simple, always breaks
the cycle, and is deterministic. On abort the victim's uncommitted versions are
rolled back, its locks are released, and it is removed from the wait-for graph,
which lets the survivor proceed.

---

## ASCII diagrams

### Version chain for key `"k"` (newest first)
After: `T1 write k=10 (commit-ts 5)`, then `T3 write k=20 (commit-ts 9)`:

```
 heads["k"]
     |
     v
 +------------------+      +------------------+
 | value   = 20     |      | value   = 10     |
 | begin_ts= 9      | prev | begin_ts= 5      | prev
 | end_ts  = INF    |----->| end_ts  = 9      |-----> (null)
 | creator = T3     |      | creator = T1     |
 +------------------+      +------------------+
   (live version)            (superseded at ts 9)

 A reader with snapshot ts=7 skips the begin_ts=9 node and reads value=10
 (5 <= 7 < 9). A reader with snapshot ts=10 reads value=20.
```

### Deadlock cycle (Scenario 3)
```
   T1 holds X(A), wants X(B)        T2 holds X(B), wants X(A)

        +-------- wait-for --------+
        |                          |
        v                          |
      [ T1 ] ---- wants B -------> [ T2 ]
        ^                          |
        |                          |
        +-------- wants A ---------+

   wait-for graph:  T1 -> T2  and  T2 -> T1   ==> CYCLE
   victim = T2 (closed the cycle) is aborted; T1 then proceeds.
```

---

## Files
- `version_store.h` — MVCC version chains (versions + per-key heads).
- `lock_manager.h` / `lock_manager.cpp` — Strict 2PL S/X locks, wait-for graph,
  DFS deadlock detection, victim selection.
- `txn_manager.h` / `txn_manager.cpp` — transaction API tying MVCC + 2PL
  together (`begin/read/write/remove/commit/abort`), snapshot visibility,
  rollback.
- `main.cpp` — deterministic scenarios + assert-based self-tests.
- `CMakeLists.txt` — build script (`project(lab8_txn_manager)`, C++17).

---

## Build & run

### Direct compiler (Apple clang as `c++`)
```sh
cd lab_8
c++ -std=c++17 -Wall -Wextra main.cpp lock_manager.cpp txn_manager.cpp -o lab8
./lab8
```
No threads are used, so `-pthread` is not required.

### CMake
```sh
cd lab_8
cmake -S . -B build
cmake --build build
./build/lab8
```

---

## Sample program output
```
========== Scenario 1: MVCC snapshot visibility ==========
T1=1 writes k=10
T2=2 reads k -> <absent> (expected <absent>)
T1 commits
T2 reads k -> <absent> (expected <absent>, snapshot predates commit)
T3=4 reads k -> 10 (expected 10)
PASS: MVCC visibility correct.

========== Scenario 2: Write-write conflict under Strict 2PL ==========
T1=1 write k=1 -> OK (acquires X on k)
T2=2 write k=2 -> BLOCKED (X held by T1 => must wait)
T1 commits (releases X)
T2 retries write k=2 -> OK
PASS: write-write conflict correctly serialized.

========== Scenario 3: Deadlock (T1:A->B, T2:B->A) ==========
T1=1 write A=100 -> OK (X on A)
T2=2 write B=200 -> OK (X on B)
T1 write B=101 -> BLOCKED (waits on T2; wait-for: T1->T2)
T2 write A=201 -> ABORTED (closes cycle => deadlock => T2 aborted)
T1 retries write B=101 -> OK
T3 reads A=100 B=101 (expected A=100, B=101)
PASS: deadlock detected, one victim aborted, data consistent.

========== Scenario 4: Abort rollback ==========
T2=3 write x=999 then ABORT
T3=4 reads x -> 7 (expected 7, the aborted write is invisible)
PASS: aborted write rolled back cleanly.

========== Scenario 5: Delete tombstone (MVCC) ==========
Old snapshot reads d -> 5 (expected 5)
New snapshot reads d -> <absent> (expected <absent>, tombstone)
PASS: tombstone visibility correct.

ALL TESTS PASSED
```
