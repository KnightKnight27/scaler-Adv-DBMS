# Lab 8: Transaction Manager (MVCC + Strict 2PL)

A small in-memory transaction manager in C++17 that combines three pieces:

1. **MVCC** for reads. Writes append new row versions instead of mutating in
   place. Readers walk a per-key version chain and pick the one visible to
   their snapshot.
2. **Strict Two-Phase Locking** for writes. Every write takes an exclusive
   lock on the row key and holds it until commit or abort. Reads take a
   shared lock (so a writer cannot start while a reader is mid-statement,
   and vice versa).
3. **Deadlock detection** by DFS on a waits-for graph. When a cycle is
   found, the youngest transaction in the cycle is the victim.

Build and run:

```
g++ -std=c++17 -Wall -Wextra -pthread main.cpp -o txmgr && ./txmgr
```

## What's in the code

One file, `main.cpp`. The public surface is `TxManager`:

```
TxId  begin()
optional<string>  read(tx, key)
void  write(tx, key, value)
void  remove(tx, key)
void  commit(tx)
void  abort(tx)
size_t vacuum()                   prunes dead versions, returns count
size_t chain_length(key)          for the vacuum demo
```

Errors come back as `TxAborted` exceptions: deadlock victim, lost-update
detection, or attempting to acquire a lock after the transaction has entered
the shrinking phase.

## How visibility works

Each `Version` carries `xmin` (the tx that created it) and `xmax` (the tx
that invalidated it; `0` means still live). Tombstones (deletions) push a
version with `tombstone = true`.

Every transaction stores a `snapshot` value, which is the global commit
counter at the moment `begin()` was called. A transaction commits by
incrementing the counter and stamping that value as its `commit_ts`.

A version is visible to reader `R` iff:

```
( v.xmin == R OR (v.xmin is committed AND v.xmin.commit_ts <= R.snapshot) )
AND
( v.xmax == 0
  OR NOT (v.xmax == R OR (v.xmax is committed AND v.xmax.commit_ts <= R.snapshot)) )
```

Using a commit counter (not the xid) for the snapshot is what lets the
reader correctly ignore transactions that have a lower xid than itself but
committed after it started.

## Lost-update detection

Strict 2PL by itself does not prevent lost updates under snapshot
isolation. The exclusive lock guarantees serial execution of the writers,
but the second writer's snapshot was taken before the first writer
committed, so it reads stale data and overwrites with a value that ignores
the first commit.

To stop that, `write()` and `remove()` walk the chain after acquiring the X
lock. If any version was committed by someone other than the current tx and
that committer's `commit_ts > my snapshot`, we throw with the same message
Postgres uses: `could not serialize access`. The application is expected
to retry.

This is the "first updater wins" rule. Scenario 6 in `main()` exercises it.

## Deadlock detection

`acquire()` builds outgoing edges in `waits_for` before going to sleep. It
then runs a DFS from the requester. If the DFS finds the requester back on
the stack, the cycle is real and the highest-xid transaction in the cycle
becomes the victim.

Picking the youngest as the victim (rather than always aborting the
requester) is cheaper on average: the older tx has done more work, so
killing the younger one wastes less.

If the requester is the victim, we throw `TxAborted` directly. Otherwise we
mark the victim as `Aborted`, drop its locks, and `notify_all`. The
victim's own thread, currently blocked in `cv.wait`, wakes up, sees its
status at the top of the `acquire` loop, and throws.

## Vacuum

`vacuum()` finds the oldest snapshot still held by any active transaction
and prunes every version whose `xmax` is committed with `commit_ts <`
that snapshot. Those versions are guaranteed to be invisible to every
current and future transaction and are safe to drop. Scenario 7 leaves a
chain of length 5 and `vacuum()` brings it down to 1 (the only live
version).

## Demos in `main()`

| # | What it shows |
|---|---|
| 1 | Reader started before writer's commit keeps seeing the pre-write value |
| 2 | Two concurrent readers both get shared locks, nobody blocks |
| 3 | Reader blocked on a writer's X lock unblocks at commit but stays at its own snapshot (correct SI) |
| 4 | A single holder's S lock upgrades to X without re-queuing |
| 5 | Deadlock: T1 holds A and waits for B, T2 holds B and waits for A. Younger one aborts |
| 6 | Two txns try to update the same row. First wins, second is told to retry |
| 7 | `vacuum()` prunes dead versions older than the oldest active snapshot |

## Expected output

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
  tx 10 aborted: deadlock: victim 10
  tx 9 committed
=== 6. SI rejects a lost update (first-updater-wins) ===
  tx 12 committed counter=1
  tx 13 aborted: could not serialize access: row touched by tx 12
=== 7. vacuum prunes dead versions ===
  vkey chain length before vacuum: 5
  vacuum pruned 8 dead versions (across all keys)
  vkey chain length after vacuum:  1
```

## Choices and limitations

- One-file, single-process, in-memory. No persistence, no WAL, no recovery
  on restart.
- No predicate locks. Phantoms are possible (a `SELECT ... WHERE ...` does
  not block a later `INSERT` matching the predicate). Real SERIALIZABLE
  would need SSI or table-level locking.
- `read()` takes shared locks. Pure SI would let reads skip locks
  entirely; combining 2PL with MVCC is what real systems do for
  SERIALIZABLE, which is what this lab targets.
- Abort is lazy: we never undo work in the heap. Aborted versions are
  filtered out by the visibility rule and eventually collected by
  `vacuum()`.
