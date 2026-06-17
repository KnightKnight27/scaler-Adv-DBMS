# Lab 8 — Transaction Manager: MVCC + Strict 2PL (C++17)

**Name:** Gauri Shukla
**Roll Number:** 24BCS10115
**Course:** Advanced DBMS — Scaler School of Technology

An in-memory transaction manager (`mvcc::TxnManager`) that puts together the
core of a relational engine's concurrency control:

- **MVCC snapshot reads** — every transaction reads against the snapshot it took
  at `begin()`; reads take **no locks** and never block.
- **Strict two-phase locking for writes** — a writer takes an **exclusive lock**
  per key and holds all of them until commit/abort.
- **Deadlock detection** — a waits-for graph is walked on every blocked write;
  a cycle aborts the **youngest** transaction in it.
- **First-updater-wins** — at commit a writer is rejected with a serialization
  failure if a concurrent transaction already committed a new version of a key
  it wrote.
- **`gc()` / vacuum** — prunes dead row versions no live or future snapshot can
  see.

This follows PostgreSQL's actual split: **reads use MVCC visibility, writes use
row locks.** It carries forward the MVCC ideas from the Lab-2/Lab-4 study of how
SQLite and PostgreSQL keep multiple versions, and uses the same `.h`/`.cc`/
`main.cc`/`Makefile` layout as my earlier labs.

---

## Files

```
Lab-8/
├── txn_manager.h    # TxnManager API + Version / Txn / lock-table layout
├── txn_manager.cc   # MVCC visibility, 2PL lock manager, deadlock detection, commit, gc
├── main.cc          # 5 scenarios, each self-checked with assertions
└── Makefile         # c++17 build, -Wall -Wextra -Wpedantic -Wshadow
```

## Build & run

```bash
make run
# or manually:
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic main.cc txn_manager.cc -o txmgr_demo && ./txmgr_demo
```

Tested with Apple clang 21 on macOS arm64 — zero warnings, exits `0`, prints
`All transaction-manager checks passed.`

---

## 1. Model

The store is a map from key → **version chain**. Each `Version` records:

```
value, deleted(tombstone), begin_ts, end_ts, creator
```

A global **commit clock** assigns each committed transaction a `commit_ts`
(`begin_ts` of the versions it creates; `end_ts` of the versions it supersedes).
A transaction's **snapshot** is the value of the clock at `begin()`.

This is a **single-threaded, deterministic simulator**: instead of a real thread
parking on a mutex, a blocked write returns `Status::LockWait` and the caller
retries after the holder releases. That keeps the deadlock schedule exactly
reproducible while modelling the same lock-wait semantics.

## 2. MVCC visibility rule

A version is visible to a snapshot `S` when

```
begin_ts <= S   AND   (end_ts == 0  OR  end_ts > S)
```

Because committed versions occupy disjoint `[begin_ts, end_ts)` intervals, at
most one version of a key is visible to any snapshot. A transaction also sees its
own uncommitted writes (read-your-writes) by checking its write-set first. The
upshot: a reader keeps seeing the values that were committed *before* it started,
no matter how many writers commit afterwards (scenario 1).

## 3. Strict 2PL + deadlock detection

Writes take a per-key **exclusive lock** held until commit/abort (strict 2PL —
the shrinking phase happens only at end of transaction). The waits-for graph is a
**functional graph**: each waiting transaction waits for exactly one lock holder,
so detecting a cycle is just following the chain of `waiter → holder` edges until
it either loops back to the requester (deadlock) or dead-ends (a plain wait).

On a cycle the **youngest transaction** (highest id — it has done the least work)
is aborted, releasing its locks and breaking the cycle (scenario 3).

## 4. First-updater-wins

Two transactions can both snapshot a key's old value, and the exclusive lock only
serializes *when* they write — it does not stop a **lost update**. So at commit
each writer re-checks every key it wrote: if the chain holds a version with
`begin_ts > my snapshot`, a concurrent transaction already updated it, and this
commit is rejected with `SERIALIZATION_FAILURE` (scenario 4). The first committer
wins; the loser would retry on a fresh snapshot.

## 5. Garbage collection

`gc()` finds the oldest snapshot held by any active transaction and removes every
version whose `end_ts` is at or below it — those are invisible to all current and
future transactions. Live (`end_ts == 0`) versions are always kept (scenario 5).

## 6. Public API

```cpp
mvcc::TxnManager tm;
mvcc::TxId t = tm.begin();

std::string v;
tm.read(t, "x", v);              // Ok / NotFound (MVCC snapshot read)
tm.write(t, "x", "42");          // Ok / LockWait / Aborted (2PL X-lock)
tm.remove(t, "x");               // Ok / LockWait / Aborted / NotFound
tm.commit(t);                    // Ok / SerializationFailure / Aborted
tm.abort(t);

tm.gc();                         // reclaim dead versions
tm.state(t); tm.last_victim();   // introspection for tests
```

`Status` values: `OK, NOT_FOUND, LOCK_WAIT, ABORTED, SERIALIZATION_FAILURE`
(`mvcc::to_string`).

## 7. What `./txmgr_demo` runs

| # | Scenario | Asserted outcome |
|---|----------|------------------|
| 1 | MVCC snapshot isolation | a reader still sees `x=100` after another txn commits `x=200`; a new txn sees `200` |
| 2 | Strict 2PL lock-wait | second writer on `y` gets `LOCK_WAIT`; proceeds after the holder releases |
| 3 | Deadlock | `T1→a, T2→b`, then each wants the other's key → cycle → **younger** txn aborted |
| 4 | First-updater-wins | the later committer of `z` is rejected with `SERIALIZATION_FAILURE`; `z=10` wins |
| 5 | `gc()` | dead versions pruned; latest values survive |

Every line prints `[pass]`/`[FAIL]`; the program exits non-zero on the first
failure.

## 8. Design notes / how this differs from a shared-lock reference

- **Reads are lock-free MVCC** (no shared read-locks) — the PostgreSQL model —
  so readers genuinely never block writers. A pure-2PL design that takes S-locks
  on reads would block them; combining S-locks *and* MVCC is redundant.
- Deadlock detection exploits the functional shape of the waits-for graph (one
  out-edge per waiter), so cycle finding is an `O(path)` chain walk rather than a
  general DFS.
- Deterministic single-threaded scheduling (`LockWait` + retry) instead of real
  threads/condition variables, which makes the deadlock and first-updater-wins
  scenarios fully reproducible under `make run`.
