# Lab Session 6 — Solution: Transaction Manager (MVCC + Strict 2PL + Deadlock Detection) in C++

My completed solution to **`lab_sessions/lab_6.txt`** — a multithreaded
transaction manager combining **MVCC** (snapshot-isolation reads), **Strict
Two-Phase Locking** (write serialization), and **deadlock detection** via a
waits-for graph. Compiled, run, and analysed against the four demo scenarios.

## Files

| File | Purpose |
|------|---------|
| `txmgr.cpp` | The full transaction manager + 4 demo scenarios |
| `run_output.txt` | Captured output of `./txmgr` |
| `.gitignore` | Excludes the compiled binary |

```bash
g++ -std=c++17 -pthread -O2 -o txmgr txmgr.cpp
./txmgr
```

> **Build fix:** the handout calls `std::function` inside `has_cycle()` but its
> include list omitted `<functional>`; I added it (and `<chrono>`) so it
> compiles cleanly.

---

## The three mechanisms and why all three are needed

| Concern | Mechanism here | What it buys |
|---------|----------------|--------------|
| Readers vs writers | **MVCC** version chains | readers never block writers — they read a consistent snapshot |
| Writers vs writers | **Strict 2PL** (row locks) | serializes conflicting writes; no lost updates |
| Locks → cycles | **Waits-for graph + DFS** | detects deadlock and aborts a victim instead of hanging forever |

MVCC alone gives only *snapshot isolation* (not full serializability) and cannot
order conflicting writes; 2PL alone makes readers block writers. Combining them —
exactly what PostgreSQL does — removes read/write contention **and** serializes
writes.

```
TransactionManager.begin/read/update/commit/abort
        │
        ├─► Lock Manager (Strict 2PL)
        │       growing:   acquire SHARED / EXCLUSIVE
        │       shrinking: release ALL locks at commit/abort (instantaneous)
        │       deadlock:  waits-for graph, DFS cycle check on each wait
        │
        └─► MVCC Heap (per-row version chain, newest first)
                INSERT → push {value, xmin=xid, xmax=0}
                UPDATE → stamp old version xmax=xid, push new version
                DELETE → stamp visible version xmax=xid
                READ   → first version with xmin committed < snapshot
                         AND (xmax==0 OR xmax not committed-before-snapshot)
```

---

## Real run (captured)

> Transaction IDs are assigned from one global counter, so the actual `xid`s run
> 1…11 across all scenarios — the scenario comments (t1…t9) are just variable
> names. e.g. Scenario 4's "t8/t9" are really xids **10/11**.

```
=== Scenario 1: MVCC Snapshot Isolation ===
[TX 1] COMMITTED
[TX 3] COMMITTED
  [TX 2] READ balance = 1000          ← sees pre-update snapshot
[TX 2] COMMITTED

=== Scenario 2: Concurrent Shared Locks ===
  [TX 4] READ balance = 2000
  [TX 5] READ balance = 2000
[TX 4] COMMITTED
[TX 5] COMMITTED

=== Scenario 3: Exclusive Lock + Waiting ===
  [TX 7] waiting for shared lock on balance...
[TX 6] COMMITTED
  [TX 7] READ balance = 3000          ← reads only after the X-lock is released
[TX 7] COMMITTED

=== Scenario 4: Deadlock Detection ===
[TX 8] COMMITTED
[TX 9] COMMITTED
  Deadlock detected, aborting tx 11
[TX 11] ABORTED
[TX 10] COMMITTED

All scenarios complete.
```

### Scenario-by-scenario verification

**1 — Snapshot isolation.** `t1` inserts `balance=1000` and commits. `t2` begins
(snapshot = its xid). `t3` then updates to `2000` and commits *after* `t2`
started. `t2` reads **1000**: the old version's `xmin=1 < snapshot` and its
`xmax=3` is **not** `< t2.snapshot`, so the old version stays visible while the
new `xmin=3` version is invisible to `t2`. This is snapshot isolation working —
a writer (`t3`) did not block or change a concurrent reader (`t2`). ✔

**2 — Concurrent shared locks.** `t4` and `t5` both take a SHARED lock on
`balance` and both proceed — shared/shared does not conflict.
**Observation / handout correction:** the values read are **2000**, the value
`t3` committed in Scenario 1 — *not* `3000` as the handout's "expected output"
listed. `3000` is only written later in Scenario 3, so the handout's expected
output was simply wrong here. My run reflects the correct state. ✔

**3 — Exclusive blocks shared.** `t6` holds an EXCLUSIVE lock (via `update` to
`3000`). A second thread's `t7` requests a SHARED lock, **blocks** on the
condition variable ("waiting for shared lock…"), and only proceeds once `t6`
commits and releases — then it reads the new committed value `3000`. This is the
2PL conflict matrix (X blocks S) plus the commit-time lock release. ✔

**4 — Deadlock.** `t10` locks `A`, `t11` locks `B`; then `t10` requests `B` and
`t11` requests `A` → waits-for cycle `10→11→10`. The DFS in `has_cycle()` detects
it and throws `DeadlockException`, aborting the victim (**t11**, the younger
transaction) while **t10** proceeds to commit. The victim was consistent across
repeated runs. ✔

---

## Two-Phase Locking — the phase boundary

```
GROWING phase                │  SHRINKING phase
acquire locks (S / X)   ✓    │  release locks      ✓
                             │  acquire a new lock  ✗  (2PL violation)
```

**Strict 2PL** (used here) collapses the shrinking phase to a single instant —
**commit/abort** — so all locks are held to the very end. My `acquire_lock`
enforces this: once `release_locks` sets `in_shrinking = true`, any further
`acquire_lock` throws. Holding locks until commit is what prevents *cascading
aborts* (no one reads a value this transaction might still roll back).

---

## MVCC details that the code makes concrete

- **A write never overwrites in place.** `UPDATE` stamps the old version's
  `xmax` and pushes a new version — the old bytes survive for any snapshot that
  still needs them. This is why PostgreSQL needs `VACUUM` (Topic 2): dead
  versions accumulate.
- **The visibility rule is the whole ballgame.** `is_visible()` encodes: *my own
  writes are visible; a committed `xmin` before my snapshot is visible; a version
  deleted by a committed `xmax` before my snapshot is not.*
- **ABORT must undo MVCC writes** — own inserts are hidden (`xmax=xid`), own
  deletes reversed (`xmax=0`) — so an aborted transaction leaves no trace.

---

## Design trade-offs

- **MVCC + Strict 2PL** removes read/write contention but still needs **deadlock
  handling** and **garbage collection** of dead versions (vacuum). PostgreSQL
  goes further with *Serializable Snapshot Isolation (SSI)*, detecting dangerous
  read-write anti-dependencies instead of paying full 2PL's throughput cost.
- **Deadlock detection is `O(V+E)` per blocked request** here. Real systems
  (PostgreSQL) run the check **periodically** (after a `deadlock_timeout`) rather
  than on every wait, trading a little latency for far less overhead on the
  common, deadlock-free path.
- **Victim selection.** Aborting the *younger* transaction (as happens here)
  tends to waste the least work and avoids livelock; production systems may use
  other cost heuristics.
- **Lock granularity.** This is row-key locking. Coarser (table) locks are
  cheaper to manage but kill concurrency; finer locks (as here) maximise
  concurrency at the cost of more lock-table bookkeeping and deadlock risk.

---

## Key learnings

- MVCC lets readers walk a **version chain** and apply a visibility rule against
  their snapshot XID — so reads see a consistent point-in-time without blocking
  writers.
- **Strict 2PL** holds every lock until commit/abort; the shrinking phase is a
  single instant, which is what eliminates cascading aborts.
- The **combination** removes read/write contention (MVCC) while still serializing
  writes (2PL) — the architecture at the heart of PostgreSQL.
- Deadlock is intrinsic to lock-based serialization; a **waits-for graph DFS** is
  the textbook detector, and aborting one victim is how the system makes progress.
- Empirically, the handout's "expected output" for Scenario 2 was incorrect
  (`3000` vs the true `2000`) — running the code, not trusting the comment, is
  the point of the lab.

---

### Reference

- Solution to `lab_sessions/lab_6.txt` (Advanced DBMS lab series).
- Concepts: MVCC visibility, Strict 2PL, waits-for deadlock detection; PostgreSQL MVCC + SSI.
- Built with `g++` 13.3.0 `-std=c++17 -pthread`, Ubuntu 24.04.
