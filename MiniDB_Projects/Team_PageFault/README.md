# MiniDB

A small but complete relational database engine, built from foundational
components for the Advanced DBMS capstone. It integrates a page-based storage
engine, a B+ tree index, a SQL query processor with a cost-based optimizer,
transactions with WAL crash recovery, and a switchable MVCC / 2PL concurrency
engine — all wired together behind a SQL REPL.

**Extension track: B — Concurrency (MVCC).**

---

## Team

> **Team name:** `Team_PageFault`

| Full Name | SCALER ID | Email |
| --------- | --------- | ----- |
| Rudray Mehra | 24BCS10425 | rudray.24bcs10425@sst.scaler.com |

---

## 1. Project Overview

**Problem.** Build a working relational database engine from scratch — not a
wrapper over an existing one — that demonstrates how the classic layers of a
DBMS fit together: durable storage, indexing, query processing, concurrency
control, and recovery.

**Goals.**
- Correctness and clarity over feature count: implement exactly what each
  required component needs, with code every team member can explain.
- One coherent system, not five disconnected lab programs.
- A measurable extension that shows a real engineering trade-off.

**Chosen extension track — B (Concurrency / MVCC).** We implement
Multi-Version Concurrency Control alongside strict Two-Phase Locking and
benchmark them head-to-head: under write contention, MVCC readers use a
consistent snapshot and never block on writers, so they sustain far higher read
throughput than 2PL (where readers wait on writers' locks). See §9–§10.

---

## 2. System Architecture

```
                 SQL text  ("SELECT name FROM emp WHERE id = 3;")
                                   │
   ┌───────────────────────────────▼───────────────────────────────┐
   │  QUERY LAYER                                                    │
   │    Lexer ─▶ Parser (recursive descent ─▶ AST)                   │
   │          ─▶ Optimizer (selectivity, scan choice, join order)    │
   │          ─▶ Executor (Volcano pull operators)                   │
   │             SeqScan · IndexScan · Filter · Project · NLJoin     │
   └───────────────┬───────────────────────────────┬────────────────┘
       reads/scans  │                               │  writes
                    ▼                               ▼
   ┌────────────────────────┐        ┌──────────────────────────────────┐
   │  INDEX                  │        │  TRANSACTIONS & RECOVERY           │
   │  B+ tree (key ▶ RowId)  │        │  Database: BEGIN/COMMIT/ROLLBACK   │
   │  leaf-linked ranges     │        │  WAL ─▶ recover() (REDO/UNDO)      │
   └───────────┬─────────────┘        └───────────────┬────────────────────┘
               │ RowId                                │ apply
               ▼                                      ▼
   ┌─────────────────────────────────────────────────────────────────────┐
   │  STORAGE ENGINE                                                       │
   │  Catalog ─▶ HeapFile (slotted pages) ─▶ BufferPool (clock-sweep)      │
   │                                       ─▶ DiskManager ─▶ <db>.dat      │
   └───────────────────────────────────────────────────────────────────────┘

   CONCURRENCY ENGINE (Track B): MVCC + 2PL TransactionManager (src/txn),
   demonstrated by the benchmark and the concurrency demo.
```

Major modules live under `src/` (see [docs/architecture.md](docs/architecture.md)
for the full table and data-flow walkthrough): `common/`, `storage/`, `index/`,
`catalog/`, `query/`, `txn/`, `recovery/`.

---

## 3. Storage Layer

- **Page format — slotted page (4 KB).** A header (`num_slots`, `free_ptr`) is
  followed by a slot directory growing left-to-right; tuple bytes grow
  right-to-left from the end of the page. Free space is the gap between them. A
  slot stores `(offset, length)`; `length == 0` marks a deleted tuple. The
  indirection means a tuple's `RowId = (page, slot)` stays stable.
  (`src/storage/page.hpp`)
- **Heap file.** An unordered collection of slotted pages. `insert` first-fits a
  tuple and returns a `RowId`; `get`/`erase` work by `RowId`; `scan` walks every
  live tuple. (`src/storage/heap_file.*`)
- **Buffer pool.** A fixed set of frames cache pages; eviction uses
  **clock-sweep** (a per-frame reference counter; a hand sweeps decrementing
  until it finds a `0` to evict). Frames carry a **pin count** (a page in use
  can't be evicted) and a **dirty** flag (modified pages are written back before
  reuse and on shutdown). (`src/storage/buffer_pool.*`)
- **Disk manager.** The only component that touches the file; reads/writes whole
  pages by id (`offset = id × 4096`) and grows the file one page at a time.
  (`src/storage/disk_manager.*`)

Durability across a clean restart is verified: rows inserted in one run are read
back in a later run.

---

## 4. Indexing

- **B+ tree**, keyed by the integer primary key, mapping to a `RowId`.
  (`src/index/bplus_tree.*`)
- **Node structure.** Internal nodes hold only separator keys and child pointers
  (they route); **all `RowId`s live in the leaves**, and leaves are linked
  left-to-right.
- **Search path.** From the root, at each internal node advance while
  `key ≥ separator[i]`, then descend, until a leaf; binary-search the leaf.
  Insert splits a full node and copies/pushes a separator up; range scans find
  the start leaf then walk the leaf chain.
- The index is **in-memory and rebuilt from the heap when a table opens**; it
  powers index scans during query execution.

---

## 5. Query Execution

- **Parser.** A hand-written recursive-descent parser turns tokens into an AST
  (`CREATE / INSERT / SELECT / DELETE` + `BEGIN / COMMIT / ROLLBACK`). Expression
  precedence is encoded by the grammar: `OR` < `AND` < comparison.
  (`src/query/lexer.*`, `parser.*`, `ast.hpp`)
- **Plan generation.** The executor builds a tree of operators from the AST,
  asking the optimizer which scan to use and how to order a join.
- **Operator execution — the Volcano (pull) model.** Every operator exposes
  `open()` then `next()`; a parent pulls rows from its child one at a time.
  Operators: `SeqScan`, `IndexScan`, `Filter`, `Project`, `NestedLoopJoin`, and
  `CountStar`. Column references resolve by name, with `table.col` qualification
  for joins. (`src/query/executor.cpp`)

Supported SQL: `CREATE TABLE`, `INSERT`, `SELECT` (with `WHERE`, `AND`/`OR`,
two-table equi-`JOIN`, `COUNT(*)`), `DELETE`, and `BEGIN`/`COMMIT`/`ROLLBACK`.

---

## 6. Optimizer

- **Selectivity estimation.** Heuristics (equality `0.1`, range `0.33`, `AND`
  multiplies, `OR` by inclusion–exclusion), refined by the one exact fact we
  have — the primary key is unique, so `pk = const` matches exactly `1/N` rows.
- **Scan choice.** `cost(SeqScan) = N`; `cost(IndexScan) = matches + log₂N` (only
  when there is a sargable PK predicate). The cheaper wins — so `id = 3` uses the
  index while `dept > 1` (no index) or a tiny table falls back to a sequential
  scan. A residual filter always sits above an index scan, so correctness never
  depends on the estimate.
- **Join ordering.** The nested-loop join materialises its inner side, so the
  optimizer makes the **smaller relation the inner** to minimise that memory.
- Every decision is printed as an `[opt] …` line so the choice is visible during
  the demo. (`src/query/optimizer.*`)

---

## 7. Transactions & Concurrency

Two complementary pieces:

- **SQL transactions (`src/catalog/database.*`).** `BEGIN`/`COMMIT`/`ROLLBACK`
  (and auto-commit) give **atomicity and durability** for the single-connection
  REPL via the WAL. A transaction buffers inverse operations so `ROLLBACK`
  reverses its writes; `COMMIT` flushes the log record, making the transaction
  durable.
- **Concurrency engine (`src/txn/`, Track B).** A thread-safe
  `TransactionManager` over a versioned store implements **MVCC** (each commit
  appends a new version `{commit_ts, value}`; reads see the version visible to
  their begin-time snapshot) and **strict 2PL** (exclusive locks for writes,
  shared locks for reads in 2PL mode; all released at commit/abort), with
  **deadlock detection** (a waits-for graph is DFS-checked on every blocked
  request; a cycle aborts the requester). A **mode switch** (`MVCC` vs `TWO_PL`)
  differs only in whether reads take a lock — that is what the benchmark
  contrasts.

**Isolation guarantees.** Strict 2PL (the `TWO_PL` mode) is serializable. MVCC
gives snapshot isolation: readers never block writers, and lost updates are
prevented by **first-committer-wins** — at commit, a transaction that wrote a
key which another transaction committed after its snapshot is aborted with a
`TxnConflict` (the caller retries). Snapshot isolation still permits write-skew
(true serializable MVCC / SSI is future work).

**Deadlock handling.** Waits-for cycle detection; the transaction whose request
closes the cycle aborts and releases its locks, unblocking the others.

---

## 8. Recovery

- **WAL design.** An append-only log of `BEGIN / INSERT / DELETE / COMMIT /
  ABORT` records. `INSERT` carries the new row (after-image, for REDO); `DELETE`
  carries the old row (before-image, for UNDO). The **write-ahead rule**: a
  change's record is flushed durable before its heap pages are forced to disk —
  so a committed transaction is recoverable even if its pages never left the
  buffer pool. (`src/recovery/wal.*`)
- **Log records.** Binary, length-prefixed (`total · type · txid · pk · table ·
  image`); a torn final record (crash mid-write) is detected by a short read and
  dropped.
- **Crash recovery procedure.** On restart, `recover()` reads the log, marks
  transactions that have a `COMMIT` as committed, **REDOes** every committed op
  (idempotently — insert-if-absent / delete-if-present) and **UNDOes** every
  loser op that reached the heap. This is an ARIES-lite redo/undo at row
  granularity (no per-page LSNs, CLRs, or checkpoints).
  (`src/catalog/database.cpp::recover`)

The REPL's `CRASH` command (a hard exit that skips the buffer-pool flush)
demonstrates it; see §12.

---

## 9. Extension Track — B (MVCC)

**Motivation.** Strict 2PL is correct (serializable) but readers and writers
block each other, which kills read throughput on contended, read-heavy
workloads. MVCC lets readers see a consistent snapshot without taking locks, so
they never wait on writers.

**Design.** The `TransactionManager` keeps, per key, a list of versions
`{commit_ts, value}` in ascending timestamp order. A transaction records its
snapshot (the commit clock at `begin`) and:
- **MVCC read:** returns the newest version with `commit_ts ≤ snapshot` — no
  lock taken.
- **2PL read:** takes a shared lock (blocks if a writer holds the exclusive
  lock).
- **Write (both modes):** takes an exclusive lock and buffers the new value;
  `commit` validates first-committer-wins (in MVCC), then stamps all buffered
  writes with a fresh timestamp and appends versions.

Blocked requests add edges to a **waits-for graph**; a DFS on every block
detects cycles and aborts the requester as the deadlock victim.

**Results.** See §10 — on a contended workload MVCC sustains **~3–4.5× the read
throughput of 2PL**, with readers never blocking or deadlocking.

---

## 10. Benchmarks

**Experimental setup.** `benchmarks/bench_mvcc_vs_2pl.cpp`: 8 reader threads + 4
writer threads contend on 64 shared keys for a 1-second window in each mode.
Each transaction touches 4 keys; writer transactions hold their locks across a
short unit of simulated work before committing. Built with `-O2 -pthread`.

**Representative results** (full output + column legend in
[benchmarks/results/mvcc_vs_2pl.txt](benchmarks/results/mvcc_vs_2pl.txt)):

| mode | reads/sec | writes | deadlocks | reader-blocks | write-conflicts |
|------|-----------|--------|-----------|---------------|-----------------|
| 2PL  | ~215k     | ~10k   | ~1100     | ~250          | 0               |
| MVCC | ~700k–980k| ~12k   | **0**     | **0**         | ~2800           |

**MVCC read-throughput speedup: ~3–4.5× (varies run to run).**

**Analysis.** Under 2PL, readers serialize behind writers' exclusive locks on
hot keys and frequently form waits-for cycles (deadlocks), some of which abort
reader transactions. Under MVCC, readers are fully decoupled — they read their
snapshot and never block or deadlock (reader-blocks = 0, deadlocks = 0). Writers
are validated at commit by first-committer-wins, so lost updates are impossible;
the cost is some write-conflict retries under contention, extra versions per key
(space), and the weaker write-skew-permitting isolation level.

---

## 11. Limitations

- **Missing features:** no `UPDATE` (modelled as delete + insert), no aggregates
  beyond `COUNT(*)`, no `ORDER BY`/`GROUP BY`, single integer primary key only,
  two-table joins only, no secondary indexes.
- **Index not persisted:** the B+ tree is rebuilt from the heap on open
  (O(N) startup per table); index pages are not written to disk.
- **Concurrency engine is separate from the on-disk store:** the MVCC/2PL
  manager is an in-memory key/value engine used for the Track-B demonstration;
  the SQL path is single-connection (serial) with WAL durability.
- **Recovery is full-log scan:** no fuzzy checkpoints; the whole WAL is replayed
  on startup.
- **MVCC = snapshot isolation:** permits write-skew; not full serializability.
- **Scalability:** in-memory index and a text catalog rewritten on each DDL
  bound the practical table/row counts; fine for a teaching engine.

**Future improvements:** persisted B+ tree with page-level recovery, ARIES
checkpoints, SSI for serializable MVCC, `UPDATE` and richer SQL, secondary
indexes, and wiring MVCC into the on-disk row store.

---

## 12. How to Run

**Dependencies.** A C++17 compiler (`g++` or `clang++`) and `make`. No external
libraries (standard library + pthreads only).

**Build.**
```sh
make           # builds bin/minidb (REPL) and bin/bench (benchmark)
```

**Run the SQL REPL.**
```sh
./bin/minidb mydb        # data files: mydb.dat / mydb.cat / mydb.wal
```
Statements end with `;`. `EXIT` quits cleanly; `CRASH` simulates a crash.

**Example session.**
```sql
CREATE TABLE emp (id INT, name TEXT, dept INT);
CREATE TABLE dept (id INT, dname TEXT);
INSERT INTO emp VALUES (1,'alice',1);
INSERT INTO emp VALUES (2,'bob',2);
INSERT INTO dept VALUES (1,'eng');
INSERT INTO dept VALUES (2,'sales');
SELECT name FROM emp WHERE id = 3;                 -- uses the PK index
SELECT id, name FROM emp WHERE dept > 1;           -- sequential scan
SELECT emp.name, dept.dname FROM emp JOIN dept ON emp.dept = dept.id;
SELECT COUNT(*) FROM emp;
DELETE FROM emp WHERE id = 2;
```

**Crash-recovery demo.**
```sh
# Run 1: commit two rows, start an uncommitted insert, then crash.
printf 'CREATE TABLE acct (id INT, bal INT);\nBEGIN;\nINSERT INTO acct VALUES (1,100);\nINSERT INTO acct VALUES (2,200);\nCOMMIT;\nBEGIN;\nINSERT INTO acct VALUES (3,999);\nCRASH\n' | ./bin/minidb crash

# Run 2: reopen — recovery REDOes the committed rows (1,2) and drops the loser (3).
printf 'SELECT id, bal FROM acct;\nEXIT\n' | ./bin/minidb crash
```

**Concurrency benchmark (Track B).**
```sh
make bench     # or: ./bin/bench
```

**Clean.**
```sh
make clean
```
