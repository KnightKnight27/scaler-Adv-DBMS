# Lab 6 — Transaction Manager: MVCC + Two-Phase Locking

A small transaction manager that puts three classic ideas together, the
same way PostgreSQL does:

1. **MVCC** — multi-version concurrency control, so readers never block
   writers and writers never block readers.
2. **Strict Two-Phase Locking (2PL)** — for write/write correctness:
   acquire locks while running, release them all at commit/abort.
3. **Deadlock detection** — a waits-for graph; if a lock request would
   close a cycle, that transaction is aborted.

## Layout

```
Lab-6/
├── transaction_manager.h    # public API + DeadlockException
├── transaction_manager.cc   # MVCC heap, lock manager, deadlock check
├── main.cc                  # four demo scenarios
├── Makefile                 # g++ -std=c++17 -pthread
└── README.md
```

## Build & run

```bash
cd Lab-6
make          # builds ./txnmgr
make run      # builds and runs the demo
make clean
```

Without `make`:

```bash
g++ -std=c++17 -Wall -Wextra -O2 -pthread \
    main.cc transaction_manager.cc -o txnmgr
./txnmgr
```

(`-pthread` is required — the lock-blocking and deadlock scenarios use
real threads.)

## 1. MVCC — versions and visibility

Every row is a **chain of versions**, newest first. Each version carries
two transaction-id stamps:

| Stamp | Meaning |
| ----- | ------- |
| `xmin` | the transaction that **created** this version |
| `xmax` | the transaction that **deleted/replaced** it (`0` = still live) |

- **insert** → push a new version `{value, xmin=me, xmax=0}`.
- **update** → stamp the current visible version's `xmax = me`, then push
  a new version. (Old readers still see the old one.)
- **delete** → stamp the visible version's `xmax = me`.

When a transaction **begins**, it takes a **snapshot** (here, simply its
own id — it can see every transaction that committed with a smaller id).
A version is visible to a reader if:

```
created-visible:  xmin == me  OR  (xmin committed AND xmin < snapshot)
AND
not-deleted-yet:  xmax == 0
                  OR NOT (xmax == me OR (xmax committed AND xmax < snapshot))
```

That single rule is what gives **snapshot isolation**: a long-running
read keeps seeing the same data even while others commit changes.

```
   row "balance"          version chain (newest -> oldest)
   ┌─────────────┐   ┌─────────────┐
   │ value=2000  │   │ value=1000  │
   │ xmin=3      │ → │ xmin=1      │
   │ xmax=0      │   │ xmax=3      │
   └─────────────┘   └─────────────┘
   A reader whose snapshot < 3 skips the first version (xmin=3 too new)
   and returns 1000.
```

## 2. Strict 2PL — locks for write correctness

MVCC alone allows the *lost update* problem (two writers overwrite each
other). So writes also take **locks**:

- `read`  → **shared** lock (many readers can share).
- `insert / update / delete` → **exclusive** lock (one writer only).

Locks follow **strict two-phase locking**:

```
GROWING phase    : acquire locks freely (while the transaction runs)
SHRINKING phase  : release locks — happens all at once at commit/abort
```

Once a transaction starts releasing (shrinking), it may not acquire a new
lock — the code enforces this and throws if violated. Holding all locks
until commit is what makes the schedule serializable and avoids cascading
aborts.

## 3. Deadlock detection — waits-for graph

When a lock can't be granted, the waiter records an edge
`waiter → holder(s)` in a **waits-for graph**. Before going to sleep, it
runs a DFS from itself; if that reaches itself again, there's a **cycle**
(a deadlock), and the requester throws `DeadlockException` so the caller
aborts it — breaking the cycle.

```
tx10 holds A, wants B          waits-for:  10 → 11
tx11 holds B, wants A                      11 → 10   ← cycle! abort one
```

## Demo scenarios (`main.cc`)

1. **MVCC snapshot isolation** — `t2` reads `balance` before `t3` commits
   a new value; `t2` still sees the old `1000`, while a later `t4` sees
   the new `2000`.
2. **Concurrent shared locks** — two readers both hold a shared lock on
   the same row at the same time (no conflict).
3. **Exclusive lock blocks a reader** — a writer holds an exclusive lock;
   a reader on another thread waits until the writer commits, then reads
   the new value.
4. **Deadlock detection** — `t1` locks A then wants B, `t2` locks B then
   wants A; the manager detects the cycle and aborts one transaction so
   the other can finish.

### Sample output

```
=== Scenario 1: MVCC snapshot isolation ===
  [tx 1] COMMIT
  [tx 3] COMMIT
  [tx 2] read balance = 1000      <- old snapshot
  [tx 2] COMMIT
  [tx 4] read balance = 2000      <- new snapshot
  [tx 4] COMMIT
...
=== Scenario 4: deadlock detection ===
  [tx 9] COMMIT
  deadlock detected, abort tx 11
  [tx 11] ABORT
  [tx 10] COMMIT
```

(The exact transaction ids and which side aborts in scenario 4 can vary
between runs because of thread timing — what's guaranteed is that exactly
one of the two deadlocked transactions is aborted and the other commits.)

## Why both MVCC *and* 2PL?

| Problem | MVCC alone | 2PL alone | MVCC + strict 2PL |
| ------- | ---------- | --------- | ----------------- |
| reader blocks writer | no | yes (shared lock) | no (snapshot read) |
| lost update (w/w)    | possible | prevented | prevented (X-lock) |
| deadlock             | n/a | possible | possible → detected |

MVCC handles read/write concurrency cheaply; 2PL handles write/write
correctness. PostgreSQL uses essentially this combination (with a
refinement called Serializable Snapshot Isolation for the strictest
level).

## Key takeaways

- A write doesn't overwrite data in place — it adds a new version and
  stamps the old one's `xmax`. Old readers keep working off their
  snapshot.
- The visibility rule is the whole story of snapshot isolation: "was this
  version created-and-committed before me, and not yet
  deleted-and-committed before me?"
- Strict 2PL keeps writes serializable; releasing all locks at the end
  avoids cascading aborts.
- Deadlocks are unavoidable with locking, so you detect them (cycle in
  the waits-for graph) and abort a victim rather than trying to prevent
  every one up front.
- On **abort**, MVCC writes must be undone: hide this transaction's own
  inserts (`xmax = self`) and revive the versions it had deleted
  (`xmax = 0`).
