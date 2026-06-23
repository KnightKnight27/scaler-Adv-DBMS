# MiniDB — Team Orchestrators

**PR Title:** TEAM_Orchestrators

**Submitted by:** Tushar Srivastava

## Team Information

- **Team Name:** Orchestrators
- **PR Author:** Tushar Srivastava

### Team Members

**Tushar Srivastava**
- Roll Number: 10427
- Email: tushar.24bcs10427@sst.scaler.com

**Tirth Shah**
- Roll Number: 10347
- Email: tirth.24bcs10347@sst.scaler.com

**Vanditabyaa Dwivedi**
- Roll Number: 10505
- Email: vanditabyaa.24bcs10505@sst.scaler.com

> This repository contains the MiniDB baseline implementation and the documentation required for the advanced DBMS capstone project.

## 1. Project Overview

### Problem statement
Build a minimalist embedded relational database engine with a modular architecture, supporting SQL parsing, query execution, and a durable storage layer.

### Goals
- Implement a working SQL REPL for table creation, insertion, selection, and deletion.
- Provide a baseline heap-file storage engine with buffer pool support.
- Design the system for future Track C extensions around modern storage and LSM-based engines.
- Document architecture, storage design, query execution, and planned extensions.

### Chosen extension track
**Track C — Modern Storage.**
The current code implements the baseline heap storage stack; the extension track is focused on LSM-style storage and benchmark comparison.

## 2. System Architecture

### Architecture diagram
```
User REPL
   ↓
Database facade
   ↓
SQL Lexer / Parser → AST
   ↓
Query plan builder
   ↓
Execution engine (operators)
   ↓
StorageEngine interface
   ↓
HeapEngine / PageManager / BufferPool
```

### Major modules
- `apps/repl/main.cpp` — interactive command loop and result formatting
- `include/minidb/database.hpp` — top-level database facade and statement executor
- `include/minidb/sql/*` — lexer, parser, AST, and statement producers
- `include/minidb/exec/*` — physical operators, predicate evaluation, projection, scan
- `include/minidb/storage/*` — page manager, buffer pool, storage engine abstraction, heap engine
- `include/minidb/catalog.hpp` — catalog metadata, table schemas, and table lookup
- `include/minidb/types.hpp` — value types, tuples, and serialization helpers

### Data flow
1. User enters SQL through the REPL.
2. SQL is tokenized and parsed into an AST.
3. The database executor converts the AST into operators.
4. Operators execute against storage through `StorageEngine`.
5. Results are returned to the REPL and printed.

## 3. Storage Layer

### Page format
- Fixed page size: `kPageSize = 4096` bytes.
- Slotted page layout with a header storing `num_slots` and `free_ptr`.
- Slot directory entries contain `(offset, length)` for each record.
- Deleted slots are tombstoned with length `0`.

### Heap files
- `HeapEngine` stores table rows sequentially in pages.
- Each table has a list of allocated page ids.
- Rows are serialized into a compact binary format before storage.

### Buffer pool
- `BufferPool` is responsible for fetching pages into memory.
- It pins pages for access and unpins them after use.
- Dirty pages are written back to disk on flush.
- Eviction is managed through a simple LRU-style mechanism.

## 4. Indexing

### B+ Tree design
A real in-memory B+ Tree index is implemented in `storage/index.{hpp,cpp}`.
Indexes are created with `CREATE INDEX <name> ON <table>(<column>)`, maintained
automatically on INSERT/DELETE, and rebuilt by scanning the table when the
database is opened (durable data lives in the heap; the index is a fast lookup
structure reconstructed on startup).

### Node structure
- Order `M = 64`. Internal nodes store sorted separator keys and child pointers.
- Leaf nodes store sorted keys, each mapping to a vector of RIDs (so non-unique
  indexes work), plus a `next` pointer chaining leaves left-to-right.
- Inserts split overflowing leaves and propagate splits up, growing the tree
  height at the root.

### Search path
- `find(key)` descends from the root using separator keys, then binary-searches
  the leaf.
- `range(lo, hi)` descends to `lo`'s leaf and walks the leaf chain, giving
  ordered range scans.

## 5. Query Execution

### Parser
- Lexer tokenizes SQL into keywords, identifiers, literals, and punctuation.
- Recursive-descent parser creates `Statement` objects for CREATE, INSERT, SELECT, and DELETE.

### Query plan generation
- The cost-based `Planner` (`exec/planner.{hpp,cpp}`) translates a `SelectStmt`
  into a physical operator tree, choosing the access path and join algorithm
  (see §6).
- `SELECT` supports `WHERE`, `INNER JOIN ... ON a.x = b.y`, and `ORDER BY`,
  with qualified (`table.column`) references.

### Operator execution
- `SeqScanOp` streams rows from `StorageEngine::scan()`; `IndexScanOp` fetches
  rows by key through a B+ Tree.
- `FilterOp` evaluates the WHERE conjunction; `ProjectOp` emits selected columns.
- `NestedLoopJoinOp` and `HashJoinOp` implement inner equi-joins.
- `SortOp` materializes and orders rows for `ORDER BY`.
- Execution follows the Volcano model with `open()`, `next()`, and `close()`.

## 6. Optimizer

### Cost estimation
The planner estimates row counts from `TableStats` (populated by `ANALYZE`, and
counted lazily on first use otherwise). Costs are expressed in expected tuples
touched. `EXPLAIN <select>` prints the chosen plan with its estimates.

### Selectivity estimation
For an equality predicate `col = v` on an indexed column, selectivity is
`1 / distinct(col)`. The planner compares an index scan (`selectivity * rows`)
against a sequential scan (`rows`) and picks the cheaper access path.

### Join selection
For inner equi-joins the planner chooses between a hash join (`|L| + |R|`) and a
nested-loop join (`|L| * |R|`) by estimated cost — hash for larger inputs,
nested-loop when one side is tiny.

## 7. Transactions & Concurrency

### Transaction model
`BEGIN` / `COMMIT` / `ROLLBACK` provide single-threaded logical transactions;
standalone statements run in an implicit autocommit transaction. Each write
records WAL and undo information. COMMIT flushes the WAL (NO-FORCE); ROLLBACK
reverse-applies the undo list.

### Locking strategy
`LockManager` provides table-level latches. A transaction acquires an exclusive
latch on first write to a table and releases all latches at commit/rollback
(strict two-phase locking). With a single connection there is no contention, but
the mechanism is real.

### Isolation guarantees
Reads observe the transaction's own uncommitted writes (read-your-writes), and a
rolled-back transaction leaves no trace. With a single connection this is
effectively serial execution.

### Deadlock handling
Because execution is single-threaded and a transaction acquires its table
latches in a strict two-phase manner (all held until commit/rollback), no
lock-wait cycle can form, so deadlock is impossible by construction. The
`LockManager` is structured so that a future multi-threaded version could add
waits-for cycle detection without changing the transaction interface.

## 8. Recovery

### WAL design
An append-only write-ahead log (`<base>.wal`) is the source of truth for
durability. The buffer policy is NO-FORCE + STEAL: heap pages are not forced at
commit (durability comes from the log) and dirty uncommitted pages may be
stolen to disk (so recovery must be able to undo them). The log record is always
flushed before the corresponding page change can reach disk.

### Log records
Each record is `{ lsn, txn_id, type, table, rid, payload }` with
`type ∈ { Begin, Insert, Delete, Commit, Abort, Checkpoint }`. An `Insert`
record's payload is the after-image of the tuple (used for redo); a `Delete`
record's payload is the before-image (used to reinsert on undo). `Commit`
flushes the log and is the durable commit point.

### Crash recovery procedure
On startup `TransactionManager::recover` runs ARIES-lite recovery:
1. **Analysis** — scan the log; find committed transactions.
2. **Redo** — replay committed Insert/Delete records (idempotent by RID),
   registering pages and re-initializing reconstructed pages as needed.
3. **Undo** — reverse-apply records of transactions that never committed.

It then checkpoints and truncates the log. Covered by `tests/test_recovery.cpp`.

## 9. Extension Track

### Motivation
Track C focuses on modern storage engines and the evolution from traditional heap storage to LSM-based storage.

### Design
- Baseline heap engine implemented in this repository.
- Future extension to add `LsmEngine` with MemTable, SSTables, and compaction.
- Compare read/write performance against the heap baseline.

### Results
- Baseline behavior is validated with a working REPL and SQL execution.
- Full LSM extension results are planned for the next development phase.

## 10. Benchmarks

### Experimental setup
`minidb_bench [rows]` (default 10000) bulk-inserts N rows, runs K random point
lookups with a sequential scan and again after `CREATE INDEX` + `ANALYZE`, then
deletes the lower half. Built with the UCRT64 toolchain; times via
`std::chrono::steady_clock`.

### Results (sample run, N = 10000, K = 500)

| scenario                   | total (ms) | throughput        |
|----------------------------|-----------:|-------------------|
| bulk insert                |      242.7 | 41,199 rows/s     |
| point lookup (seq scan)    |     3171.9 | 6.3437 ms/op      |
| point lookup (index scan)  |       25.4 | 0.0508 ms/op      |
| delete (lower half)        |      195.7 | 25,552 rows/s     |

Index speedup on point lookups: **~125x**.

### Analysis
A sequential-scan point lookup is O(N) per query, while a B+ Tree lookup is
O(log N) plus the matching rows, so the index advantage grows with table size.
The cost-based planner switches to the index scan automatically once an index
exists and statistics show the predicate is selective.

## 11. Limitations

### Implemented (for context)
All six advanced features are working end-to-end and covered by tests: a real
B+ Tree index with `CREATE INDEX`, inner nested-loop/hash joins, a cost-based
optimizer (`ANALYZE` / `EXPLAIN`), `BEGIN` / `COMMIT` / `ROLLBACK` transactions,
write-ahead logging with ARIES-lite recovery, and a benchmark harness — plus
`ORDER BY` and a `.read` script command.

### Missing features
- No aggregates (`COUNT`/`SUM`/`AVG`) or `GROUP BY`, no subqueries.
- Joins are inner equi-joins only — no outer/cross joins, and the join grammar
  supports a single `INNER JOIN` per query.
- No `UPDATE` statement; no `NULL` values; `VARCHAR(n)` length is advisory.
- The B+ Tree is in-memory (rebuilt by scanning the table on open) rather than
  a page-backed, fully persistent index.

### Scalability limits
- Single-threaded: one connection, no concurrent transactions, so the
  `LockManager` never contends and there is no real parallelism.
- The buffer pool is fixed-size with simple LRU; scans materialize rows eagerly.
- Indexes and statistics live in memory, so very large tables are bounded by RAM
  and `ANALYZE` must be rerun after reopening for accurate selectivity.
- The WAL is replayed in full at startup (checkpoint truncates it), so recovery
  time grows with the size of the un-checkpointed log.

### Future improvements
- Page-backed B+ Tree nodes through the buffer pool for full index persistence.
- Multi-threaded execution with real 2PL contention and waits-for deadlock
  detection.
- Multi-way join ordering, aggregates/`GROUP BY`, and `UPDATE`.
- The Track C LSM storage engine (MemTable + SSTables + compaction) compared
  against the heap baseline (see §9).
- Incremental statistics maintenance and periodic checkpointing.

## 12. How to Run

### Dependencies
- C++14-compatible compiler
- CMake 3.20+
- Ninja or another build generator

### Build steps
```powershell
cd C:\Users\Tushar Srivastava\Desktop\MiniDB
mkdir build
cd build
cmake -G Ninja ..
ninja
```

### Run the REPL
```powershell
cd C:\Users\Tushar Srivastava\Desktop\MiniDB\build
.\minidb.exe ..\minidb
```

### Example commands
```sql
CREATE TABLE users (id INT, name VARCHAR(100));
INSERT INTO users VALUES (1, 'Alice');
INSERT INTO users VALUES (2, 'Bob');
SELECT id, name FROM users ORDER BY name;

CREATE INDEX ix_users_id ON users(id);
ANALYZE users;
EXPLAIN SELECT name FROM users WHERE id = 1;     -- shows IndexScan + cost

CREATE TABLE orders (oid INT, uid INT, amount INT);
INSERT INTO orders VALUES (10, 1, 100);
SELECT users.name, orders.amount
  FROM users INNER JOIN orders ON users.id = orders.uid;

BEGIN;
DELETE FROM users WHERE id = 2;
ROLLBACK;                                        -- the row is restored
```

### Build, test, and benchmark
```bash
./build.sh           # build minidb + minidb_bench (UCRT64 toolchain)
./build.sh test      # build and run the ctest suite
./build/minidb_bench 10000
```

### Demo scripts
Run a feature demo through the REPL:
```bash
printf '.read demos/03_join.sql\n.exit\n' | ./build/minidb demo
```
Scripts live in `demos/` (one per feature).

### Notes
- SQL statements must end with `;`.
- REPL commands: `.tables`, `.read <file>`, `.help`, `.exit`.
- Supported statements: CREATE TABLE, CREATE INDEX, INSERT, SELECT (WHERE,
  INNER JOIN, ORDER BY), DELETE, ANALYZE, EXPLAIN, BEGIN, COMMIT, ROLLBACK.

## Project Directory Structure

This repository follows the baseline MiniDB implementation. The core code lives in the root source tree under `apps/`, `include/`, and `src/`.
