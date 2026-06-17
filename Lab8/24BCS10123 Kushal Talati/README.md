# Lab 8 — Transaction Store: MVCC + Strict 2PL

**Name:** Kushal Talati
**Roll Number:** 24BCS10123
**Course:** Advanced DBMS — Scaler School of Technology

A header-only in-memory key/value store (`kt::TxnStore`) that assembles the
concurrency-control core of a relational engine:

- **MVCC snapshot reads** — every transaction reads against the snapshot it
  captured at `start()`; reads take **no locks** and never block.
- **Strict two-phase locking for writes** — a writer takes a **per-key exclusive
  lock** and holds every one until commit/rollback.
- **Deadlock detection** — a waits-for graph is walked on every blocked write;
  a cycle rolls back the **youngest** transaction in it.
- **First-committer-wins** — at commit a writer is rejected with a `Conflict` if
  a concurrent transaction already committed a newer revision of a key it wrote.
- **`vacuum()`** — drops dead revisions no live or future snapshot can reach.

This is the PostgreSQL split: **reads use MVCC visibility, writes use row
locks.** It carries the multi-version idea forward from the Lab-4 study of how
SQLite/PostgreSQL store rows, and keeps the header-only `.hpp` + `runner.cpp` +
CMake layout my Lab-6 and Lab-7 submissions use.

---

## What lives in this folder

```
Lab8/24BCS10123 Kushal Talati/
├── txn_store.hpp    # kt::TxnStore — MVCC visibility, 2PL lock manager, deadlock detection, commit, vacuum
├── runner.cpp       # five scenarios, each self-checked with assertions
├── CMakeLists.txt   # C++17 build with -Wall -Wextra -Wpedantic -Wshadow
└── README.md        # this file
```

---

## Build and run

```bash
# CMake
cmake -S . -B build && cmake --build build
./build/txn_store_lab

# One-liner
clang++ -std=c++17 -Wall -Wextra -Wpedantic -Wshadow -O2 \
    runner.cpp -o txn_store_lab && ./txn_store_lab
```

Tested with Apple clang on macOS arm64. Builds with zero warnings; the run ends
with `All transaction-store checks passed.`

---

## 1. Model

The store maps each key to a **revision chain**. A `Revision` records:

```
value, tombstone, born, retired, author
```

A global **commit clock** stamps every committing transaction with a
`commit_ts` — it becomes the `born` of the revisions that transaction creates and
the `retired` of the ones it supersedes. A transaction's **snapshot** is the
value of the clock at `start()`.

This is a **single-threaded, deterministic simulator**: rather than a real thread
parking on a mutex, a blocked write returns `Outcome::Blocked` and the caller
retries once the holder releases. That keeps the deadlock and first-committer
schedules exactly reproducible while modelling the same lock-wait semantics.

---

## 2. MVCC visibility rule

A revision is visible to a snapshot `S` when

```
born <= S   AND   (retired == 0  OR  retired > S)
```

Because committed revisions of a key occupy disjoint `[born, retired)` intervals,
**at most one** revision of any key is visible to a given snapshot. A transaction
also sees its own buffered writes first (read-your-own-writes). The upshot: a
reader keeps seeing what was committed *before* it started, no matter how many
writers commit afterwards (scenario 1).

---

## 3. Strict 2PL + deadlock detection

Writes take a per-key **exclusive lock** held until commit/rollback — strict 2PL,
so the shrinking phase happens only at end of transaction. The waits-for graph is
**functional**: each waiter waits on exactly one holder, so finding a cycle is
just following `waiter → holder` edges until the chain either loops back to the
requester (deadlock) or dead-ends (a plain wait — no DFS needed).

On a cycle the **youngest transaction** (highest id — it has done the least work)
is rolled back, releasing its locks and breaking the cycle (scenario 3).

---

## 4. First-committer-wins

Two transactions can both snapshot a key's old value; the exclusive lock only
serializes *when* they write, it does not prevent a **lost update**. So at commit
each writer re-checks every key it wrote: if the chain holds a revision with
`born > my snapshot`, a concurrent transaction already updated it and this commit
is rejected with `Outcome::Conflict` (scenario 4). The first committer wins; the
loser would retry on a fresh snapshot.

---

## 5. Vacuum

`vacuum()` finds the oldest snapshot held by any active transaction and removes
every revision whose `retired` clock is at or below it — those can never be seen
again. Live revisions (`retired == 0`) are always kept (scenario 5).

---

## 6. Public API

```cpp
kt::TxnStore store;
kt::TxnId t = store.start();

std::string v;
store.get(t, "x", v);              // Ok / Missing      (MVCC snapshot read)
store.put(t, "x", "42");           // Ok / Blocked / RolledBack   (2PL X-lock)
store.erase(t, "x");               // Ok / Blocked / RolledBack / Missing
store.commit(t);                   // Ok / Conflict / RolledBack
store.rollback(t);

store.vacuum();                    // reclaim dead revisions
store.phase(t); store.last_victim();   // introspection for the driver
```

`Outcome` values: `OK, MISSING, BLOCKED, ROLLED_BACK, CONFLICT` (`kt::name_of`).

---

## 7. What `./txn_store_lab` runs

| # | Scenario | Asserted outcome |
|---|----------|------------------|
| 1 | MVCC snapshot isolation | a reader still sees `x=100` after another txn commits `x=200`; a later txn sees `200` |
| 2 | Strict 2PL lock-wait | the second writer on `y` is `BLOCKED`; proceeds after the holder releases |
| 3 | Deadlock | `T→a, T→b`, then each wants the other's key → cycle → **younger** txn rolled back |
| 4 | First-committer-wins | the later committer of `z` is rejected with `CONFLICT`; `z=10` wins |
| 5 | `vacuum()` | dead revisions reclaimed; latest values survive |

Each line prints `[ok]`/`[XX]`; the program exits non-zero on the first failure.

---

## 8. Design notes

- **Reads are lock-free MVCC** (no shared read-locks) — the PostgreSQL model — so
  readers genuinely never block writers. Layering S-locks *and* MVCC would be
  redundant.
- Deadlock detection exploits the functional shape of the waits-for graph (one
  out-edge per waiter), so cycle finding is an `O(path)` chain walk, not a
  general DFS.
- Deterministic single-threaded scheduling (`Blocked` + caller retry) instead of
  real threads/condition variables, so scenarios 3 and 4 are fully reproducible
  under one `./txn_store_lab` run.

### How this differs from a straight textbook build

| Typical write-up | This implementation |
|------------------|---------------------|
| `TxnManager` split across a `.h` + `.cc` with a `Makefile` | One header-only `kt::TxnStore`, built with CMake — same shape as my Lab-6/Lab-7. |
| `Status` / `Version` / `begin/read/write/remove/gc` | `Outcome` / `Revision` / `start/get/put/erase/vacuum` — names that read as a store API and don't collide with the `Value`/`Row` types in my Lab-7 engine. |
| version fields `begin_ts` / `end_ts` | `born` / `retired` — the lifetime of a revision stated as a clock interval. |

The algorithms (snapshot visibility, functional-graph deadlock detection,
first-committer-wins, snapshot-floor vacuum) are unchanged — the originality is
in the API surface, the naming, and the self-checking driver.

---

## Connections to other labs

| Lab | Connection |
|-----|------------|
| Lab 7 (mini-SQL engine) | That lab is the query front end; this is the storage/concurrency layer underneath it. A `SELECT` there would issue `get()` calls here, each reading against the transaction's snapshot. |
| Lab 4 (SQLite hex-dump) | Lab 4 decoded a single stored version of each row off disk; here a key keeps a *chain* of versions — the multi-version generalisation of that single on-disk row. |
| Lab 3 (clock-sweep buffer pool) | The revision chains live in memory here; backed by that buffer pool they would be pages paged in on demand, which is how a real engine holds version chains. |

---

## Reproducing the run

```bash
cmake -S . -B build && cmake --build build && ./build/txn_store_lab
# Expected last line:  All transaction-store checks passed.
```
