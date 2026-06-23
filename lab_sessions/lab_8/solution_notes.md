# Lab 8 — Full Transaction Manager: MVCC + Strict 2PL + Deadlock Detection + Vacuum

## Concept

This lab builds a complete in-memory transaction manager that mirrors the core of PostgreSQL's concurrency architecture:

- **MVCC** — each write produces a new row version tagged with the writer's transaction ID. Readers walk the version chain and apply a visibility rule against their snapshot. No reader ever blocks a writer.
- **Strict 2PL** — writers acquire exclusive locks before touching a row and hold them until commit or abort. Prevents lost updates and guarantees serializability for writes.
- **Deadlock detection** — waits-for graph built dynamically; DFS detects cycles and aborts the blocked transaction.
- **Vacuum** — old versions that are no longer visible to any active transaction are pruned from the version chains to reclaim memory (mirrors PostgreSQL's `VACUUM`).

## Approach

### Transaction table
Each transaction has: `tid`, `snap` (snapshot stamp at start), `commitStamp`, `state` (Running/Finished/Killed), `shrinking` (2PL phase flag).
A global atomic `globalStamp` increments on each commit, providing a monotonic timeline.

### MVCC version chain
`store: map<RowKey, list<RowVersion>>` where each version has `{data, creator, invalidator, deleted}`.

Visibility rule for transaction T reading version V:
- `V.creator == T` (own uncommitted write) → visible
- OR `V.creator` committed with `commitStamp ≤ T.snap` → visible
- AND `V.invalidator == 0` (not deleted) OR invalidator committed after T's snapshot or was killed

**put()**: find current visible version → stamp its `invalidator = tx` → push new version to front.
**erase()**: stamp visible version's `invalidator = tx`, push a tombstone (`deleted=true`).
**abort()**: undo — mark own inserts as invisible, un-mark own deletions.

### Lock manager
`lockTable: map<RowKey, LockEntry>` where each entry has a list of current owners and a condition variable for waiters.
- Shared locks are mutually compatible.
- Any exclusive lock conflicts with everything else.
- On conflict: record waits-for edges, run DFS, throw `TxnFailure` on cycle.
- On commit/abort: remove all locks for that transaction, `notify_all()` to wake waiters.

### Vacuum
Compute the minimum snapshot across all running transactions (`horizon`). Any version whose `invalidator` committed before `horizon` is invisible to everyone alive — safe to delete.

## Solution

`main.cpp` — `TxManager` class with 5 scenarios:

**Scenario 1 — Snapshot isolation**: T2 starts before T3 commits `balance=2000`. T2 reads `balance=1000` — correct, T3's commitStamp is beyond T2's snapshot.

**Scenario 2 — Concurrent reads**: T4 and T5 both hold shared locks simultaneously — no conflict, both served.

**Scenario 3 — Write blocks read**: T6 holds exclusive lock while writing. Reader thread T7 blocks on condition variable. T6 commits → notify_all → T7 wakes and reads the new value.

**Scenario 4 — Deadlock**: T8 locks X, T9 locks Y. T8 tries to lock Y, T9 tries to lock X → cycle in waits-for graph → DFS detects it → younger transaction aborted, older commits.

**Scenario 5 — Vacuum**: After all transactions commit, vacuum prunes stale versions. 2 versions pruned in this run.

## Key Takeaway

MVCC and 2PL solve different problems: MVCC eliminates read-write contention, 2PL serializes write-write conflicts. Together they give snapshot isolation with write serializability. Deadlock detection is unavoidable with 2PL — PostgreSQL runs it on a background timer. Vacuum is essential with MVCC to prevent unbounded version chain growth.
