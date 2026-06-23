# MiniDB — Design Specification

**Date:** 2026-06-23
**Track:** C — Modern Storage (LSM-tree)
**Language:** C++20 (GCC 16, MSYS2 UCRT64)
**Build:** CMake + Ninja
**Strategy:** Interface-first vertical slice, then deepen each layer in dependency order.

---

## 1. Goal & Constraints

Build a working single-process, embedded relational database (MiniDB) implementing
the required core (storage, B+ tree indexing, SQL execution, cost-based optimizer,
transactions with 2PL, WAL recovery) plus the **LSM-tree storage extension (Track C)**,
benchmarked against the B+ tree / heap-file baseline.

Constraints:
- **Timeline:** tight (~2–4 weeks). Favor a continuously-demoable vertical slice.
- **SQL surface:** lean. Types `INT`, `VARCHAR`, `DOUBLE`. Statements:
  `CREATE TABLE`, `INSERT`, `SELECT` (WHERE, single equi-JOIN, ORDER BY, aggregates
  COUNT/SUM/MIN/MAX/AVG), `DELETE`, `BEGIN`/`COMMIT`/`ROLLBACK`.
- **Rubric weighting:** Core 40%, Extension 20%, Benchmarks 15%, Code Quality 15%,
  Demo/Report 10%. Implication: correct, complete, well-structured *core* first.

## 2. Architecture (layers, top → bottom)

```
CLI / REPL
SQL Frontend     Tokenizer -> Parser -> AST
Planner/Optimizer  AST -> logical plan -> physical plan (selectivity, scan choice, join order)
Execution Engine   Volcano operators: SeqScan, IndexScan, Filter, Project, NestedLoopJoin,
                   Aggregate, Sort, Insert, Delete
Txn Manager | Lock Manager | Catalog
Access Methods   Index interface -> B+ Tree
StorageEngine (interface)
   - HeapEngine: PageManager + BufferPool + slotted heap files   (baseline)
   - LsmEngine:  MemTable + SSTables + compaction                (Track C)
WAL / Recovery   LogManager, ARIES-lite (redo committed, undo uncommitted)
Disk (files)
```

The **critical seam** is `StorageEngine`: a record-oriented interface
(`insert`, `remove`, `get`, `scan`, `flush`). Both `HeapEngine` and `LsmEngine`
implement it; the executor and transaction layers depend only on the interface.
The Track C benchmark = run the same workload, swap the engine.

## 3. Component Interfaces (stable seams)

- **StorageEngine**: `RID insert(TableId, Tuple)`, `bool remove(RID)`,
  `optional<Tuple> get(RID)`, `unique_ptr<RecordIterator> scan(TableId)`, `flush()`.
- **HeapEngine internals**: `PageManager` (4 KB pages, allocate/read/write),
  `BufferPool` (frame table, LRU eviction, pin/unpin, dirty bits), slotted-page heap files.
- **Index**: `insert(Key, RID)`, `erase(Key)`, `optional<RID> find(Key)`,
  `KeyIterator range(lo, hi)`. Implemented by B+ Tree.
- **Executor operator**: Volcano model — `open()`, `optional<Tuple> next()`, `close()`.
  Plans are trees of operators; IndexScan vs SeqScan is a pure planner choice.
- **TransactionManager / LockManager**: `begin/commit/abort`; shared/exclusive locks
  per RID; waits-for graph for deadlock detection (victim = youngest txn); strict 2PL.
- **LogManager**: append-only WAL, LSN-stamped; `recover()` redo+undo (ARIES-lite).
- **Catalog**: table name -> schema (columns, types, PK); persisted to a system file.

## 4. Deliberate Simplifications (tight timeline)

- Lock granularity starts **table-level**, refined to **row-level** later.
- WAL covers the **heap engine** path. The LSM engine gets durability via its own
  write-ahead MemTable log + SSTable flush — called out as a design point in the viva.
- No query rewrite/optimizer beyond selectivity estimation, scan selection, and
  join-order selection for the single supported join.
- Single database, fixed page size (4 KB), no NULLs in v1.

## 5. Build Order (Approach 1 — vertical slice, then deepen)

1. **Slice (week 1):** interfaces + simplest impls. End-to-end CREATE/INSERT/SELECT…WHERE
   through HeapEngine with table-level locks, no recovery. Always demoable.
2. **B+ Tree** primary index + IndexScan operator.
3. **Joins + Aggregates + cost-based optimizer** (selectivity, scan choice, join order).
4. **Transactions:** strict 2PL, row locks, deadlock detection; concurrency demo.
5. **Recovery:** WAL + crash/recovery; durability demo.
6. **Track C:** LsmEngine (MemTable/SSTable/compaction); benchmark vs HeapEngine.
7. **Benchmarks + report + demo polish.**

Maps to course milestones M1–M5; LSM + benchmarks land in M5.

## 6. Testing & Quality

- Dependency-free unit-test harness (assert macros) wired to CTest; one test target
  per component (page manager, buffer pool, heap engine, B+ tree, parser, executor,
  lock manager, recovery).
- Each interface is independently mockable/testable.
- Deterministic on-disk format with versioned headers.

## 7. Repository Layout

```
MiniDB/
  CMakeLists.txt
  build.ps1 / build.sh        # set UCRT64 PATH, configure + build + test
  include/minidb/             # public headers (one per component)
  src/                        # implementations mirroring include tree
  apps/repl/                  # CLI entry point
  tests/                      # CTest-registered unit tests
  bench/                      # benchmark drivers (Track C)
  docs/                       # this spec, README, benchmark report
```
