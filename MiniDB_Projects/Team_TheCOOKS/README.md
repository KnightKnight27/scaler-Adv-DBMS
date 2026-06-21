# WALterDB

A working, single-process **relational database engine** built from foundational
components for the Advanced DBMS capstone. It implements page-based storage, a
buffer pool, a B+tree index, SQL execution, a cost-based optimizer, transactions
with strict two-phase locking, and write-ahead-log crash recovery — plus
**Extension Track C: an LSM-tree storage engine** benchmarked against the classic
heap-file + B+tree engine.

Built entirely on the C++20 standard library and a build system (CMake), with
Catch2 for tests. The architecture *is* the project.

---

## Team

**Team Name:** `TEAM_TheCOOKS`

| Full Name | Scaler Email | Roll Number |
|---|---|---|
| Bibek Jyoti Charah | bibek.24bcs10112@sst.scaler.com | 24bcs10112 |
| Pranav Gupta | pranav.24bcs10237@sst.scaler.com | 24bcs10237 |
| Jinesh Gandhi | jinesh.24bcs10072@sst.scaler.com | 24bcs10072 |
| Siddhant Prasad | siddhant.24bcs10255@sst.scaler.com | 24bcs10255 |

---

## 1. Project Overview

- **Problem statement.** Design and build a relational database engine from
  scratch that integrates storage, indexing, query processing, transactions, and
  recovery into one coherent system.
- **Goals.** Correctness and architectural clarity, with every design decision
  explainable and every trade-off deliberate (see
  [`docs/DESIGN_NOTES.md`](docs/DESIGN_NOTES.md)).
- **Chosen extension track: C — Modern Storage (LSM-tree).** A second storage
  engine (MemTable + SSTables + compaction) sharing the same interface as the
  classic engine, with an A/B benchmark of write throughput, read latency, and
  storage amplification.

## 2. System Architecture

```
                 ┌──────────────────────┐
                 │   CLI / REPL          │  src/cli
                 └──────────┬───────────┘
                            │ SQL text
                 ┌──────────▼───────────┐
                 │ Parser (lexer→AST)    │  src/parser
                 └──────────┬───────────┘
                            │ AST
                 ┌──────────▼───────────┐
                 │ Planner / Optimizer   │  src/exec/planner.cpp
                 │ scan choice·join order │◄── Catalog (schemas, row stats)
                 └──────────┬───────────┘
                            │ physical operator tree
                 ┌──────────▼───────────┐
                 │ Volcano Executor      │  src/exec
                 │ SeqScan·IndexScan·     │
                 │ Filter·Project·NLJoin  │
                 └─────┬──────────┬──────┘
        locks/WAL      │          │  reads/writes
        ┌──────────────▼──┐   ┌───▼──────────────┐
        │ TransactionMgr   │   │ Catalog / Table   │  src/catalog
        │ LockManager(2PL) │   │ heap + PK B+tree  │
        │ WAL + Recovery   │   └───┬──────────────┘
        └──────────────┬──┘       │
                       │      ┌────▼─────────────────────────────┐
                       │      │ StorageEngine interface (KV)       │  src/engine
                       │      ├──────────────────┬────────────────┤
                       │      │ HeapBTreeEngine   │  LSMEngine      │  src/lsm
                       │      │ heap + B+tree     │ MemTable/SSTable│  (Track C)
                       │      └────────┬─────────┴────────┬───────┘
                       │               │                   │
                  ┌────▼───────────────▼──┐         (own SSTable files
                  │   Buffer Pool (LRU-K)  │  src/buffer   + WAL)
                  └────────┬──────────────┘
                  ┌────────▼──────────────┐
                  │  Disk Manager (pages)  │  src/storage
                  └───────────────────────┘
```

The keystone of the design is the `StorageEngine` key-value interface
(`src/engine/storage_engine.h`): both the classic heap+B+tree engine and the
Track-C LSM engine implement it, so the Track-C benchmark is a one-line engine
swap through a shared seam. The relational layer maps a row to a KV pair
(`key = table_id ‖ primary_key`, `value = serialized tuple`).

**Data flow.** SQL → tokens → AST → (binding + cost-based planning) → physical
operator tree → demand-driven `next()` pulls rows through the tree, reading the
heap/index via the buffer pool, under two-phase-locking, with modifications
written to the WAL before pages reach disk.

**Module map** (`src/`): `common` · `storage` · `buffer` · `index` · `catalog` ·
`parser` · `exec` · `txn` · `recovery` · `lsm` · `engine` · `cli`.

## 3. Storage Layer

- **Page format — slotted pages** (`storage/slotted_page.*`). Fixed 4 KB pages: a
  16-byte header (page LSN, next-page pointer, slot count, free-space pointer), a
  slot array growing forward from the header, and variable-length tuples growing
  backward from the end of the page. A delete tombstones its slot (offset 0) and
  the slot id stays stable, so outstanding record ids remain valid.
- **Heap files** (`storage/heap_file.*`). A table's rows form a singly linked
  list of slotted pages addressed by `RID = (page_id, slot)`. Inserts append to
  the tail page (allocating a new page when it fills); a move-only cursor scans
  live records across the chain.
- **Buffer pool** (`buffer/buffer_pool.*`). Page table + pin counts + dirty flags
  + **LRU-K (K=2)** replacement: a page accessed fewer than K times is given
  infinite backward-distance, so one-shot scans are evicted before genuinely hot
  pages. The current (dirty) version of a page is flushed on eviction.
- **Disk manager** (`storage/disk_manager.*`). One database file, one global page
  space, `pread`/`pwrite`/`fsync`; page N lives at byte offset `N × 4096`.

## 4. Indexing

- **B+tree** (`index/bplus_tree.*`) mapping order-preserving key bytes → RID,
  serving as each table's primary-key index.
- **Node structure.** Page-backed, fixed fanout with a bounded key length (64 B):
  a header (type, key count, next-leaf pointer) followed by fixed-stride key
  slots; leaves carry a parallel RID array and a `next_leaf` pointer forming a
  leaf chain for ordered range scans, internals carry a parallel child-pointer
  array.
- **Search path.** Descend from the root, at each internal node selecting the
  child whose key range covers the search key, down to a leaf, then scan the leaf.
  Inserts split bottom-up and grow a new root when the root itself splits. A
  stable meta page records the current root id, so the tree survives root splits
  and reopen.

## 5. Query Execution

- **Parser** (`parser/`). A hand-rolled lexer (keywords resolved
  case-insensitively in the parser) feeds a recursive-descent parser that uses
  precedence climbing for expressions. Grammar: `CREATE TABLE`, `INSERT`
  (multi-row), `SELECT` (qualified columns, `[INNER] JOIN … ON`, `WHERE`),
  `DELETE`, `BEGIN/COMMIT/ROLLBACK`, `EXPLAIN`. A syntax error is returned as a
  message.
- **Plan generation** (`exec/planner.cpp`). The AST is bound (column references
  resolved, ambiguity checked) and lowered into a physical operator tree.
- **Operator execution** (`exec/operators.*`). The Volcano / iterator model:
  `SeqScan`, `IndexScan`, `Filter`, `Projection`, `NestedLoopJoin` each implement
  `open()/next()/close()`; one row flows through the whole tree per root `next()`.
  Predicates evaluate with SQL three-valued logic.

## 6. Optimizer

A cost-based optimizer (`exec/planner.cpp`):

- **Scan choice.** For a single-table query whose `WHERE` has an equality on the
  primary-key column, the planner emits an **IndexScan** (B+tree point lookup);
  otherwise a **SeqScan**. The choice is visible in `EXPLAIN`.
- **Selectivity & cost estimation.** A primary-key equality estimates one row; a
  sequential scan costs ≈ heap pages (`rows / rows-per-page`), an index point
  lookup ≈ tree height + 1. Row-count statistics come from the catalog.
- **Join ordering.** Greedy: start from the smallest relation, then repeatedly add
  the smallest relation that an unapplied `ON` predicate connects to the joined
  set, applying each predicate as soon as both its tables are present.

```
EXPLAIN SELECT * FROM users WHERE id = 2;     EXPLAIN SELECT * FROM users WHERE name = 'bob';
-> Project: id, name, age                     -> Project: id, name, age
  -> Filter: (id = 2)                           -> Filter: (name = 'bob')
    -> IndexScan(users USING pk, id = 2)          -> SeqScan(users) [rows~3, cost~1 pages]
```

## 7. Transactions & Concurrency

- **Locking strategy** (`txn/lock_manager.*`). Shared/exclusive locks under
  **strict two-phase locking**: locks are acquired on demand and all released
  together at commit/abort. Granularity is table-level — an exclusive table lock
  also serves as the physical write latch, so the write path needs no page
  latches; the lock manager itself supports finer (row) resources. Physical
  structures are protected by short-lived latches (the buffer-pool latch,
  catalog/WAL mutexes), distinct from the long-lived logical locks.
- **Isolation.** Serializable for the locked items under strict 2PL.
- **Deadlock handling.** A wait-for graph: when a transaction would block and
  close a cycle it is chosen as the victim, its statement reports an abort, and it
  is rolled back from its undo log. Demonstrated with two concurrent conflicting
  transactions (`tests/test_concurrency.cpp`).

## 8. Recovery

- **WAL design** (`recovery/wal_manager.*`). An append-only, self-framing log of
  logical records, fsynced at commit (so a commit is durable through the log even
  while data pages are still buffered). The write-ahead rule is enforced by the
  buffer pool fsyncing the log before writing any dirty page.
- **Log records.** `Begin`, `Insert(table, row)`, `Delete(table, row)`, `Commit`,
  `Abort` — row images keyed by primary key, which makes replay idempotent.
- **Crash recovery procedure** (`recovery/recovery_manager.*`), ARIES-lite:
  **Analysis** (a transaction is committed iff it has a Commit record) → **Redo**
  (replay committed operations forward via idempotent upsert / delete-by-key) →
  **Undo** (reverse-replay the rest). The log is replayed in full on open and
  truncated at clean shutdown.
- **Demonstrated** (`scripts/crash_test.sh`): commit some rows, start an
  uncommitted transaction, then exit with no clean checkpoint; on reopen the
  committed rows survive and the uncommitted ones are rolled back.

## 9. Extension Track — C: LSM-tree Storage

- **Motivation.** Contrast the write / read / space trade-offs of a
  log-structured engine against the classic page-oriented engine through one
  shared interface.
- **Design** (`src/lsm/`). A write appends to a **WAL** then a sorted in-memory
  **MemTable**; when the MemTable fills it is flushed, in key order, to an
  immutable **SSTable** (sorted data + a **sparse index** + a **bloom filter** +
  min/max keys + footer). A read checks the MemTable, then SSTables
  newest→oldest, with the bloom filter and min/max keys letting most lookups skip
  a table entirely; the first hit wins and a tombstone means deleted. **Size-
  tiered compaction** k-way merges SSTables (newest value per key wins, tombstones
  dropped) to bound read amplification and reclaim space. `HeapBTreeEngine`
  exposes the classic primitives through the same interface for the A/B test.
- **Results.** The LSM engine leads on write throughput (~2.7×) and storage
  amplification (1.11× vs 2.37×); bloom filters keep its point reads fast in the
  in-memory regime. Full analysis in
  [`docs/BENCHMARK_REPORT.md`](docs/BENCHMARK_REPORT.md).

## 10. Benchmarks

Driver: `bench/benchmark.cpp` (built as `walterdb_bench`) runs one workload
against both engines through the `StorageEngine` interface. Representative run
(N = 100k rows, M = 20k point lookups):

| engine | load ops/s | point p50 (µs) | point p99 (µs) | scan rows/s | write-amp | storage-amp |
|---|---:|---:|---:|---:|---:|---:|
| HeapBTree | 360k | 8.3 | 23.6 | 2.5M | 4.81× | 2.37× |
| LSM (pre-compaction) | 990k | 1.0 | 1.9 | 3.3M | 2.23× | 1.11× |
| LSM (post-compaction) | 990k | 1.0 | 1.9 | 3.2M | 3.34× | 1.11× |

Setup, methodology, and per-metric analysis are in
[`docs/BENCHMARK_REPORT.md`](docs/BENCHMARK_REPORT.md).

## 11. How to Run

**Dependencies:** a C++20 compiler (g++ ≥ 10 / clang ≥ 10), CMake ≥ 3.16. Catch2
is vendored under `third_party/`.

```sh
# Build
cmake -S . -B build
cmake --build build -j

# Run the unit tests (one Catch2 file per module)
./build/walterdb_tests                 # or: ctest --test-dir build

# Interactive shell  (creates demo.wdb / demo.catalog / demo.wal)
./build/walterdb demo
#   walterdb> CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR);
#   walterdb> INSERT INTO users VALUES (1,'alice');
#   walterdb> SELECT * FROM users WHERE id = 1;
#   walterdb> EXPLAIN SELECT * FROM users WHERE id = 1;
#   walterdb> .help

# Scripted demos
bash scripts/demo.sh          # full feature tour
bash scripts/crash_test.sh    # crash recovery: committed survives, uncommitted rolls back

# Track-C benchmark
./build/walterdb_bench 100000 20000   # N rows, M point-lookups -> benchmark_results.csv
```

The design rationale, the trade-offs behind each subsystem, and what is
prioritized over what are documented in
[`docs/DESIGN_NOTES.md`](docs/DESIGN_NOTES.md).
