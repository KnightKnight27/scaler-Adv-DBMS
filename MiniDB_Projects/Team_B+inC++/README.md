# MiniDB

A small but complete relational database engine, built from foundational components for the
Advanced DBMS capstone. It integrates a page-based storage engine, a B+ tree index, a SQL
query processor with a cost-based optimizer, transactions with MVCC + Two-Phase Locking, and
write-ahead-logging crash recovery — wired together behind a SQL REPL.

**Extension track: B — Concurrency (MVCC).**

---

## Team

> **Team name:** `Team_B+inC++`

| Full Name | Scaler Email | Roll Number |
| --------- | ------------ | ----------- |
| Ankur Kalita    | ankur.23bcs10185@sst.scaler.com    | 23BCS10185 |
| Manjari Rathore | manjari.23bcs10192@sst.scaler.com  | 23BCS10192 |
| Kushagra Sharma | kushagra.23bcs10165@sst.scaler.com | 23BCS10165 |
| Sandip Dey      | sandip.23bcs10114@sst.scaler.com   | 23BCS10114 |

---

## 1. Project Overview

**Problem.** Build a working relational database engine from scratch — not a wrapper over an
existing one — that demonstrates how the classic layers of a DBMS fit together: durable
storage, indexing, query processing, concurrency control, and recovery.

**Goals.**
- Correctness and clarity over feature count: implement exactly what each required component
  needs, with code every team member can explain.
- One coherent system, not five disconnected lab programs.
- A measurable extension that shows a real engineering trade-off.

**Chosen extension track — B (Concurrency / MVCC).** We implement Multi-Version Concurrency
Control alongside Strict Two-Phase Locking and benchmark them head-to-head: under write
contention, MVCC readers use a consistent snapshot and never block on writers, so they sustain
far higher read throughput than 2PL (where readers wait on writers' locks).

---

## 2. System Architecture

```
                          SQL text  ("SELECT name FROM emp WHERE id = 3;")
                                       │
        ┌──────────────────────────────▼──────────────────────────────┐
        │  QUERY LAYER                                                  │
        │   Lexer ─▶ Parser (recursive descent ─▶ AST)                  │
        │        ─▶ Optimizer (selectivity, scan choice, join order)    │
        │        ─▶ Executor (Volcano pull operators)                   │
        │           SeqScan · IndexScan · Filter · Project · NLJoin     │
        └───────────────┬───────────────────────────┬──────────────────┘
            reads/scans  │                           │  writes (INSERT/DELETE)
                         ▼                           ▼
        ┌────────────────────────┐      ┌────────────────────────────────┐
        │  INDEX                 │      │  TRANSACTIONS & RECOVERY         │
        │  B+ tree (key ▶ RowID) │      │  Database: BEGIN/COMMIT/ROLLBACK │
        │  leaf-linked range scan│      │  WAL (write-ahead log) ─▶ recover│
        └───────────┬────────────┘      │  MVCC + 2PL engine (Track B)     │
                    │ RowID              └───────────────┬──────────────────┘
                    ▼                                    │ apply
        ┌───────────────────────────────────────────────▼──────────────────┐
        │  STORAGE ENGINE                                                    │
        │  Catalog ─▶ HeapFile (slotted pages) ─▶ BufferPool (clock-sweep)   │
        │                                       ─▶ DiskManager ─▶ data file  │
        └───────────────────────────────────────────────────────────────────┘
```

**Major modules** (`src/`):

| Dir | Module | Role |
|-----|--------|------|
| `common/` | types, value, config | `PageID`, `RowID`, `Value`(=int\|text), `PAGE_SIZE` |
| `storage/` | disk_manager, page, buffer_pool, heap_file | durable, cached, slotted row store |
| `index/` | bplus_tree | primary-key index → `RowID`, range scans |
| `catalog/` | schema, catalog, database | table registry, row (de)serialization, session + transactions |
| `query/` | lexer, parser, ast, optimizer, executor | SQL → plan → results |
| `txn/` | transaction, transaction_manager | MVCC + 2PL concurrency engine |
| `recovery/` | wal | write-ahead log + crash recovery |

**Data flow.** A SQL string is tokenized, parsed to an AST, and planned into a tree of
pull-based operators. Reads flow down through the index/heap via the buffer pool; writes go
through the `Database` session, which logs each change to the WAL *before* applying it to the
heap and index, and groups changes into transactions that commit (durably) or roll back.

---

## 3. Storage Layer

- **Page format — slotted page (4 KB).** A header (`num_slots`, `free_space_end`) is followed
  by a slot directory growing left-to-right; tuple bytes grow right-to-left from the end. Free
  space is the gap between them. A slot stores `(offset, length)`; `length == 0` marks a
  deleted tuple. The indirection means a tuple's `RowID = (page, slot)` stays stable even if
  its bytes move within the page.
- **Heap file.** An unordered collection of slotted pages. `insert` first-fits a tuple and
  returns a `RowID`; `get`/`erase` work by `RowID`; `scan` walks every live tuple.
- **Buffer pool.** A fixed set of frames cache pages. Eviction uses **clock-sweep** (a usage
  counter per frame; the hand sweeps, decrementing until it finds a `0` to evict) — O(1) and
  scan-resistant. Frames carry a **pin count** (a page in use can't be evicted) and a **dirty**
  flag (modified pages are written back before reuse and on shutdown).
- **Disk manager.** The only component that touches the file; reads/writes whole pages by id
  (`offset = id × 4096`) and grows the file one page at a time.

Durability across a clean restart was verified two-process: write rows in one process, read
them back in another.

---

## 4. Indexing

- **B+ tree**, keyed by the integer primary key, mapping to a `RowID`.
- **Node structure.** Internal nodes hold only separator keys and child pointers (they route);
  **all `RowID`s live in the leaves**, and leaves are linked left-to-right.
- **Search path.** From the root, at each internal node advance while `key ≥ separator[i]`,
  descend, until a leaf; binary-search the leaf. Insert splits a full node and copies/pushes a
  separator up; range scans find the start leaf then walk the leaf chain.
- The index is **in-memory and rebuilt from the heap when a table opens** — it fully supports
  search/insert/delete and powers index scans; persisting nodes to disk pages is out of scope.

---

## 5. Query Execution

- **Parser.** A hand-written recursive-descent parser turns tokens into an AST
  (`CREATE/INSERT/SELECT/DELETE` + `BEGIN/COMMIT/ROLLBACK`). Expression precedence is encoded
  by the grammar: `OR` < `AND` < comparison.
- **Plan generation.** The planner builds a tree of operators from the AST, asking the
  optimizer which scan to use and how to order a join.
- **Operator execution — the Volcano (pull) model.** Every operator exposes `open()` then
  `next()`; a parent pulls rows from its child one at a time. Operators: `SeqScan`,
  `IndexScan`, `Filter`, `Project`, `NestedLoopJoin`. Column references resolve by name (with
  `table.col` qualification for joins).

Supported SQL: `CREATE TABLE`, `INSERT`, `SELECT` (with `WHERE`, `AND`/`OR`, two-table equi
`JOIN`), `DELETE`, and `BEGIN`/`COMMIT`/`ROLLBACK`.

---

## 6. Optimizer

- **Selectivity estimation.** Heuristics (equality 0.1, range 0.33, `AND` multiplies, `OR` by
  inclusion–exclusion), refined by the one exact fact we have — the primary key is unique, so
  `pk = const` matches exactly `1/N` rows.
- **Scan choice.** `cost(SeqScan) = N`; `cost(IndexScan) = matches + log₂N` (only when there's
  a sargable PK predicate). The cheaper wins — so `id = 3` uses the index while `age > 18` (no
  index) or a tiny table falls back to a sequential scan. A residual filter always sits above
  an index scan, so correctness never depends on the estimate.
- **Join ordering.** The nested-loop join materializes its inner side, so the optimizer makes
  the **smaller relation the inner** to minimize that memory.
- Every decision is printed as an `[opt] …` line so the choice is visible.

---

## 7. Transactions & Concurrency

Two complementary pieces:

- **Concurrency engine (`txn/`, Track B).** A thread-safe `TransactionManager` over a
  versioned store: **MVCC** (each write makes a new version `{value, xmin, xmax}`; reads see
  the version visible to their snapshot), **Strict 2PL** (exclusive locks for writes, shared
  locks for reads in 2PL mode; all released at commit/abort), and **deadlock detection** (a
  waits-for graph is DFS-checked on every blocked request; a cycle aborts the requester). A
  **mode switch** (`MVCC` vs `TWO_PL`) differs only in whether reads take a lock — that is what
  the benchmark contrasts.
- **SQL transactions (`Database`).** `BEGIN`/`COMMIT`/`ROLLBACK` (and auto-commit) provide
  **atomicity and durability** for the single-connection REPL via the WAL; `ROLLBACK` reverses
  a transaction's writes.

**Isolation guarantees.** Strict 2PL (the `TWO_PL` mode) is serializable. MVCC gives snapshot
isolation (readers never block writers); writes still take exclusive locks, so lost updates are
prevented, though snapshot isolation permits write-skew (true serializable-MVCC / SSI is future
work).

**Deadlock handling.** Waits-for cycle detection; the transaction whose request closes the
cycle aborts and releases its locks, unblocking the others.

---

## 8. Recovery

- **WAL design.** An append-only log of `BEGIN / INSERT / DELETE / COMMIT / ABORT` records.
  `INSERT` carries the new row (after-image, for REDO); `DELETE` carries the old row
  (before-image, for UNDO). The **write-ahead rule**: a change's record is flushed durable at
  `COMMIT`, before its heap pages are forced to disk — so a committed transaction is
  recoverable even if its pages never left the buffer pool.
- **Log records.** Binary, length-prefixed (`type · txid · pk · table · image`); a torn final
  record (crash mid-write) is detected by a short read and dropped.
- **Crash recovery procedure.** On restart, `recover()` reads the log, marks transactions with
  a `COMMIT` as committed, then **REDOes** every committed op (idempotently — insert-if-absent,
  delete-if-present) and **UNDOes** every loser op that reached the heap. This is an ARIES-lite
  redo/undo at row granularity (no per-page LSNs, CLRs, or checkpoints).

A `CRASH` command (hard exit, skipping the buffer-pool flush) demonstrates it; see §12.

---

## 9. Extension Track — B (MVCC)

- **Motivation.** Under read-heavy contention, 2PL readers block on writers' exclusive locks.
  MVCC lets readers use a consistent snapshot and proceed without locking, trading strict
  serializability (it gives snapshot isolation) for much higher read throughput.
- **Design.** Version chains per key (`xmin`/`xmax`) + a snapshot-visibility rule; writers take
  exclusive locks (so writes remain serialized and conflict-checked); readers in MVCC mode take
  no lock. The same engine runs in a `TWO_PL` mode for an apples-to-apples comparison.
- **Results.** See §10 — MVCC sustains several times the read throughput of 2PL under
  contention, and the gap widens as contention grows.

---

## 10. Benchmarks

**Setup.** `benchmarks/bench_mvcc_vs_2pl.cpp` runs an identical workload — N reader threads and
M writer threads contending on a few hot keys, each writer briefly holding its exclusive lock —
once in each mode, counting reads completed in a fixed time window. Run with `make bench`; raw
output is in `benchmarks/results/mvcc_vs_2pl.txt`.

**Results** (representative; absolute numbers are machine- and run-dependent):

| Workload | MVCC reads | 2PL reads | MVCC advantage |
|----------|-----------:|----------:|:--------------:|
| light (8 readers, 2 writers, 200 µs hold) | ~0.8 M | ~0.26 M | **~3×** |
| heavy (12 readers, 4 writers, 500 µs hold) | ~0.85 M | ~0.11 M | **~7×** |

**Analysis.** With more/longer write-holds, 2PL readers spend more time blocked on writers'
exclusive locks, so their throughput collapses; MVCC readers are unaffected because they read a
snapshot without locking. This is exactly the behaviour Track B asks to demonstrate: higher read
throughput and reduced blocking under contention.

---

## 11. Limitations

- **Types:** `INT` and `TEXT` only; no `NULL`; non-negative integer literals.
- **Queries:** two-table equi-joins only (nested-loop, inner materialized); no aggregation /
  `GROUP BY` / `ORDER BY` / `UPDATE`.
- **Index:** single primary-key B+ tree, in-memory (rebuilt on open); no secondary indexes.
- **Optimizer:** heuristic selectivity (no histograms); index scans only on the PK.
- **Catalog/schema is not persisted** — reopening a database means re-declaring `CREATE TABLE`
  (then data and recovery work); recovery is triggered explicitly via `RECOVER`.
- **Concurrency:** the MVCC/2PL engine is exercised by the benchmark; the SQL REPL is
  single-connection, so SQL transactions provide atomicity/durability rather than isolation
  between concurrent SQL sessions. MVCC mode is snapshot isolation, not serializable.
- **Recovery:** `flush()` is an OS flush, not `fsync` (durable across a process crash, not a
  power loss); no checkpointing, so the WAL grows unbounded and recovery replays it all.
- **Build:** static linking is used to sidestep a libstdc++ DLL issue on one MSYS2 toolchain.

Future work: persisted catalog + automatic startup recovery, on-disk B+ tree, secondary
indexes, predicate pushdown + N-way join enumeration, SSI for serializable MVCC, checkpoints.

---

## 12. How to Run

**Dependencies.** A C++17 compiler (`g++`) and `make`. POSIX threads (`-pthread`).

**Build.**
```sh
make            # builds ./minidb
# or, without make:
g++ -std=c++17 -pthread -O2 -Isrc $(find src -name '*.cpp') -o minidb -static
```

**Run** — the REPL reads SQL from stdin; data files live in the directory you pass (default `.`):
```sh
./minidb data <<'SQL'
CREATE TABLE emp (id INT PRIMARY KEY, name TEXT, age INT, dept_id INT);
CREATE TABLE dept (id INT PRIMARY KEY, dname TEXT);
INSERT INTO emp VALUES (1, 'Kartik', 20, 10);
INSERT INTO emp VALUES (2, 'Sandip', 30, 20);
INSERT INTO emp VALUES (3, 'Krishank', 25, 10);
INSERT INTO emp VALUES (4, 'Nitish', 17, 20);
INSERT INTO emp VALUES (5, 'Kp', 28, 10);
INSERT INTO emp VALUES (6, 'Arjun', 22, 20);
INSERT INTO dept VALUES (10, 'Engineering');
INSERT INTO dept VALUES (20, 'Sales');
SELECT name FROM emp WHERE id = 1;                                  -- optimizer picks the index
SELECT name FROM emp WHERE age > 25;                               -- non-PK predicate -> seq scan
SELECT emp.name, dept.dname FROM emp JOIN dept ON emp.dept_id = dept.id;
BEGIN;
INSERT INTO emp VALUES (7, 'Temp', 99, 10);
ROLLBACK;                                                           -- row 7 undone
SELECT id, name FROM emp WHERE id = 4;                             -- still consistent
SQL
```

**Crash-recovery demo** (two processes):
```sh
# 1) commit (1,2); start an uncommitted insert (3); crash
./minidb data <<'SQL'
CREATE TABLE acct (id INT PRIMARY KEY, bal INT);
BEGIN; INSERT INTO acct VALUES (1,100); INSERT INTO acct VALUES (2,200); COMMIT;
BEGIN; INSERT INTO acct VALUES (3,300);
CRASH;
SQL
# 2) recover: committed (1,2) survive, uncommitted (3) is gone
./minidb data <<'SQL'
CREATE TABLE acct (id INT PRIMARY KEY, bal INT);
RECOVER;
SELECT * FROM acct;
SQL
```

**Benchmark (Track B).**
```sh
make bench      # builds and runs MVCC vs 2PL; writes benchmarks/results/
```

See `docs/architecture.md` for a deeper architecture walkthrough and `docs/design_decisions.md`
for the engineering trade-offs behind each component.
