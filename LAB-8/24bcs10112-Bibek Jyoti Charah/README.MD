# Lab 8 — In-Memory Transaction Manager (MVCC + 2PL)

**Author:** Bibek Jyoti Charah · **Roll No:** 24bcs10112 · **Course:** Advanced DBMS

A header-only C++17 transaction manager (`namespace minidb`) that splits
concurrency control the way PostgreSQL does: **reads ride MVCC**, **writes
ride locks**.

- **MVCC snapshot reads** — each txn reads against the snapshot it captured at
  `begin()`. Readers never block and never take locks.
- **Strict 2PL writes** — a writer takes an exclusive lock per key and holds
  it until commit/abort.
- **Deadlock detection** — a blocked write walks the waits-for graph; a cycle
  aborts the youngest (highest-id) transaction on it.
- **First-updater-wins** — a commit is rejected with `Serialization` if any
  key it wrote gained a newer committed version since its snapshot.
- **`gc()`** — prunes dead versions no live snapshot can still observe.

The simulator is single-threaded: a contended write returns `LockWait` and
the caller retries once the holder releases. That keeps the deadlock and
serialization scenarios deterministic and easy to assert on.

## Files

```
lab8/24bcs10112-Bibek Jyoti Charah/
├── txn_manager.hpp   # namespace minidb — the TxnManager class
├── main.cpp          # 6-scenario bank-account demo with assertions
├── CMakeLists.txt    # C++17, -Wall -Wextra -Wpedantic -Wshadow
└── README.MD
```

## Build & run

```bash
cmake -S . -B build && cmake --build build && ./build/txn_manager_demo
# or
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 main.cpp -o txn_manager_demo && ./txn_manager_demo
```

Expected final line: `All transaction-manager checks passed.`

## Version layout

Each key maps to a chain of `Version`s ordered by the commit timestamp at
which they appeared. A version stores:

| Field      | Meaning |
|------------|---------|
| `value`    | the cell payload (`std::string`) |
| `dead`     | true if this version is a delete (tombstone) |
| `begin_ts` | commit timestamp the version became live |
| `end_ts`   | commit timestamp it was superseded; `0` = still live |
| `by`       | id of the txn that produced it |

**Visibility rule** for snapshot `S`:
`begin_ts <= S  AND  (end_ts == 0  OR  end_ts > S)`.
Reads scan the chain newest-to-oldest and return the first visible version —
that single inequality is the whole read path.

## Lock manager + deadlock

Two small maps hold the lock state:

```
lock_owner_ : key      -> holder txn id
waits_for_  : waiter id -> holder txn id
```

`lock(t, key)`:

1. Free → grant, record in `lock_owner_` and `t.locks`.
2. Already held by `t` → reentrant success.
3. Otherwise record `waits_for_[t] = holder` and run `cycleVictim(t)`, which
   follows `waits_for_` from `t`; if it loops back, it returns the
   highest-id txn on the cycle. That victim is aborted. If the victim is `t`
   we return `Aborted`; otherwise its lock is now free, so we retry.
4. No cycle → `LockWait`.

On commit/abort, `releaseLocks` drops the txn's locks and any `waits_for_`
edges pointing at it, so its waiters can retry.

## Commit: first-updater-wins

Before installing versions, `commit()` checks every key the txn wrote: if the
latest committed version's `begin_ts` is newer than the txn's snapshot,
someone committed first and we abort with `Serialization`. Otherwise we take
`commit_ts = ++clock`, stamp the old head's `end_ts`, append a fresh version
(`begin_ts = commit_ts, end_ts = 0`), release locks, and mark `Committed`.

## `gc()`

A dead version (`end_ts != 0`) is needed only while some live snapshot can
still see it. `gc()` finds the oldest active snapshot (or the global clock if
none) and drops every version whose `end_ts` is at or below that floor.

## Demo scenarios (bank accounts)

Seeded with `alice=1000, bob=500, carol=750`, then six asserted scenarios:

| # | Scenario | Proves |
|---|----------|--------|
| 1 | A reader snapshots `alice=1000`; another txn commits `1500`. The reader still sees `1000`; a fresh reader sees `1500`. | Snapshot isolation |
| 2 | Old snapshot taken before `delete carol`; new snapshot after. Old still sees the row, new sees `<none>`. | Tombstone visibility |
| 3 | T1 locks `bob`; T2's write returns `LOCK_WAIT`; after T1 aborts, T2 retries and commits. | Strict 2PL |
| 4 | T_AB locks `alice`, T_BA locks `bob`, then each wants the other → cycle; the younger txn is auto-aborted. | Deadlock detection |
| 5 | Two txns snapshot `alice=1400`; T1 commits `2000`; T2 takes the freed lock but its commit is rejected. | First-updater-wins |
| 6 | After 1–5, `alice` has several versions; with no live snapshots `gc()` keeps only the live one. | Garbage collection |

`invariants()` runs after every scenario and checks: every lock holder is
Active; each chain has strictly increasing `begin_ts`; no `end_ts <= begin_ts`;
and only the tail version may be live.

## API

```cpp
minidb::TxnManager db;
TxId t = db.begin();
std::string v;
db.read(t, "alice", v);          // MVCC against t's snapshot
db.write(t, "alice", "1500");    // 2PL: acquires the key lock
db.remove(t, "carol");           // tombstone
db.commit(t);                    // Ok / Serialization / Aborted
db.abort(t);

db.gc();                         // prune dead versions

db.state_of(t);  db.last_victim();
db.live_txns();  db.locks_held();  db.versions();
db.dump("alice");                // version timeline, for debugging
db.invariants();                 // "" when healthy
```

All results use `enum class Result { Ok, NotFound, LockWait, Aborted, Serialization }`;
`minidb::name(Result)` / `name(State)` give printable strings.
