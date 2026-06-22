# MiniDB — A Teaching Relational Database Engine

MiniDB is a small but complete relational database engine written from scratch in
**C++14**. It integrates a paged storage engine, a B+Tree index, a SQL parser and
Volcano-style execution engine, a cost-based optimizer, transactions with Strict
Two-Phase Locking, and Write-Ahead-Logging crash recovery — plus an **MVCC
extension (Track B)**.

It is built to run on the provided toolchain (**MinGW g++ 6.3.0, Windows, PowerShell**)
with **no external dependencies and no build system** other than a PowerShell script
that drives `g++` directly.

---

## Team Information

> **Team Name:** `TEAM_<FILL_ME>`
>
> | Full Name | Scaler Email | Roll Number |
> | --------- | ------------ | ----------- |
> | _<your name>_ | _<your scaler email>_ | _SCALER<xxxxx>_ |

*(Solo submission — please confirm with the instructor, as the guidelines suggest a
minimum team size of 2.)*

---

## 1. Project Overview

- **Problem statement.** Build a working relational database engine from foundational
  components and integrate them into one coherent system, demonstrating an
  understanding of database internals rather than feature count.
- **Goals.** A correct, demonstrable end-to-end path:
  `SQL → parser → optimizer → operators → index/heap → buffer pool → disk`, with
  serializable transactions and durable crash recovery.
- **Chosen extension track.** **Track B — Concurrency (MVCC):** Multi-Version
  Concurrency Control with snapshot-isolation reads that never block, benchmarked
  against the 2PL baseline.

## 2. System Architecture

```
                      ┌──────────────────────────────┐
   SQL text  ───────► │  Lexer → Parser → AST         │   (src/sql)
                      └──────────────┬───────────────┘
                                     ▼
                      ┌──────────────────────────────┐
                      │  Optimizer (cost/selectivity) │   (src/optimizer)
                      │  picks SeqScan vs IndexScan,   │
                      │  join order                    │
                      └──────────────┬───────────────┘
                                     ▼
                      ┌──────────────────────────────┐
                      │  Executor: Volcano operators  │   (src/exec)
                      │  SeqScan IndexScan Filter      │
                      │  Project NestedLoopJoin        │
                      └───────┬───────────────┬───────┘
                              ▼               ▼
                  ┌────────────────┐   ┌────────────────┐
                  │  B+Tree index  │   │  Heap file     │   (src/index, src/storage)
                  │  PK → RID      │   │  fixed slots   │
                  └───────┬────────┘   └───────┬────────┘
                          └───────┬────────────┘
                                  ▼
                      ┌──────────────────────────────┐
                      │  Buffer Pool (CLOCK eviction) │   (src/storage)
                      └──────────────┬───────────────┘
                                     ▼
                      ┌──────────────────────────────┐
                      │  Disk Manager (single file,    │   (src/storage)
                      │  4 KB pages)  +  WAL           │   (src/recovery)
                      └──────────────────────────────┘

   Cross-cutting:  Catalog (src/catalog)   Transactions/2PL/MVCC (src/txn)
```

- **Major modules**: storage, index, record, catalog, sql, exec, optimizer, txn, recovery.
- **Data flow**: a statement is tokenized and parsed into an AST; the optimizer chooses
  access paths and join order; the executor builds an operator tree that pulls rows from
  the index and/or heap through the buffer pool; mutations go through the WAL.

## 3. Storage Layer

- **Page format.** Fixed **4096-byte** pages. The single database file (`minidb.db`)
  holds all pages; **page 0 is the serialized catalog**. Each heap page is:
  `[next_page:4][record_size:2][num_used:2]` header followed by fixed-size slots
  `[status:1][payload:record_size]`. `status` 1 = live, 0 = free/deleted.
- **Fixed-length tuples.** The type system is intentionally small — `INT` (4 bytes) and
  `VARCHAR(n)` (padded to `n`). This makes every tuple fixed-length, so slotted pages
  reduce to simple fixed-slot arrays, deletion is a single status byte, and WAL images
  are trivial byte ranges. Tuples are serialized field-by-field, little-endian (never a
  padded struct), so the on-disk format is portable.
- **Heap files.** A singly-linked chain of pages with O(1) insert into the first free
  slot, `Get`/`Update`/`Delete` by RID, and a full `Scan`.
- **Buffer pool.** A fixed array of frames with a **CLOCK (second-chance)** replacement
  policy, pin/unpin reference counting, dirty-page write-back on eviction, and hit /
  miss / eviction counters. *(See `.test storage`.)*

## 4. Indexing

- **B+Tree design** (`src/index/bplus_tree.*`). A disk-resident B+Tree keyed by `INT`,
  values are `RID`s. A small **header page** stores the current root id so root changes
  from splits are persisted automatically.
- **Node structure.** Each node is one page: `[is_leaf:1][count:2][next:4]` header, a key
  array, then either a RID array (leaf) or a child-pointer array (internal). Leaves are
  linked left-to-right for ordered **range scans**.
- **Search path.** Descend from the root: at each internal node pick the child for the
  key (`i = first index with key < keys[i]`), repeat until a leaf, then scan the leaf.
  Insert splits leaves/internals bottom-up and grows a new root when needed.
- **Operations.** Search, Insert (with split propagation), Delete (lazy — removed from
  the leaf without merge; a documented MVP trade-off), Range, ScanAll.
  *(See `.test index`.)*

## 5. Query Execution

- **Parser** (`src/sql`). A hand-written lexer plus a **recursive-descent parser**
  producing an AST. Supported SQL: `CREATE TABLE` (with `PRIMARY KEY`), `INSERT`,
  `SELECT` (`*` or column list, `WHERE` with `AND`, inner `JOIN ... ON`), `DELETE`,
  and an `EXPLAIN` prefix. `--` line comments are supported.
- **Plan generation.** The executor resolves columns (qualified `table.column`), asks
  the optimizer for access paths, and assembles an operator tree.
- **Operator execution.** A **Volcano (iterator) model** — every operator implements
  `Open / Next / Close`: `SeqScan`, `IndexScan`, `Filter`, `Project`, and
  `NestedLoopJoin`. The join uses a per-outer-row inner factory, so supplying an
  `IndexScan` factory yields an **index-nested-loop join**.

## 6. Optimizer

- **Cost estimation** (`src/optimizer`). A simple textbook model: a sequential scan
  costs ≈ row count; an index scan costs ≈ estimated matching rows + tree height.
- **Selectivity estimation.** Equality on the (unique) primary key ≈ `1/N`; generic
  equality ≈ `0.1`; range predicates ≈ `0.3`; `!=` ≈ `0.9`. Primary-key predicates are
  combined into `[low, high]` key bounds.
- **Access-path choice.** Index scan is chosen when a PK bound exists *and* its estimated
  cost beats a full scan; otherwise a sequential scan. `EXPLAIN` prints the decision and
  the costs.
- **Join ordering.** For a two-table inner join the **smaller relation drives** the loop
  (outer), and the inner side uses its PK index when the join column is its primary key.

## 7. Transactions & Concurrency

- **Locking strategy** (`src/txn`). A `LockManager` implementing **Strict Two-Phase
  Locking** with shared (read) and exclusive (write) locks, lock upgrades, and a FIFO
  wait queue. Locks are held until commit/abort (strict 2PL ⇒ **serializable**).
- **Concurrency model.** The target toolchain lacks `std::thread`/`std::mutex`, so
  concurrency is driven by a **deterministic, single-threaded scheduler**: a blocked
  lock request is parked as *waiting* rather than blocking an OS thread. This makes
  every concurrency scenario (including deadlocks) **fully reproducible** for the viva.
- **Deadlock handling.** A **wait-for graph** is built from waiting↔granted conflicts;
  a DFS finds cycles and aborts the **youngest** transaction as the victim; releasing its
  locks wakes the survivors. *(See `.test txn`.)*

## 8. Recovery

- **WAL design** (`src/recovery`). An append-only write-ahead log (`minidb.wal`) with a
  **STEAL / NO-FORCE** policy. The WAL is forced to disk at commit ("force-log-at-commit"),
  so a committed transaction is durable even if its data pages are still in the buffer
  pool at crash time.
- **Log records.** `BEGIN`, `UPDATE(txn, cell, before, after)`, `COMMIT`, `ABORT`,
  fixed-size and packed little-endian.
- **Crash recovery procedure (ARIES-lite).**
  1. **Analysis** — scan the log for committed transactions.
  2. **Redo** — replay every update's after-image in order (repeat history).
  3. **Undo** — roll back, in reverse, the updates of transactions that never committed.
  The demo uses **two separate processes** so the crash is a real process exit.
  *(See `benchmarks/recovery_demo.ps1` or `.test recovery`.)*

## 9. Extension Track — B: MVCC

- **Motivation.** Under 2PL, readers and writers block each other on hot rows. MVCC lets
  reads proceed against a consistent snapshot without locks, raising read throughput and
  reducing blocking under contention.
- **Design** (`src/txn/mvcc.*`). Each key owns a **version chain**
  `{begin_ts, end_ts, value, creator, committed}`. A transaction takes a **snapshot
  timestamp** at begin; a version is visible iff it is committed and
  `begin_ts ≤ snapshot < end_ts`. Reads never lock. Writers append an uncommitted
  version; commit stamps it with the commit timestamp and closes the prior version.
  Concurrent conflicting writers are rejected (**first-committer-wins**) to prevent lost
  updates.
- **Results.** Snapshot isolation holds (a reader keeps seeing its snapshot after a
  concurrent commit), and under contention MVCC blocks **0** readers where 2PL blocks
  **all** of them. See §10. *(See `.test mvcc`.)*

## 10. Benchmarks

**Experimental setup.** MinGW g++ 6.3.0, `-O2`, Windows 11, single database file with a
256-frame buffer pool. Reproduce with `.\benchmarks\bench.ps1` (writes
`benchmarks/results.txt`). Representative results:

**(1) Index scan vs sequential scan** — 50,000 rows, 500 point lookups:

| Access path | Total | Per lookup |
| ----------- | ----- | ---------- |
| IndexScan   | ~3 ms | ~0.006 ms  |
| SeqScan     | ~1014 ms | ~2.03 ms |
| **Speedup** | **~340×** | |

**(2) MVCC vs 2PL — reader blocking under contention** (1 writer holds a hot key):

| Readers | 2PL blocked | MVCC blocked |
| ------- | ----------- | ------------ |
| 100     | 100         | 0            |
| 1,000   | 1,000       | 0            |
| 10,000  | 10,000      | 0            |

**(3) Raw read throughput (uncontended)** — 200,000 reads: MVCC ≈ 1.9 ms vs
2PL lock+unlock ≈ 3.1 ms.

**Analysis.** The index turns an O(N) scan into an O(log N) traversal — a ~340× win that
justifies the optimizer's access-path choice. MVCC removes reader blocking entirely under
contention and even has lower per-read overhead than acquiring/releasing a shared lock,
confirming the Track-B hypothesis.

## 11. Limitations

- Types limited to `INT` and `VARCHAR(n)`; VARCHAR is fixed-width (space-padded).
- `WHERE` supports `AND`-conjunctions of `column <op> constant` only (no `OR`, no
  expressions/subqueries). Joins are inner, two-table, equi-join.
- B+Tree deletion is lazy (no node merge/rebalance); no secondary indexes.
- The catalog must fit in a single 4 KB page.
- Concurrency is demonstrated via a deterministic scheduler rather than OS threads (a
  consequence of the toolchain); MVCC garbage collection is not implemented.
- **Future work:** more types, richer predicates, multi-way joins + hash join,
  B+Tree rebalancing, secondary indexes, checkpointing, version GC.

## 12. How to Run

**Prerequisites:** MinGW `g++` on `PATH` (verify with `g++ --version`). PowerShell.

```powershell
# Build
.\build.ps1                      # produces minidb.exe

# Interactive SQL REPL
.\minidb.exe                     # then type SQL ending in ';', or .help

# End-to-end SQL demo (CREATE/INSERT/SELECT/WHERE/JOIN/DELETE/EXPLAIN)
.\run_demo.ps1

# Component self-tests (great for the viva)
.\minidb.exe .test storage       # buffer pool + heap + eviction
.\minidb.exe .test index         # B+Tree search/range/delete
.\minidb.exe .test txn           # 2PL + deadlock detection
.\minidb.exe .test recovery      # WAL redo/undo (in-process)
.\minidb.exe .test mvcc          # snapshot isolation + contention

# Benchmarks and the real two-process crash-recovery demo
.\benchmarks\bench.ps1
.\benchmarks\recovery_demo.ps1
```

Example SQL:

```sql
CREATE TABLE users (id INT, name VARCHAR(16), age INT, PRIMARY KEY (id));
INSERT INTO users VALUES (1, 'alice', 30);
EXPLAIN SELECT * FROM users WHERE id = 1;     -- chooses the index
SELECT name FROM users WHERE age > 25;
```

---

### Source layout

```
src/common      config, types (Value/RID), Status
src/storage     disk_manager, buffer_pool (CLOCK), heap_file
src/record      schema, tuple (de)serialization
src/index       bplus_tree
src/catalog     catalog (persisted to page 0)
src/sql         lexer, parser, ast
src/exec        operators (Volcano), executor (planner + run)
src/optimizer   cost/selectivity, access-path & join-order choice
src/txn         lock_manager (2PL), transaction, mvcc (Track B)
src/recovery    log_record, log_manager (WAL), recovery_manager (ARIES-lite)
src/main.cpp    REPL, self-tests, benchmarks
```
