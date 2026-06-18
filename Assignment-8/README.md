# Assignment 8 — Transaction Manager (MVCC + Strict 2PL)

**Tanishq Singh | 24BCS10303**

---

## What this is

An in-memory transaction manager written in C++17 in a single `main.cpp`.
It combines four classic concurrency-control ideas in one `TransactionManager` class:

1. **MVCC for reads** — each key owns a chain of versions tagged with
   creator / invalidator txn IDs. Readers walk the chain and pick the
   first version visible to their snapshot. Readers never block writers,
   writers never block readers.

2. **Strict 2PL for writes** — Shared / Exclusive row locks are acquired
   before every write and held until commit or abort. Supports S → X
   upgrade when the requesting transaction is the sole lock holder.

3. **Deadlock detection** — before registering a wait, a DFS is run over
   the waits-for graph. If a cycle is found, the youngest transaction on
   the cycle (highest ID) is killed immediately — no thread ever blocks
   indefinitely.

4. **Lost-update protection** — after grabbing the X lock, `write()` rescans
   the version chain and aborts if any other committed transaction wrote
   to the same key after our snapshot (first-updater-wins rule).

5. **`vacuum()`** — computes the oldest live snapshot (xmin) and prunes any
   version whose deletion was committed before that horizon — no active
   transaction can ever read it again.

---

## Files

```
Assignment-8/
├── main.cpp       ← engine + 7 demos that double as assertions
├── Makefile       ← make / make run / make test / make clean
├── run_tests.sh   ← build + run in one step
└── README.md      ← this file
```

---

## Build & Run

```bash
cd Assignment-8
bash run_tests.sh
```

Or manually:

```bash
g++ -std=c++17 -Wall -Wextra -O3 -o txn_manager main.cpp
./txn_manager
```

Builds clean — zero warnings under `-Wall -Wextra`.

---

## Demo Results

```
Results: 15/15 PASS
```

| # | Demo | Checks |
|---|------|--------|
| 1 | Snapshot Isolation | T2 sees committed write; T3 snapshot frozen before T4's commit |
| 2 | Shared Locks | S + S compatible — two readers don't conflict |
| 3 | Blocking / SI Snapshot Read | Uncommitted write invisible to concurrent reader |
| 4 | Lock Upgrade (S → X) | Sole holder can upgrade without deadlock |
| 5 | Deadlock Detection | Cycle in waits-for graph → youngest txn aborted |
| 6 | Lost Update Prevention | First-updater-wins aborts the second writer |
| 7 | Vacuum GC | 2 stale versions pruned; latest value still readable |

---

## Design Notes

### MVCC version chain

```
key "f" after three committed writes (t=2,3,4):
  [val=10 xmin=2 xmax=3] [val=20 xmin=3 xmax=4] [val=30 xmin=4 xmax=0]
```

A read with snapshot=3 walks newest-first:
- `val=30`: `xmin=4 > snapshot=3` → skip
- `val=20`: `xmin=3 <= 3` and `xmax=4 > 3` → **visible**, return 20

After `vacuum()` with no live snapshots, the two deleted versions are pruned.

### Waits-for graph & DFS deadlock detection

Every time a lock request conflicts, edges are added: `requester → each holder`.
A DFS from the requester checks for a back edge. If found, the cycle is
extracted and the node with the highest txn ID is aborted before any blocking
actually happens. This makes deadlock detection O(V + E) per acquire.

### First-updater-wins

After acquiring X, `write()` scans the version chain for any version where:
- `created_by != me`
- `created_by > my_snapshot`
- that creator's status is `COMMITTED`

If such a version exists, the current writer loses — it gets aborted and the
key retains the earlier committed value.

---

## Complexity

| Operation | Time |
|-----------|------|
| `read()` | O(chain length) — walk versions |
| `write()` | O(chain length) + O(V+E) deadlock check |
| `commit()` / `abort()` | O(locks held) |
| `vacuum()` | O(total versions) |
