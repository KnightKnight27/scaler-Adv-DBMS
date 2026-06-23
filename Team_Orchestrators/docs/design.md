# MiniDB End-to-End Design — Six Advanced Features

**Date:** 2026-06-23
**Team:** Orchestrators
**Status:** Implemented

## 1. Goal

Turn the MiniDB baseline (heap storage + lean SQL REPL) into a complete,
end-to-end relational engine by implementing the six features currently listed
as limitations:

1. B+ Tree index
2. Join processing
3. Cost-based query optimizer
4. Transactions / concurrency control
5. Write-ahead logging + crash recovery
6. Benchmark harness

**Depth target:** functional and educational — real, correct algorithms that
compile and run end-to-end through the REPL, demonstrable but not
industrial-grade.

**Confirmed scope decisions:**
- Transactions are single-threaded logical (BEGIN/COMMIT/ROLLBACK, autocommit
  otherwise). No real multithreaded contention.
- B+Tree is in-memory, rebuilt by scanning the table on open (real B+Tree
  algorithms; no page-backed nodes).
- SQL surface gains `ORDER BY` (SortOp) but **not** aggregates / GROUP BY.
- Verification = `tests/test_*.cpp` suite + `demos/*.sql` scripts + green
  `./build.sh test`.

## 2. Current State (baseline)

Working: slotted-page heap (`HeapEngine`, `PageManager`, `BufferPool`),
SQL lexer/parser for CREATE/INSERT/SELECT/DELETE, Volcano operators
(`SeqScanOp`, `FilterOp`, `ProjectOp`), `Catalog`.

A prior attempt left the tree **non-compiling**: `heap_engine.hpp` references
`BPlusTreeIndex` without including it; `database.cpp`'s statement switch does
not handle the `Begin/Commit/Rollback` AST kinds that were already added. There
are stub files for `BPlusTreeIndex` (a `std::map` wrapper), `WriteAheadLog`,
`LockManager`, `TransactionManager` that are not wired into `Database`.

Key existing types the design builds on:
- `Value` — tagged union (Int/Double/Varchar) with full ordering
  (`compare`, `operator<`, `operator==`). Usable directly as a B+Tree key.
- `RID { PageId page_id; uint16_t slot; }`.
- `StorageEngine` interface already declares `find`, `replay_insert`,
  `replay_delete` (added by the prior attempt).
- `Schema` — `index_of(name)`, `column(i)`, `size()`, `columns()`.
- Build: CMake globs `src/*.cpp` into `minidb_core`, builds `minidb` REPL, and
  auto-discovers `tests/test_*.cpp` for ctest. Toolchain is MSYS2 UCRT64
  (GCC 16 / CMake / Ninja) via `build.sh`.

## 3. Architecture After Changes

```
User REPL (.read scripts, BEGIN/COMMIT, EXPLAIN, ANALYZE, CREATE INDEX)
   |
Database facade  ── TransactionManager ── WriteAheadLog
   |                      |
SQL Lexer / Parser -> AST |
   |                      |
Planner (cost-based) -> physical Operator tree
   |
Execution engine: SeqScan | IndexScan | Filter | Project | Sort
                  | NestedLoopJoin | HashJoin
   |
StorageEngine interface
   |
HeapEngine + PageManager + BufferPool + BPlusTree indexes
```

Each subsystem is an isolated unit behind a clear interface so it can be tested
on its own.

## 4. Feature Designs

### 4.0 Build repair (Phase 0, prerequisite)

Before any feature work, get a clean green build of the baseline:
- Include `index.hpp` where needed (or remove the premature member until 4.1).
- Make `database.cpp` handle every `StatementKind` (transaction kinds can
  initially return a "not yet supported" message, replaced in 4.4).

A working fallback must exist at all times.

### 4.1 B+ Tree index

New class `BPlusTree` (replaces the `std::map` stub), in `storage/index.hpp` +
`index.cpp`:
- Configurable order `M` (default ~64). Internal nodes hold up to `M` keys +
  `M+1` child pointers; leaf nodes hold key→RID entries and a `next` pointer to
  the right sibling (linked list for range scans).
- `insert(key, rid)` with leaf split propagating up; root split grows height.
- `Optional<RID> find(key)` — descend to leaf, binary search.
- `range(low, high)` — descend to `low`'s leaf, walk `next` pointers collecting
  RIDs until `high`. Returns an iterator/vector of RIDs.
- `remove(key)` — locate leaf and erase the entry. (Simple erase without merge;
  underflow tolerated — acceptable for educational scope, documented.)
- Duplicate keys: a leaf entry stores a small vector of RIDs per key (so
  non-unique indexes work).

Catalog gains index metadata: `struct IndexMeta { name; TableId table; size_t
column; }`. `HeapEngine` owns `unordered_map<IndexId, BPlusTree>`, rebuilt on
open by scanning each indexed table, and updated on every `insert`/`remove`.

New SQL `CREATE INDEX <name> ON <table>(<column>);` — parser + AST
(`CreateIndexStmt`) + `Database::run_create_index`.

### 4.2 Join processing

Parser/AST: extend `SelectStmt` with an optional join:
`struct Join { std::string table; std::string left_col; std::string right_col; }`
parsed from `... FROM a INNER JOIN b ON a.x = b.y`. Column references in
SELECT/WHERE may be qualified (`a.x`). The joined output `Schema` prefixes each
column name with its table (`a.x`, `b.y`) to disambiguate.

Operators:
- `NestedLoopJoinOp(left, right, predicate)` — general; for each left tuple,
  rescan right, emit concatenations passing the join predicate.
- `HashJoinOp(left, right, left_key_idx, right_key_idx)` — build a hash table on
  the (smaller, build-side) right input keyed by join column, probe with left.
  Equi-join only.

`predicate.hpp`/operators handle concatenated tuples and qualified-name lookup.

### 4.3 Cost-based optimizer

New `Planner` unit (`exec/planner.hpp` + `.cpp`) that builds the physical
operator tree (logic currently inline in `database.cpp` moves here).

Statistics: `struct TableStats { size_t row_count; unordered_map<size_t,size_t>
distinct; }` per table. Populated by `ANALYZE <table>;` (full scan) and kept
roughly current on insert/delete. Stored in catalog.

Cost model (relative, unit = expected tuples touched):
- **Access path:** for `WHERE col = v` with an index on `col`, estimate
  selectivity `1 / distinct(col)`; index-scan cost ≈ `selectivity * row_count +
  log`. Seq-scan cost ≈ `row_count`. Choose the cheaper. Range predicates use a
  default range selectivity (e.g. 1/3).
- **Join algorithm:** hash-join cost ≈ `|L| + |R|`; nested-loop ≈ `|L| * |R|`.
  Pick hash for equi-joins unless an input is tiny.
- **Join order (3+ tables):** greedy — start from the smallest estimated
  cardinality, join in the next table that minimizes intermediate size.

`EXPLAIN <query>;` prints the chosen operator tree with estimated cardinality /
cost at each node, so the optimizer's decisions are visible and testable.

### 4.4 Transactions (single-threaded logical)

`TransactionManager` (expanded from stub) integrated into `Database`:
- `begin()` — assign a monotonically increasing `TxnId`, write a `BEGIN` WAL
  record, start an in-memory undo list.
- Each DML op, before applying, appends a WAL record **and** an undo entry
  (inverse op: an insert's undo is a delete of its RID; a delete's undo is an
  insert of the saved before-image).
- `commit()` — write `COMMIT` record, flush WAL, clear undo list.
- `rollback()` — reverse-apply the undo list, write `ABORT` record, discard.
- Autocommit: a statement outside an explicit `BEGIN` runs in its own implicit
  txn (begin → apply → commit).

`LockManager` retained as a table-level latch acquired/released per statement —
present for demonstration and future multithreading, no contention today.

`Database` routes INSERT/DELETE through the transaction manager so WAL + undo are
always recorded.

### 4.5 WAL + crash recovery (ARIES-lite)

`WriteAheadLog` (expanded from stub) — append-only binary log file
`<base>.wal`. Record format: `{ lsn, txn_id, type, table, rid, payload }` where
`type ∈ { Begin, Insert, Delete, Commit, Abort, Checkpoint }`. Insert payload =
after-image bytes; delete payload = before-image bytes.

Guarantees:
- **Write-ahead:** the log record is appended before the page mutation.
- **Durability:** `COMMIT` forces an `fsync`/flush of the log.

Buffer policy: **STEAL** (the buffer pool may flush an uncommitted txn's dirty
pages to disk on eviction). This is why a crash can leave uncommitted effects on
disk and recovery needs a real undo phase — matching the apply-immediately model
in 4.4. Recovery runs in the `Database` constructor before serving, in three
phases (ARIES-lite):
1. **Analysis** — scan the WAL once, collect the set of `txn_id`s that have a
   `Commit` record (the *winners*); all others are *losers*.
2. **Redo** — replay every `Insert`/`Delete` record of *winner* txns in LSN
   order via `replay_insert`/`replay_delete` (idempotent by RID), restoring any
   committed effect that hadn't been flushed.
3. **Undo** — for *loser* txns, walk their records in reverse and reverse-apply:
   an `Insert`'s undo deletes its RID; a `Delete`'s undo re-inserts its saved
   before-image at the same RID. This removes any uncommitted effect that *had*
   been flushed.
4. After successful recovery, write a `Checkpoint` and truncate the log.

This is the same inverse-op logic the live `rollback()` in 4.4 uses; the undo
phase is just rollback driven from the log instead of an in-memory list.

Crash-recovery test: apply committed + uncommitted work, **do not** cleanly
close (skip checkpoint/truncate), reopen a fresh `Database` on the same files,
assert committed rows are present and uncommitted rows are absent.

### 4.6 Benchmark harness

New executable `minidb_bench` (`apps/bench/main.cpp`, added to CMake). Uses
`std::chrono::steady_clock`. Scenarios over a configurable row count `N`
(default 100k):
- **Insert** throughput (rows/sec).
- **Point lookup**: seq-scan vs index-scan latency (avg over K random keys) —
  demonstrates the B+Tree + optimizer payoff.
- **Range scan**: index range vs full filter.
- **Delete** throughput.

Prints a formatted results table (scenario, rows, total ms, rows/sec, avg
latency). Documented in README §10.

### 4.7 ORDER BY

`SortOp` operator: materialize child tuples, sort by the `order_by` column
indices using `Value::compare`, emit in order. Parser already produces
`order_by`; planner adds `SortOp` at the top of the tree when non-empty.

## 5. REPL changes

- `.read <file>` — execute a `;`-terminated SQL script (for demos/tests).
- New statements routed: `CREATE INDEX`, `ANALYZE`, `EXPLAIN`, `BEGIN`,
  `COMMIT`, `ROLLBACK`.
- Existing `.tables`, `.exit` retained.

## 6. Testing strategy

`tests/test_util.hpp` — minimal `CHECK(cond)` / `CHECK_EQ(a,b)` macros that
print and set a failure flag; `main` returns non-zero on any failure (ctest
reads the exit code).

Test suites (each its own `tests/test_*.cpp`, auto-discovered by CMake):
- `test_bplustree` — insert/find/range/duplicate/split correctness.
- `test_join` — nested-loop and hash join produce identical, correct results.
- `test_optimizer` — EXPLAIN chooses index scan when selective, seq scan
  otherwise; hash vs nested-loop choice.
- `test_transactions` — commit persists, rollback reverts, autocommit.
- `test_recovery` — simulated crash leaves only committed data.
- `test_sql_e2e` — full CREATE/INSERT/SELECT/JOIN/ORDER BY through `Database`.

`demos/*.sql` — one script per feature, runnable via `.read`, included in README.

## 7. Phasing (implementation order)

0. Repair build → green baseline.
1. B+Tree index + CREATE INDEX (+ test).
2. ORDER BY / SortOp (+ test) — small, unblocks demos.
3. Joins (nested-loop, then hash) (+ test).
4. Planner + statistics + cost model + EXPLAIN/ANALYZE (+ test).
5. WAL + recovery (+ test).
6. Transactions on top of WAL (+ test).
7. Benchmark harness.
8. Demos, README rewrite, final green `./build.sh test`.

Each phase keeps the build green and is independently testable.

## 8. Non-goals (YAGNI)

On-disk B+Tree pages; multithreaded locking / real concurrency; LSM engine;
NULLs; subqueries; aggregates / GROUP BY; outer/cross joins (inner-equi focus,
nested-loop covers general inner predicates); query rewrite beyond join order
and access-path selection.
