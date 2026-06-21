# Lab 8 — Transaction Manager (MVCC + Strict 2PL + Deadlock Detection)

**Name:** Akshat Kushwaha
**Roll No:** 24BCS10060

## What this lab is about

This lab ties together how a database lets many transactions run "at the same
time" without corrupting data. I built a small in-memory transaction manager that
combines the three core ideas:

- **MVCC** for reads — keep multiple versions of each value so readers see a
  stable snapshot and never block.
- **Strict Two-Phase Locking (2PL)** for writes — a writer locks each key it
  changes and holds the lock until commit/abort.
- **Deadlock detection** — when two transactions wait on each other, find the
  cycle and abort one of them.

I kept it single-threaded and deterministic so the behaviour is easy to follow
and the demo prints the same result every time.

## Files

| File | What it does |
|---|---|
| `txn_manager.cpp` | `TxnManager` class + a 3-scenario demo with pass/fail checks |

## Build & run

```bash
g++ -std=c++17 -Wall -Wextra txn_manager.cpp -o txn_manager
./txn_manager
```

Output:

```
=== 1) MVCC snapshot isolation ===
  [pass] reader sees alice=100
  [pass] old reader STILL sees alice=100
  [pass] fresh reader sees alice=175

=== 2) Strict 2PL - second writer blocks ===
  [pass] T1 locks bob
  [pass] T2 is blocked on bob
  [pass] T2 gets the lock after T1 aborts
  [pass] bob is now 70

=== 3) Deadlock detection - youngest aborted ===
  [pass] T_a locks alice
  [pass] T_b locks bob
  [pass] T_a waits for bob (held by T_b)
    [deadlock] cycle found, aborting youngest txn T9
  [pass] T_b (youngest) chosen as deadlock victim
  [pass] T_a can now take bob and finish
```

## MVCC — multi-version reads

Every key keeps a **chain of versions**. A version has:

| Field | Meaning |
|---|---|
| `value` | the stored value |
| `begin_ts` | the commit time at which this version became visible |
| `end_ts` | the commit time at which it was replaced (`0` = still the live one) |

When a transaction calls `begin()`, it records a **snapshot** = the current
commit clock. A version is visible to that snapshot if:

```
begin_ts <= snapshot   AND   (end_ts == 0  OR  end_ts > snapshot)
```

So an **update never overwrites** the old value — it sets the old version's
`end_ts` and appends a new version. That's why in scenario 1 the old reader keeps
seeing `alice = 100` even after another transaction commits `alice = 175`: the
`100` version is still the one visible to the reader's older snapshot. A
transaction also sees its own un-committed writes first (from a pending buffer).
This is exactly how PostgreSQL lets readers and writers run without blocking each
other.

## Strict 2PL — write locks

Reads use MVCC, but writes still need locks so two transactions don't clobber the
same key. Each write takes an **exclusive lock** on the key:

- if the lock is free → take it and buffer the new value (uncommitted),
- if I already hold it → just update my buffered value,
- if someone else holds it → I have to **wait** (`write` returns `Blocked`).

"Strict" means all locks are held until the transaction **commits or aborts** —
they are never released early. On commit the buffered writes are installed as new
versions and all locks are released at once (scenario 2 shows T2 blocked until T1
aborts, then succeeding).

## Deadlock detection — waits-for graph

Locks can deadlock: T_a holds `alice` and wants `bob`, while T_b holds `bob` and
wants `alice`. Neither can proceed. I track a **waits-for graph**: an edge
`waiter -> holder` is added whenever a write blocks. After adding an edge I follow
the edges from the waiter; if I get back to where I started, there's a **cycle**.

To break it I abort the **youngest** transaction in the cycle (the one with the
highest id) and release its locks, so the other can continue. In scenario 3,
T_a (older) and T_b (younger) cross, the cycle is detected, and **T_b is the
victim** — after it aborts, T_a takes the lock and commits.

```
T_a --wants bob--> T_b
 ^                  |
 |                  |
 +----wants alice---+        cycle!  -> abort youngest (T_b)
```

## Putting it together

- **Reads** go through MVCC → never blocked, always consistent to the snapshot.
- **Writes** go through 2PL locks → serialised per key, held to commit.
- **Deadlocks** between writers are detected with a waits-for cycle check and
  resolved by aborting the youngest.

This is a simplified version of what PostgreSQL does: snapshot-isolation reads
plus row/lock-level write serialisation, with a deadlock detector that picks a
victim.

## Key takeaways

- MVCC means an update *adds a version* instead of overwriting, tagged with
  begin/end timestamps; visibility is decided by the reader's snapshot.
- Because readers use snapshots, **readers never block writers and writers never
  block readers**.
- Strict 2PL holds every write lock until commit/abort, which keeps writes
  correct but can cause deadlocks.
- A waits-for graph + cycle detection finds deadlocks; aborting the youngest
  transaction is a simple, fair way to break the cycle.
