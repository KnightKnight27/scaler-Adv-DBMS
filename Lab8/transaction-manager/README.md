# Lab 8: Transaction Manager (MVCC + Strict 2PL)

A compact in-memory transaction manager in C++17 that stitches three ideas
together:

1. **MVCC for reads.** Writes never overwrite a row in place — they append a
   new version. A reader walks the per-key version chain and keeps the first
   one visible to its own snapshot.
2. **Strict Two-Phase Locking for writes.** A write grabs an exclusive lock on
   the row key and holds it until commit/abort. A read grabs a shared lock, so
   a writer can't start mid-read and a reader can't start mid-write.
3. **Deadlock detection** via DFS over a waits-for graph. On a cycle, the
   youngest transaction in it is killed.

Build and run:

```
g++ -std=c++17 -Wall -Wextra -pthread main.cpp -o txmgr && ./txmgr
```

## The public API

Everything lives in `main.cpp`. The surface is `TxnEngine`:

```
TxnId  start()
optional<string>  fetch(tx, key)
void  store_(tx, key, value)
void  erase_(tx, key)
void  commitTxn(tx)
void  abortTxn(tx)
size_t  gc()                       prunes dead versions, returns the count
size_t  versionCount(key)          used by the gc demo
```

Failures surface as `TxnFailure` exceptions — the deadlock victim, a rejected
lost update, or a lock request made after the transaction entered its
shrinking phase.

## How visibility is decided

Each `RowVersion` records `creator` (the tx that wrote it) and `invalidator`
(the tx that superseded/deleted it; `0` means still live). A deletion pushes a
version with `deleted = true`.

Every transaction stores a `snap` — the value of the global commit counter at
the moment `start()` ran. Committing bumps that counter and stamps the new
value as the tx's `commitStamp`.

A version is visible to reader `R` when:

```
( creator == R OR (creator finished AND creator.commitStamp <= R.snap) )
AND
( invalidator == 0
  OR NOT (invalidator == R OR (invalidator finished AND invalidator.commitStamp <= R.snap)) )
```

Snapshotting on the commit counter (not the tx id) is the trick that lets a
reader correctly skip a transaction with a smaller id that nonetheless
committed *after* the reader began.

## Catching lost updates

Strict 2PL on its own doesn't stop lost updates under snapshot isolation. The
exclusive lock serializes the writers, but the second writer took its snapshot
before the first committed, so it reads stale data and clobbers the first
commit.

To block that, `store_()` and `erase_()` re-scan the chain after taking the X
lock. If a version was committed by someone else whose `commitStamp` is newer
than my snapshot, it throws with the familiar Postgres wording —
`could not serialize access` — and the caller is expected to retry.

That's the "first updater wins" rule. Demo 6 in `main()` drives it.

## Deadlock detection

`lockRow()` records its outgoing edges in `waitGraph` before blocking, then
runs a DFS from the requester. If the requester reappears on the stack, the
cycle is genuine and the **highest-id** (youngest) transaction in it is chosen
as the victim.

Killing the youngest rather than always aborting the requester is cheaper on
average — the older tx has done more work, so sacrificing the younger one
wastes less.

If the requester *is* the victim, it throws `TxnFailure` straight away.
Otherwise the victim is marked `Killed`, its locks are dropped, and
`notify_all` wakes it; the victim's thread, parked in `cv.wait`, sees its state
at the top of the `lockRow` loop and throws.

## Garbage collection (gc / vacuum)

`gc()` finds the oldest snapshot still held by any running transaction and
drops every version whose `invalidator` committed with `commitStamp <` that
horizon. Those versions can't be seen by any current or future transaction, so
they're safe to reclaim. Demo 7 builds a chain of length 5 and `gc()` shrinks
it to 1 (the single live version).

## What `main()` demonstrates

| # | What it shows |
|---|---------------|
| 1 | A reader that started before a writer committed keeps seeing the old value |
| 2 | Two concurrent readers both take shared locks; neither blocks |
| 3 | A reader blocked on a writer's X lock unblocks at commit but stays at its own snapshot |
| 4 | A sole holder upgrades its S lock to X without re-queuing |
| 5 | Deadlock: T1 holds X waits for Y, T2 holds Y waits for X — the younger aborts |
| 6 | Two txns update the same row; first wins, second is told to retry |
| 7 | `gc()` prunes dead versions older than the oldest active snapshot |

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

(See `screenshot.png` for the run.)

## Trade-offs and limits

- One file, single process, fully in memory. No persistence, no WAL, no
  crash recovery.
- No predicate locks, so phantoms are possible — a `SELECT ... WHERE ...`
  won't block a later matching `INSERT`. True SERIALIZABLE would need SSI or
  table-level locking.
- `fetch()` takes shared locks. Pure SI would skip read locks entirely;
  pairing 2PL with MVCC is what real systems do for SERIALIZABLE, which is the
  target here.
- Abort is lazy — nothing is undone in the heap. Aborted versions are filtered
  out by the visibility rule and eventually reclaimed by `gc()`.
