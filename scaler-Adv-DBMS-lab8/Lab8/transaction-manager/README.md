# Lab 8 — Transaction Manager (MVCC + Strict 2PL)

**24BCS10404 — Rajveer Bishnoi**

A compact in-memory transaction manager in C++17 combining:

1. **MVCC for reads** — writes append a new version; readers walk the per-key chain for their snapshot
2. **Strict 2PL for writes** — exclusive lock on write, shared on read, held until commit/abort
3. **Deadlock detection** — DFS over a waits-for graph; youngest transaction in the cycle is the victim

---

## Build & run

```bash
g++ -std=c++17 -Wall -Wextra -pthread main.cpp -o txmgr && ./txmgr
```

---

## Public API (`TxnEngine`)

| Method | Description |
|--------|-------------|
| `start()` | Begin a new transaction, returns `TxnId` |
| `fetch(tx, key)` | Read a key under snapshot isolation (acquires S lock) |
| `store_(tx, key, value)` | Write a key (acquires X lock, appends new version) |
| `erase_(tx, key)` | Delete a key (acquires X lock, appends deleted version) |
| `commitTxn(tx)` | Commit and release all locks |
| `abortTxn(tx)` | Abort and release all locks |
| `gc()` | Prune versions older than the oldest active snapshot |
| `versionCount(key)` | Number of versions in the chain for a key |

Failures surface as `TxnFailure` exceptions.

---

## How visibility works

Each `RowVersion` records `creator` and `invalidator` (0 = still live). A transaction's `snap` is the global commit counter at `start()` time.

A version is **visible** to reader `R` when:
- Creator finished AND `commitStamp ≤ R.snap` (or creator == R)
- AND invalidator is either 0, or committed **after** R's snapshot

---

## Catching lost updates (first-updater-wins)

After taking the X lock, `store_()` re-scans the chain. If any version was committed by another transaction whose `commitStamp > my snap`, it throws `could not serialize access` — same wording as PostgreSQL. The caller retries. Demo 6 shows this.

---

## Deadlock detection

`lockRow()` records waits-for edges in `waitGraph` before blocking, then runs DFS from the requester. A cycle means deadlock — the **highest-id (youngest)** transaction in the cycle is killed, since older transactions have done more work.

---

## Garbage collection (`gc`)

Finds the oldest snapshot held by any running transaction (the horizon). Drops every version whose invalidator committed before the horizon — those versions can never be seen again. Demo 7 shrinks a chain of 5 down to 1.

---

## Demos in `main()`

| # | What it shows |
|---|---------------|
| 1 | Reader started before a writer committed keeps its old snapshot |
| 2 | Two readers take shared locks simultaneously without blocking |
| 3 | Reader blocked on a writer's X lock; unblocks at commit but stays at SI snapshot |
| 4 | Sole holder upgrades S lock to X without re-queuing |
| 5 | Deadlock: T1 holds X waits for Y, T2 holds Y waits for X — younger aborts |
| 6 | Two writers update same row; first wins, second told to retry |
| 7 | `gc()` prunes dead versions older than the oldest active snapshot |

---

## Files

- `main.cpp` — complete transaction manager + 7 demos
