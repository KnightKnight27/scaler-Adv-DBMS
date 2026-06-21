# Lab 8 — In-Memory Transaction Manager (MVCC + Strict 2PL)

> **Course:** Advanced DBMS
> **Author:** Mahir Abidi
> **Roll No:** 24BCS10125
> **Language:** C++17 (threads, mutexes, condition variables)

A single-file in-memory transaction manager that combines three classic concurrency-control techniques:

1. **MVCC for reads.** Each write appends a new version to a per-key chain instead of overwriting. A reader walks the chain and picks the newest version visible to its own snapshot — so readers never block writers.
2. **Strict Two-Phase Locking for writes.** Writers acquire an exclusive (X) lock; readers acquire a shared (S) lock. All locks are held until commit or abort, enforcing strict 2PL.
3. **Deadlock detection** via DFS on a waits-for graph. When a cycle is found, the youngest (highest-id) transaction is aborted — sacrificing the least work done.

A `vacuum()` pass garbage-collects version chains once no active transaction can see the dead versions.

---

## Files

| File           | Purpose                                                              |
| -------------- | -------------------------------------------------------------------- |
| `main.cpp`     | `TxManager` class + seven demo/test scenarios (593 lines).           |
| `Makefile`     | Build targets: `make build`, `make run`, `make test`, `make clean`.  |
| `run_tests.sh` | One-command harness: clean → build → assert → verbose demo.          |
| `README.md`    | This document.                                                       |

---

## Build & Run

```bash
bash run_tests.sh       # clean build + assertions + verbose demo
```

Or via Makefile:

```bash
make build   # compiles with -O3 -Wall -Wextra -pthread
make run     # verbose demo: prints all 7 scenarios
make test    # quiet assertion suite: exits 0 if 7/7 pass
make clean   # removes the binary
```

Or by hand:

```bash
g++ -std=c++17 -Wall -Wextra -pthread main.cpp -o txmgr
./txmgr          # verbose demo
./txmgr --test   # assertion suite
```

---

## Public API

```cpp
TxnId              start();
optional<string>   fetch(tx, key);
void               store_(tx, key, value);
void               erase_(tx, key);
void               commitTxn(tx);
void               abortTxn(tx);
size_t             vacuum();          // prunes dead versions, returns count
size_t             chainLength(key);  // length of a per-key version chain
```

Failures throw `TxnFailure` (a `runtime_error` subtype) — deadlock victims, serialization conflicts, and 2PL violations all surface this way.

---

## How Visibility Works

Every `RowVersion` records its `creator` (the tx that wrote it) and `invalidator` (the tx that superseded or deleted it; `0` = still live). Each transaction stores a `snap` — the value of the global commit counter at `start()` time.

A version is visible to reader `R` when:

```
( creator == R  OR  creator finished with commitStamp <= R.snap )
AND
( invalidator == 0
  OR NOT (invalidator == R  OR  invalidator finished with commitStamp <= R.snap) )
```

Snapshotting on the commit counter (not the tx id) correctly handles a transaction with a lower id that committed *after* the reader started.

---

## Lost Update Prevention

Strict 2PL serializes writers, but under snapshot isolation the second writer could still clobber the first if it read stale data before acquiring the lock. To block this, `store_()` and `erase_()` re-scan the version chain *after* taking the X lock. If any version was committed by another tx whose `commitStamp > reader.snap`, the operation throws:

> `could not serialize access: row touched by tx N`

This is the **first-updater-wins** rule (the same semantics Postgres uses). Demo 6 exercises it.

---

## Deadlock Detection

Before blocking, `lockRow()` records outgoing edges in `waitGraph` and runs a DFS from the requester. A cycle means deadlock; the highest-id (youngest) transaction in the cycle is chosen as the victim.

If the requester is the victim it throws immediately. Otherwise the victim is marked `Killed`, its locks are dropped, and `notify_all` wakes every waiter; the victim's thread sees the `Killed` state at the top of the wait loop and throws.

---

## Vacuum (Garbage Collection)

`vacuum()` computes the oldest snapshot still held by any running transaction. Any version whose invalidator committed *before* that horizon is invisible to all current and future readers and can be safely removed.

Demo 7 builds a 5-version chain, calls `vacuum()`, and confirms it shrinks to 1 (the single live version) while pruning 8 dead versions across all keys.

---

## The 7 Scenarios

| # | What it demonstrates |
|---|---|
| 1 | Reader started before a writer's commit keeps the old value (snapshot isolation) |
| 2 | Two concurrent readers both hold S locks without blocking each other |
| 3 | A reader blocked on a writer's X lock unblocks at commit but reads from its own snapshot |
| 4 | A sole S-lock holder upgrades to X in-place without re-queuing |
| 5 | Deadlock: T1 holds X waits for Y, T2 holds Y waits for X — younger aborts |
| 6 | Two writers on the same row — first wins, second gets a serialization error |
| 7 | `vacuum()` prunes dead version chains below the oldest active snapshot |

---

## Expected Output

```
=== 1. snapshot isolation: reader sees pre-write value ===
  reader (tx 2) sees: 1000
=== 2. two readers hold shared locks at the same time ===
  tx 4 read: 2000
  tx 5 read: 2000
=== 3. exclusive lock blocks a reader, but reader stays at SI snapshot ===
  reader (tx 7) waiting for shared lock...
  reader (tx 7) got: 2000
=== 4. lock upgrade S -> X by sole holder ===
  tx 8 read under S lock: 3000
  tx 8 upgraded to X lock and wrote 4000
=== 5. deadlock detection (younger tx aborts) ===
  tx 11 aborted: deadlock: victim 11
  tx 10 committed
=== 6. SI rejects a lost update (first-updater-wins) ===
  tx 13 committed counter=1
  tx 14 aborted: could not serialize access: row touched by tx 13
=== 7. vacuum prunes dead versions ===
  vkey chain length before vacuum: 5
  vacuum pruned 8 dead versions (across all keys)
  vkey chain length after vacuum:  1
```

---

## Design Notes

- **No `<bits/stdc++.h>`** — only portable standard headers.
- **RAII locking** — every shared-state access uses `lock_guard` or `unique_lock`; no bare locks.
- **Strong typedefs** — `TxnId`, `Stamp`, `RowKey` make signatures self-documenting.
- **Single-process, in-memory only** — no WAL, no crash recovery, no predicate locks.
- **Abort is lazy** — aborted versions are filtered by visibility and reclaimed by `vacuum()`.
