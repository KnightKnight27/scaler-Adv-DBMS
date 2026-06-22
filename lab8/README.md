# Lab 6 — Transaction Manager: MVCC + Strict 2PL + Deadlock Detection

## Overview

This lab implements a small in-memory transaction manager that puts three
classic concurrency-control mechanisms under one roof:

1. **MVCC version chains** for read-only (snapshot) transactions, which read a
   consistent past state *without taking any locks*.
2. **Strict Two-Phase Locking (Strict 2PL)** for read/write transactions, with
   shared (S) and exclusive (X) locks, S→X upgrade, and lock release deferred
   to commit/abort.
3. **Deadlock detection** through a waits-for graph that is searched for cycles;
   when a cycle is found the youngest transaction in it is aborted.

The interesting design question is not how to implement each piece in
isolation, but *why* you would want all three together and how they divide the
work. The short answer: snapshot reads ride on MVCC so they never block and
never participate in deadlocks, while the heavier read/write traffic is kept
serializable-ish by locking, and locking's one failure mode (deadlock) is
handled by detection rather than prevention. The rest of this report explains
the reasoning behind each choice.

## Build and run

```
g++ -std=c++17 *.cpp -o txn
./txn
```

The concurrency is **simulated deterministically**. Rather than spawn real
threads — whose interleavings vary run to run and are painful to grade — each
transaction issues its operations in an explicit order, and a "blocked" request
is modelled as the request being parked on a FIFO queue with the transaction
marked `WAITING`. A later `commit`/`abort` that releases the lock wakes the
waiter. This is exactly the bookkeeping a real lock manager does, minus the OS
scheduler, so the logic is faithful while the output is reproducible.

---

## 1. MVCC version chains and visibility

### The data structure

Each key maps to the **head** of a singly linked *version chain*, newest first.
A version records:

```
struct Version {
    Value value;
    Ts    begin_ts;   // commit timestamp of the txn that created it
    Ts    end_ts;     // commit timestamp of the txn that superseded it, or INF
    prev  ----------> older version
}
```

A version is the "current" value during the half-open time interval
`[begin_ts, end_ts)`. When a writer commits a new value, the manager *caps* the
old head by setting its `end_ts` to the new commit timestamp and links a fresh
version in front of it. Nothing is mutated in place and nothing is deleted, so
old readers can still walk the chain and find what they need.

```
key "X":   [ 'x_by_t2'  5..INF ]  ->  [ 'x_by_t1'  3..5 ]  ->  [ 'x0'  1..3 ]
              (newest, current)          (superseded)           (oldest)
```

### Visibility rule

Every transaction takes a **snapshot timestamp** (`start_ts`) when it begins. A
read returns the version `v` satisfying

```
v.begin_ts <= start_ts < v.end_ts
```

i.e. the version that was current *as of the moment the reader started*. Walking
the chain from the head, the first version whose interval contains `start_ts` is
the answer. This is **snapshot isolation**: the reader sees a single,
internally consistent committed state — the one frozen at its start — regardless
of what writers do afterward.

### Why snapshot reads never block

A reader needs no lock because it is not looking at the present; it is looking
at a fixed point in the past that, by construction, is immutable. A concurrent
writer only ever *appends* a newer version and caps an `end_ts`; it never
overwrites the bytes the reader is reading. With no shared mutable state on the
read path there is nothing to lock, nothing to wait on, and therefore no way for
a read-only transaction to deadlock. Readers don't block writers and writers
don't block readers — the headline benefit of MVCC.

The demo shows this directly: reader `T1` starts (snapshot `start_ts=3`), then
writer `T2` updates `X` and commits at `commit_ts=5`. `T1` afterward reads `X`
and still sees the *old* value `x0`, because `1 <= 3 < 5`. A brand-new reader
`T3` started after the commit sees the new value `x1`.

---

## 2. Strict 2PL for read/write transactions

Read/write transactions cannot rely on MVCC alone for write/write conflicts, so
they use locking via the `LockManager`.

### S/X locks and the compatibility rule

- **Shared (S)** is taken to read; multiple readers may hold S on the same key.
- **Exclusive (X)** is taken to write; it conflicts with everything.

```
        held S    held X
req S    OK       conflict
req X  conflict   conflict
```

Only S/S is compatible. Any request that conflicts with a current holder (and
respects FIFO order so waiters are not starved) is parked on the queue.

### Lock upgrade (S → X)

A transaction that already holds S and then wants to write requests an upgrade
to X. The upgrade is granted *in place* if no other transaction currently holds
the lock; otherwise it must wait until the others release. Upgrades are a
notorious deadlock source (two txns both holding S and both wanting X wait on
each other forever) — which is precisely why the deadlock detector exists.

### "Two-phase" and why "strict"

Two-phase locking means a transaction has a *growing phase* (only acquires
locks) followed by a *shrinking phase* (only releases). The two-phase rule is
what guarantees **conflict serializability**: once you have proven you will
never grab another lock after releasing one, the schedule is equivalent to some
serial order.

**Strict** 2PL goes further: it holds *every* lock — both S and X — until the
transaction commits or aborts. The release happens all at once at the end. The
payoff is avoiding **cascading aborts**. If a transaction released its X lock
early, another transaction could read the uncommitted value; if the first then
aborted, the second has read dirty data and must abort too, possibly triggering
a chain. By holding X locks to the end, no one can read or overwrite a value
until its writer has durably committed, so an abort is purely local. This
implementation buffers writes per transaction and only installs them into the
version chain at commit time, which reinforces the same guarantee: uncommitted
writes are invisible.

The demo's scenario (b) shows `T1` taking X on `X`; `T2`'s write of `X` is
parked behind it. When `T1` commits, its locks release, `T2` is woken, acquires
the X lock, writes, and commits — clean handoff, no dirty reads.

---

## 3. Deadlock detection via the waits-for graph

Locking can deadlock. Two strategies exist: **prevention** (impose an ordering
or use timeouts so cycles can never form) and **detection** (let them form, then
break them). This lab uses detection because it is precise — it aborts a
transaction only when a real cycle exists — whereas prevention schemes like
timeouts cause false-positive aborts and ordering schemes restrict the workload.

### The graph

The lock manager maintains a directed **waits-for graph**:

```
waits_for[a] = { b, c }   means  "a is blocked waiting for b and c to release"
```

Every time a request blocks, edges are added from the requester to each
conflicting holder (and to any conflicting earlier waiter). When a lock is
granted or a transaction finishes, the corresponding edges are cleared.

### Cycle search

Right after a request blocks, the manager runs a depth-first search from the new
waiter looking for a path back to itself. A back-edge to the start node means a
cycle:

```
   T2 ---- waits for ----> T1
    ^                       |
    |                       |
    +------ waits for ------+

   T1 holds X(X), wants Y ;  T2 holds X(Y), wants X  ==>  T2 -> T1 -> T2
```

This is the situation produced by scenario (c).

### Victim selection

When a cycle is detected, one of its members is chosen as the **victim** and
aborted, which frees its locks and lets the rest proceed. This implementation
picks the **youngest** transaction (the one with the largest id / latest start).
Rationale: the youngest has typically done the least work, so rolling it back
wastes the least effort, and consistently aborting younger transactions avoids
starving older ones (an old transaction will eventually be the oldest in any
cycle and survive). The abort discards the victim's buffered writes and releases
its locks; the survivor is woken and runs to completion.

In the demo, `T2` (id 2, younger) is chosen, rolled back, and `T1` finishes.

---

## 4. How the three mechanisms combine

The division of labour is the whole point:

- **Read-only transactions** go through MVCC. They take a snapshot, never lock,
  never block, never deadlock, and never need the detector. This is where the
  scalability comes from — analytics-style reads run fully concurrently with
  writers.
- **Read/write transactions** go through Strict 2PL, which serializes their
  conflicting accesses and (because it is strict) prevents cascading aborts.
- **Deadlock detection** is the safety net for the only thing 2PL can get stuck
  on. It is consulted lazily — only when a lock request actually blocks — so it
  costs nothing on the common, conflict-free path.

A real system (PostgreSQL is the canonical example) layers exactly these ideas:
MVCC snapshots for readers, row locks for writers, and a deadlock detector that
fires after a short wait.

---

## 5. Sample program output

```
=== In-memory Transaction Manager: MVCC + Strict 2PL + Deadlock Detection ===

========================================================
  Scenario (a): MVCC snapshot read vs concurrent writer
========================================================
[BEGIN]  T1 (read-only, snapshot)  start_ts=3
[BEGIN]  T2 (read-write, 2PL)  start_ts=4
[WRITE]  T2 (X) writes X = 'x1' (buffered)
[COMMIT] T2  commit_ts=5
    version chain for X: ['x1' @T0 5..INF] -> ['x0' @T0 1..5]
[READ ]  T1 snapshot-reads X = 'x0'  (version begin=1 end=5)
  -> reader saw the pre-update value: no lock taken, no blocking.
[COMMIT] T1  commit_ts=6
[BEGIN]  T3 (read-only, snapshot)  start_ts=7
[READ ]  T3 snapshot-reads X = 'x1'  (version begin=5 end=INF)
[COMMIT] T3  commit_ts=8

========================================================
  Scenario (b): Strict 2PL blocking (no deadlock)
========================================================
[BEGIN]  T1 (read-write, 2PL)  start_ts=3
[BEGIN]  T2 (read-write, 2PL)  start_ts=4
[WRITE]  T1 (X) writes X = 'x_by_t1' (buffered)
[BLOCK]  T2 waits for X on X
    waits-for graph: T2->T1
  -> T2 is parked behind T1's X-lock.
[COMMIT] T1  commit_ts=5
         -> T2 unblocked
  -> T1 committed; T2 now owns the X-lock and proceeds.
[WRITE]  T2 (X) writes X = 'x_by_t2' (buffered)
[COMMIT] T2  commit_ts=6
    version chain for X: ['x_by_t2' @T0 6..INF] -> ['x_by_t1' @T0 5..6] -> ['x0' @T0 1..5]

========================================================
  Scenario (c): Deadlock detection and victim abort
========================================================
[BEGIN]  T1 (read-write, 2PL)  start_ts=3
[BEGIN]  T2 (read-write, 2PL)  start_ts=4
[WRITE]  T1 (X) writes X = 'x_t1' (buffered)
[WRITE]  T2 (X) writes Y = 'y_t2' (buffered)
[BLOCK]  T1 waits for X on Y
    waits-for graph: T1->T2
  (T1 now waiting on T2)
[BLOCK]  T2 waits for X on X
    waits-for graph: T2->T1  T1->T2
[DEADLK] cycle detected: T2 -> T1 -> T2
[VICTIM] choosing youngest -> T2
[ABORT]  T2  (deadlock victim)
         -> T1 unblocked
  -> T2 was the victim and is rolled back.
[WRITE]  T1 (X) writes Y = 'y_t1' (buffered)
[COMMIT] T1  commit_ts=5
    version chain for X: ['x_t1' @T0 5..INF] -> ['x0' @T0 1..5]
    version chain for Y: ['y_t1' @T0 5..INF] -> ['y0' @T0 2..5]

=== done ===
```

---

## 6. Trade-offs and discussion

**MVCC: read concurrency vs version storage.** MVCC buys lock-free, non-blocking
reads, but it pays in space: every update creates a new version and old versions
linger as long as some active snapshot might need them. A production system needs
a *garbage collector* (PostgreSQL's VACUUM, the "undo" purge in others) to
reclaim versions older than the oldest live snapshot. A long-running read-only
transaction pins the whole history and bloats storage — a real operational
hazard. There is also a read cost: a read may have to walk several chain links to
find the visible version, so write-heavy keys grow long chains and slow reads.

**2PL: correctness vs blocking and throughput.** Locking gives a simple,
well-understood serializability guarantee, but writers block other writers (and,
in a pure-2PL system, even readers), which limits throughput under contention.
Strict 2PL adds a second cost: holding locks until commit lengthens the window
during which others wait. The benefit — no cascading aborts and recoverable
schedules — is almost always worth it, which is why "strict" is the default in
practice.

**Deadlock: detection vs prevention.** Detection is precise (it aborts only on a
genuine cycle) but requires maintaining and periodically searching the
waits-for graph, and a victim's work is thrown away. Prevention via timeouts is
cheap but produces false positives (a slow-but-not-deadlocked transaction gets
killed), and prevention via lock ordering (e.g. always lock keys in id order)
avoids deadlock entirely but forces an unnatural access pattern on application
code. The detection approach here is a good middle ground for a general-purpose
manager; the main tuning knob in a real system is *when* to run detection —
running it on every block (as we do) is precise but can be expensive under heavy
contention, so production systems usually run it after a short delay.

**Victim policy.** Aborting the youngest minimises wasted work and prevents
starvation of older transactions, but it can repeatedly punish short, frequently
restarted transactions. Alternatives weigh the number of locks held or the
amount of work done; there is no universally best policy, only a choice of which
fairness/efficiency property to favour.

**Putting it together.** The combined design reflects a common real-world
philosophy: make the cheap, frequent case (reads) lock-free with MVCC, keep the
expensive case (conflicting writes) correct with strict locking, and treat
deadlock — locking's only true pathology — as a rare event to be detected and
repaired rather than prevented up front. Each mechanism covers a weakness of the
others, which is why mature databases use all three.
