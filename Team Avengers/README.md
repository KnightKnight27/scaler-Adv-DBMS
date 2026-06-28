# MiniDB — Team Avengers

A working relational database engine built from foundational components for the
Advanced DBMS capstone: page-based storage, a B+ tree index, SQL query
processing with a cost-based optimizer, transactions, and write-ahead crash
recovery — plus the **Concurrency (Track B)** extension: Multi-Version
Concurrency Control measured against a Two-Phase Locking baseline.

> **Extension track chosen: Track B — Concurrency (MVCC).**

---

## Team Members

| Name | Roll Number | Scaler Email |
|------|-------------|--------------|
| Shubham Shah | 24BCS10316 | shubham.24bcs10316@sst.scaler.com |
| Abhi Gandhi | 24BCS10397 | abhi.24bcs10397@sst.scaler.com |

---

## 1. Project Overview

**Problem statement.** Build a small but genuine relational database engine that
integrates the components studied across the labs — storage, indexing, query
processing, transactions, and recovery — into one coherent system, rather than a
pile of disconnected demos.

**Goals.**
- A page-based storage engine with a buffer pool (not just in-memory `std::map`).
- A real B+ tree primary index that the query planner actually uses.
- End-to-end SQL: `CREATE TABLE`, `INSERT`, `SELECT` (with `WHERE`/`JOIN`), `DELETE`.
- A **cost-based** optimizer that chooses between a table scan and an index scan.
- Transactions with serializable isolation, and crash recovery via WAL.

**Chosen extension track — Track B (Concurrency).** Replace 2PL with MVCC and
demonstrate higher read throughput / reduced blocking under contention. We
implement *both* schemes so the improvement is measured, not asserted.

---

## 2. System Architecture

Layered, bottom-up — each layer depends only on the ones beneath it:

```
            ┌─────────────────────────────────────────────┐
            │  main.cpp  — interactive SQL shell (REPL)     │
            ├─────────────────────────────────────────────┤
            │  database.hpp — execute(sql) engine façade    │
            ├───────────────┬───────────────┬──────────────┤
            │  sql/         │  optimizer/   │  txn/         │
            │  lexer,parser │  cost model,  │  MVCC + 2PL   │
            │  → AST        │  access path  │  (Track B)    │
            ├───────────────┴───────┬───────┴──────────────┤
            │  execution/  Volcano operators                │
            │  SeqScan IndexScan Join Filter Projection     │
            ├───────────────────────┬───────────────────────┤
            │  catalog/  schema +   │  recovery/  WAL +     │
            │  table registry       │  crash recovery       │
            ├───────────┬───────────┴───────────────────────┤
            │  index/   │  record/  tuples + slotted heap    │
            │  B+ tree  │                                     │
            ├───────────┴─────────────────────────────────┬─┤
            │  storage/  DiskManager + BufferPool (clock)  │ │
            └──────────────────────────────────────────────┘
```

**Data flow for a `SELECT`.** SQL string → `lexer` → tokens → `parser` → AST →
`optimizer` builds a Volcano operator tree (choosing scan vs index) → the engine
pulls tuples through the tree → results.

**Major modules:** `storage`, `index`, `record`, `catalog`, `sql`, `execution`,
`optimizer`, `txn`, `recovery` (each under `src/`, each with its own test).

---

## 3. Storage Layer

- **Page format.** Fixed 4 KiB pages, the unit of disk⇄memory transfer.
- **Heap files.** A table's rows live in a chain of **slotted pages**. Within a
  page, a slot directory grows forward from the header while tuple bytes grow
  backward from the end; free space is the gap between. A slot is
  `(offset, length)`; a deleted row is a **tombstone** (`length = 0`) so its
  **RID never moves** and the index can keep pointing at it.
- **Buffer pool.** Caches a bounded set of pages. A **page table** maps
  `page_id → frame`; **pin counts** protect in-use pages; the **clock-sweep
  (second-chance) replacer** chooses an unpinned victim. A dirty victim is
  written back before its frame is reused. All disk reads/writes go through the
  pool — it is the single source of truth for page contents.

Files: `src/storage/{disk_manager,buffer_pool}.hpp`, `src/record/{tuple,table_heap}.hpp`.

---

## 4. Indexing

- **B+ tree** mapping primary key (`int64`) → `RID`.
- **Node structure.** Internal nodes hold separator keys + child pointers; **all
  keys and values live in the leaves**, and leaves are **linked left-to-right**.
- **Search path.** Descend from the root following separators to a leaf — every
  lookup costs exactly the tree height, so lookup cost is uniform.
- **Range scans** (what justifies a B+ tree over a hash index): locate the start
  leaf, then walk the leaf chain — no re-descent.
- **Maintenance.** Insert splits a full node (leaf split *copies* the split key
  up, internal split *moves* it up); delete handles underflow by **borrowing**
  from a sibling, else **merging**. Validated against a `std::map` oracle over
  500 inserts + 250 deletes.

File: `src/index/bplus_tree.hpp`.

---

## 5. Query Execution

- **Parser.** Recursive-descent (one function per grammar rule), so operator
  precedence falls out of the call nesting: `a=1 OR b=2 AND c=3` parses as
  `a=1 OR (b=2 AND c=3)`.
- **Plan generation.** The optimizer turns the AST into a tree of operators.
- **Operators (Volcano / iterator model).** Each implements `open()` + `next()`;
  calling `next()` on the root recursively pulls one tuple from children, so
  tuples stream instead of materializing whole intermediate tables:
  - `SeqScanExecutor` — full heap scan, optional pushed-down filter.
  - `IndexScanExecutor` — reads only the B+ tree key range a predicate allows.
  - `NestedLoopJoinExecutor` — equi-join; inner side buffered once, rescanned.
  - `FilterExecutor` — post-join predicate (may reference either table).
  - `ProjectionExecutor` — narrows to the `SELECT` list.

Files: `src/sql/{lexer,parser,ast}.hpp`, `src/execution/{executor,operators}.hpp`.

---

## 6. Optimizer

Cost-based, measured in estimated tuples processed:

- **Selectivity estimation** (System-R style heuristics): equality `0.1`, range
  `0.33`; `AND` multiplies selectivities, `OR` adds (capped at 1).
- **Cost estimation.** `SeqScan` cost = N rows. `IndexScan` cost = tree height +
  matched slice. The planner picks the **cheaper** plan — genuinely cost-based,
  not a fixed rule.
- **Primary-key range extraction.** Walks the `AND`-chain of `WHERE`, narrowing
  `[lo, hi]` (`id > 5 AND id <= 10` → `[6, 10]`). `OR` or non-PK predicates make
  the index unusable → safe fallback to a sequential scan.
- **Join ordering.** For a two-table join the smaller relation becomes the inner
  (buffered) side of the nested loop.

Observed: `WHERE id = 100` → IndexScan (cost 25 < 200); `WHERE v = 100` (non-key)
→ SeqScan. File: `src/optimizer/optimizer.hpp`.

---

## 7. Transactions & Concurrency

Two interchangeable concurrency-control managers operating on row keys:

- **Strict 2PL (core requirement).** Reads take shared locks, writes take
  exclusive locks; all locks held until commit (serializable + recoverable).
  Lock compatibility: S/S compatible, anything-with-X conflicts.
- **MVCC (Track B extension).** Reads run against the snapshot captured at
  `begin()` and **never lock**. A version is visible to snapshot `S` iff
  `xmin ≤ S AND (xmax == 0 OR xmax > S)`. Writes take an exclusive lock and
  buffer a pending version; commit assigns a timestamp and appends to the
  version chain.
- **Isolation guarantees.** 2PL → serializable. MVCC → snapshot isolation +
  **first-updater-wins**: a commit aborts with `SerializationFailure` if a key
  it wrote received a newer committed version than its snapshot (prevents lost
  updates).
- **Deadlock handling.** Both managers build a waits-for graph and run DFS on a
  blocked write; a cycle aborts the **youngest** (highest-id) transaction.
- **Garbage collection.** MVCC `gc()` prunes versions older than the oldest live
  snapshot.

Files: `src/txn/{mvcc,two_pl}.hpp`.

---

## 8. Recovery

- **WAL design.** An append-only log of records: `BEGIN`, `UPDATE(redo image)`,
  `COMMIT`, `ABORT`, `CHECKPOINT`. The **write-ahead rule** is enforced by
  flushing the log on `COMMIT` *before* the commit returns and *before* the
  change becomes visible.
- **Log records.** `(type, txn_id, key, value)`, length-prefixed, binary.
- **Crash recovery procedure** (redo-of-winners, a simplified ARIES):
  1. **Analysis** — scan the log, collect committed transaction ids.
  2. **Redo** — replay `UPDATE`s of committed transactions, in log order.
  Updates of aborted/uncommitted transactions are never replayed — that is the
  undo, because dirty data is not forced to disk before commit (no-force /
  no-steal). Demonstrated: after a simulated crash, committed rows survive while
  aborted and in-flight transactions leave no trace.

File: `src/recovery/wal.hpp`.

---

## 9. Extension Track — Track B (Concurrency / MVCC)

- **Motivation.** Under 2PL a read takes a shared lock, which conflicts with a
  writer's exclusive lock — so readers block whenever a row is being written.
  In read-heavy OLTP that is the dominant cost. MVCC removes it: readers ride a
  snapshot and take no locks.
- **Design.** Per-key version chains with `xmin`/`xmax` commit timestamps;
  snapshot visibility check on read; first-updater-wins on commit; version GC.
- **Results.** See §10 — MVCC serves 100% of reads with zero blocking where the
  2PL baseline blocks 100% under the same write contention.

---

## 10. Benchmarks

**Setup.** `benchmarks/mvcc_vs_2pl.cpp`: 200 "hot" rows each held by an
uncommitted writer, then 200,000 reader transactions each read a hot row. We
count reads served vs blocked and time the workload. Apple clang 21, arm64,
`-O2`.

| Scheme | Reads served | Reads blocked | Blocked % | Time |
|--------|-------------:|--------------:|----------:|-----:|
| **MVCC** | 200,000 | 0 | **0.0%** | ~27 ms |
| **2PL**  | 0 | 200,000 | **100.0%** | ~34 ms |

**Analysis.** Under write contention every 2PL read blocks behind the writer's
exclusive lock; every MVCC read proceeds on its snapshot. This is the
read-throughput / reduced-blocking improvement Track B asks for, measured
directly. (Run: `./build/bench_mvcc`.)

---

## 11. Limitations

Honest scope boundaries (deliberate trade-offs under the project timeline):

- **In-memory B+ tree index.** The index is rebuilt in memory rather than paged
  to disk; only the heap pages are persistent. The page-based design is there
  (storage layer); paging the index is future work.
- **Catalog not persisted across sessions.** Page-level storage is durable (the
  disk manager persists pages; the WAL recovers committed transactions), but the
  catalog — table definitions and the table→heap-page mapping — lives in memory,
  so the interactive shell starts fresh each run and does not reload tables
  created in a previous session. Persisting the catalog in a system table is
  future work.
- **Transactional store vs query path.** The MVCC/2PL managers operate on a
  row-key store; they are not yet woven into the SQL executor's read path, so
  `SELECT` does not currently run under a snapshot. The concurrency control is
  fully implemented and benchmarked as its own subsystem.
- **Single-threaded concurrency model.** A blocked lock returns `LOCK_WAIT` and
  the caller retries (deterministic, demo-friendly) rather than using OS threads.
- **Nested-loop join only** (no hash/merge join); good for small inner relations.
- **Types limited to `INT` and `TEXT`**; first column is the `INT` primary key.
- **No secondary indexes; no aggregation/GROUP BY.**

---

## 12. How to Run

**Dependencies:** a C++17 compiler (clang or gcc). CMake optional.

```bash
cd "Team Avengers"

# Option A — CMake
cmake -S . -B build && cmake --build build
./build/minidb                 # interactive SQL shell
./build/bench_mvcc             # Track B benchmark
cd build && ctest --output-on-failure   # run all unit tests

# Option B — no CMake
./run_tests.sh                 # builds everything + runs all 8 test suites
clang++ -std=c++17 -O2 -Isrc src/main.cpp -o minidb && ./minidb
```

**Example session:**
```sql
CREATE TABLE users (id INT, name TEXT, age INT)
INSERT INTO users VALUES (1, 'alice', 30)
INSERT INTO users VALUES (2, 'bob', 25)
SELECT name, age FROM users WHERE age >= 30
SELECT * FROM users WHERE id = 1      -- planner uses the index
DELETE FROM users WHERE age < 27
.exit
```
For `SELECT`, the shell prints the optimizer's chosen plan as `-- plan:` lines.

---

## Note on AI-Assisted Development

AI tooling was used to assist development of this project. Consistent with the
course policy, every team member understands the design and implementation and
is prepared to explain and defend all code, design decisions, and trade-offs
during the viva. Each module has a corresponding test under `src/.../test_*.cpp`.
