# MiniDB — A Relational Database Engine from Scratch

> Advanced DBMS Capstone Project
> **Extension Track: B — Concurrency (MVCC)**

---

## Team Information

**Team Name:** `Team_alpha`

| Full Name | Scaler Email ID | Roll Number |
|-----------|-----------------|-------------|
| `Tanush`  | `tanush24bcs10348@sst.scaler.com` | `10348` |
| `Krushna` | `krushna24bcs10464@sst.scaler.com` | `10464` |
| `Shreyas` | `shreyas24bcs10401@sst.scaler.com` | `10401` |

*(Replace the placeholders above and in the PR description before submitting.)*

---

## 1. Project Overview

### Problem Statement
Modern relational databases hide an enormous amount of machinery behind a simple
`SELECT`: pages on disk, a buffer pool, B+ tree indexes, a SQL parser, a
cost-based optimizer, transactions with locking, and crash recovery. The goal of
this project is to build a small but **genuinely working** relational engine that
integrates all of these components, so that every layer can be understood,
demonstrated, and defended.

### Goals
- Implement the six required subsystems as one coherent engine, not isolated demos.
- Keep each component the simplest version that is still **correct** and
  demonstrates the real mechanism (slotted pages, true B+ tree splits, a Volcano
  operator tree, a cost model, 2PL with deadlock detection, WAL redo/undo).
- Favour clarity and explainability over feature count.

### Chosen Extension Track
**Track B — Concurrency (MVCC).** We add Multi-Version Concurrency Control with
snapshot isolation on top of the transaction infrastructure built for the core
2PL engine. This was chosen because it reuses the existing transaction/timestamp
machinery and produces a clean, measurable result: under write contention, MVCC
readers do not block on writers, whereas 2PL readers do.

---

## 2. System Architecture

### Major Modules

```
                         +---------------------------+
                         |        MiniDB (CLI)       |   REPL / script runner
                         +-------------+-------------+
                                       |
                         +-------------v-------------+
                         |         Executor          |   statement dispatch,
                         |  (auto-commit / explicit  |   txn context, 2PL locks
                         |        transactions)      |
                         +----+-----------+----------+
                              |           |
                   +----------v--+    +---v-----------------+
                   |   Parser    |    |     Optimizer       |  cost-based:
                   | (SQL -> AST)|    | selectivity, join   |  scan vs index,
                   +-------------+    | order, access path  |  join ordering
                                      +----+-----------+----+
                                           |           |
                                  +--------v---+   +---v-----------+
                                  | Operators  |   |   Catalog     |  tables,
                                  | (Volcano:  |   | schemas,      |  schemas,
                                  | scan/index |   | B+ indexes,   |  statistics
                                  | /filter/   |   | statistics    |
                                  | join/proj) |   +---+-------+----+
                                  +-----+------+       |       |
                                        |        +-----v--+ +--v---------+
                                        |        | B+Tree | |   Table    | heap file
                                        |        | index  | | (per-table)|
                                        |        +--------+ +--+------+--+
                                        |                      |      |
                                        |              +-------v-+ +--v-------+
                                        |              | Buffer  | | Disk     |
                                        |              | Pool    | | Manager  | raw pages
                                        |              | (LRU)   | | (heap)   |
                                        |              +----+----+ +----+-----+
                                        |                   |           |
                                        |              +----v-----------v---+
                                        |              |       Page         | 4KB slotted
                                        |              +--------------------+
                                        |
              +-------------------------v--------------------------+
              |   Transactions:  TransactionManager + LockManager  |  2PL, deadlock
              |   Recovery:      WAL  +  RecoveryManager (redo/undo)|  detection, WAL
              |   Extension:     MvccStore (snapshot isolation)     |  MVCC
              +----------------------------------------------------+
```

### Data Flow (a `SELECT` query)
1. **Parser** turns SQL text into an `Ast.Select`.
2. **Executor** acquires shared (read) locks under 2PL and hands the AST to the **Optimizer**.
3. **Optimizer** estimates selectivity from cached **Catalog** statistics, chooses
   an access path (sequential vs index scan) per table, orders joins, and builds a
   tree of **Operators**.
4. The operator tree pulls tuples via the **Table** → **BufferPool** → **Page** path,
   using the **B+ Tree** index for point lookups when chosen.
5. Rows stream up through filter/join/project operators to the user.

A write (`INSERT`/`DELETE`) additionally writes a **WAL** record before mutating any
page, registers an undo action, and updates indexes and statistics.

---

## 3. Storage Layer

### Page Format
Pages are fixed-size **4 KB slotted pages** (the design used by PostgreSQL/SQLite):

```
 byte 0          12                              freePtr        4096
 +--------+----------------+   ......            +----------------+
 | header | slot directory | -> grows right     | <- tuple data  |
 +--------+----------------+                     +----------------+
 header: pageId(4) | slotCount(4) | freePointer(4)
 slot  : offset(4) | length(4)        (length = -1 => deleted/tombstone)
```

Tuples are written from the end of the page backwards; the slot directory grows
forwards from the header. When they would meet, the page is full. Each tuple is
serialized with a 1-byte MVCC flag plus 16 bytes of begin/end timestamps, then
its column values (INT = 4 bytes; STRING = 4-byte length prefix + UTF-8 bytes).

### Heap Files
Each table is its own **heap file** (`data/<table>.heap`) — an unordered set of
pages. Giving every table a dedicated file means each page is always interpreted
with exactly one schema, which keeps (de)serialization unambiguous.

### Buffer Pool
Each table owns a **buffer pool** that caches its pages in memory:
- **LRU eviction** via an access-ordered map; the least-recently-used *unpinned*
  page is evicted, and flushed first if dirty.
- **Pinning** prevents in-use pages from being evicted.
- **Dirty tracking** ensures modified pages are written back on eviction / flush.
- **Statistics** (`hits`, `misses`, `evictions`) drive the buffer-pool benchmark.

---

## 4. Indexing

### B+ Tree Design
A classic **B+ tree** maps an integer key → `RID(pageId, slot)`:
- All values live in **leaf nodes**; internal nodes only route searches.
- **Leaves are linked**, so a range scan is one search plus a sequential walk.
- The tree is **balanced**: every root-to-leaf path is equal length → O(log n).
- Default order = 64 (max children per internal node).

### Node Structure
Each node holds a sorted `keys` list. Internal nodes also hold `children`; leaf
nodes hold parallel `values` (RIDs) and a `next` pointer to the following leaf.

### Search Path
`search(key)` descends from the root, at each internal node choosing the child
whose key range contains the search key, until it reaches a leaf, where it binary
-searches for the key. **Insert** splits overflowing leaves (copy-up) and internal
nodes (push-up), growing a new root when the old root splits. The primary-key
index is built automatically for any column declared `PRIMARY KEY`, and is used by
the optimizer for equality lookups.

---

## 5. Query Execution

### Parser
A hand-written tokenizer + recursive-descent **Parser** supports:
`CREATE TABLE`, `CREATE INDEX`, `INSERT`, `SELECT` (with `WHERE`, multi-table
`JOIN`, `AND`), `DELETE`, and `BEGIN`/`COMMIT`/`ABORT`. It produces a compact AST.

### Query Plan Generation
The **Optimizer** converts an `Ast.Select` into a tree of physical operators,
deciding access paths and join order (see §6).

### Operator Execution (Volcano model)
Every operator implements `open() / next() / close()`. Calling `next()` on the
root recursively pulls one row from children. Implemented operators:
- `SeqScan` — full heap-file scan (skips MVCC-deleted tuples).
- `IndexScan` — B+ tree point lookup → single `RID` → page read.
- `Filter` — applies residual `WHERE` predicates.
- `NestedLoopJoin` — joins two inputs on a predicate.
- `Project` — selects / reorders output columns.

Rows are maps of `table.column → value`, so joined rows carry columns from
multiple relations unambiguously.

---

## 6. Optimizer

### Cost Estimation
A simple cost model compares access paths:
- `cost(seq scan) ≈ rows`
- `cost(index scan) ≈ log2(rows) + matches`

### Selectivity Estimation
- Equality predicate: `sel = 1 / distinctValues(column)`
- Range predicate (`<`, `>`, ...): `sel ≈ 0.33`

Distinct-value and row counts come from **cached Catalog statistics** maintained
incrementally on every insert/delete (real systems keep cached stats rather than
scanning at plan time). Estimated output size = `rows × ∏ selectivities`.

### Access-Path & Join-Order Selection
- **Access path:** if a `WHERE` clause has an equality on an indexed integer
  column and the estimated index cost beats the scan cost, an `IndexScan` is
  chosen; otherwise a `SeqScan`. The decision and its numbers are printed under
  `.plan on` / `--plan`.
- **Join order:** tables are ordered by estimated size (smallest first) so the
  smallest relation drives the nested-loop join, minimizing tuples examined.

Example (`--plan`):
```
access[users]: INDEX SCAN on id=2 (sel=0.333, idxCost=2.6 < scanCost=3.0)
```

---

## 7. Transactions & Concurrency

### Locking Strategy
The core engine uses **Strict Two-Phase Locking (2PL)** with `SHARED` (read) and
`EXCLUSIVE` (write) modes, at table granularity. Reads take shared locks, writes
take exclusive locks, and **all locks are held until commit/abort** (the strict
property), which guarantees serializable, recoverable schedules.

### Isolation Guarantees
Strict 2PL provides **serializable** isolation for the core engine. The MVCC
extension (§9) provides **snapshot isolation** as an alternative.

### Deadlock Handling
The `LockManager` maintains a **wait-for graph** (which transaction waits on which).
Before a transaction blocks, a DFS checks whether the new wait edge would create a
**cycle**; if so the requesting transaction is chosen as the **victim**, aborted
with a `DeadlockException`, and rolled back (releasing its locks so others
proceed). Demonstrated by `test/DeadlockDemo.java`.

---

## 8. Recovery

### WAL Design
A **Write-Ahead Log** records every change *before* the corresponding page is
modified, and is flushed to disk on every append. A transaction is durably
committed once its `COMMIT` record is on the log. Records are human-readable lines:
```
<lsn>|<txnId>|<type>|<table>|<pageId>|<slot>|<base64-after>|<base64-before>
```

### Log Records
`BEGIN`, `COMMIT`, `ABORT`, `INSERT` (after-image), `DELETE` (before-image),
`UPDATE` (before+after), `CHECKPOINT`.

### Crash Recovery Procedure (ARIES-style)
1. **Analysis** — scan the log to classify transactions as *committed* or *loser*
   (started but not committed).
2. **Redo** — replay all logged data operations forward (repeating history) so the
   on-disk state reflects everything the log recorded.
3. **Undo** — roll back loser transactions backward using before-images.

Net effect: committed transactions survive a crash; uncommitted ones disappear
(**atomicity + durability**). Demonstrated by `benchmarks/recovery_test.sql`:
after deleting the heap file (simulated crash) and keeping the WAL, `.recover`
restores exactly the committed rows and discards the uncommitted insert.

---

## 9. Extension Track — MVCC (Track B)

### Motivation
Under 2PL, readers and writers block each other on the same row. MVCC removes that
contention by keeping **multiple versions** of each row, letting readers see a
consistent **snapshot** without locking.

### Design
`MvccStore` keeps, per key, a newest-first chain of **versions**, each stamped with
`beginTs` (creating txn) and `endTs` (superseding txn). A transaction reads as of
its start timestamp using the **visibility rule**:

> version `v` is visible to snapshot `Ts` iff `v` is committed (or our own),
> `v.beginTs ≤ Ts`, and `v.endTs > Ts`.

Reads are **lock-free** (immutable version chain + concurrent map). Writes create a
new version. At commit we enforce **first-committer-wins**: if any key in the
transaction's write set was committed by a concurrent transaction after this one
started, we abort with a `WriteConflict`.

### Results
- **Snapshot isolation** (`test/MvccDemo.java`): a reader holding an old snapshot
  keeps seeing `v1` while a concurrent committed writer produces `v2`; both
  versions coexist and the reader never blocks.
- **Write-write conflict**: the second concurrent committer to the same key is
  correctly aborted.
- **Reduced blocking** (benchmark 3): under a contended hot row, MVCC readers wait
  **~5x less** per read than 2PL readers.

---

## 10. Benchmarks

Measured with `test/Benchmark.java` (OpenJDK 21). Representative run
(`benchmarks/results.txt`):

### Benchmark 1 — Index Scan vs Sequential Scan (point lookup)
| rows | seq scan (µs/query) | index scan (µs/query) | speedup |
|------|--------------------|-----------------------|---------|
| 2,000 | ~1,970 | ~340 | ~6x |
| 10,000 | ~2,650 | ~94 | ~28x |
| 40,000 | ~16,600 | ~69 | ~240x |

Index point lookup stays roughly constant (O(log n) tree descent) while the
sequential scan grows linearly — so the optimizer's index choice matters more as
data grows.

### Benchmark 2 — Buffer Pool Hit Rate
Repeated scans over a hot table reach a **100% hit rate** (0 misses) after pages
are cached — repeated reads are served from memory, not disk.

### Benchmark 3 — MVCC vs 2PL Reader Blocking Under Contention
| scheme | avg read wait (µs) |
|--------|--------------------|
| 2PL | ~32 |
| MVCC | ~6 |

On a single hot row that a writer holds for ~50µs at a time, MVCC readers wait
**~5x less** because they read a snapshot instead of waiting for the exclusive
lock. *(Absolute numbers vary by machine; the ratio is the point.)*

### Experimental Setup
Single machine, OpenJDK 21, in-process threads for the concurrency benchmark.
Numbers are illustrative of asymptotic behaviour rather than absolute performance.

---

## 11. Limitations

- **In-memory B+ tree:** indexes are rebuilt from the heap file on startup rather
  than persisted as index pages. Delete does not merge underflowing nodes (lookups
  remain correct; the tree is just less compact after many deletes).
- **Table-granularity locking:** the 2PL demo locks whole tables, not individual
  rows. Sufficient to show locking, deadlock detection, and isolation, but coarser
  than row-level locking.
- **Tombstone deletes:** deleted slots are marked, not compacted, so space is not
  reclaimed within a page.
- **MVCC store is separate:** the MVCC extension is implemented as a focused
  in-memory store alongside the row engine, to keep the visibility logic clear and
  defensible rather than entangling it with the on-disk page format. There is no
  background version garbage collection.
- **SQL subset:** no aggregation/GROUP BY, ORDER BY, UPDATE statement, or nested
  queries; two column types (INT, STRING).
- **Single-process:** no networking, no distributed features.

### Future Improvements
Persisted index pages with node merging; row-level locking with lock escalation;
in-page compaction; MVCC integrated into the heap with a vacuum process;
aggregation and sorting operators; a richer type system.

---

## 12. How to Run

### Dependencies
- **JDK 17+** (developed and tested on OpenJDK 21). No external libraries, no build
  tools — plain `javac`/`java`.

### Build
```bash
cd MiniDB
javac -d out $(find src -name "*.java")
```

### Run the interactive shell
```bash
java -cp out minidb.MiniDB                 # data stored under ./data
java -cp out minidb.MiniDB --plan          # also print query plans
java -cp out minidb.MiniDB --dir=mydata    # choose a data directory
```
REPL dot-commands: `.plan on|off`, `.stats`, `.recover`, `.tables`, `.exit`.

### Run a SQL script
```bash
java -cp out minidb.MiniDB --plan benchmarks/demo.sql
```

### Reproduce the demos
```bash
# End-to-end SQL (create/insert/select/where/join/index-scan/delete)
java -cp out minidb.MiniDB --plan benchmarks/demo.sql

# Crash recovery (committed survive, uncommitted undone)
java -cp out minidb.MiniDB --dir=data_crash benchmarks/recovery_test.sql
rm -f data_crash/accounts.heap          # simulate a crash (keep the WAL)
printf '.recover\nSELECT * FROM accounts;\n.exit\n' | \
  java -cp out minidb.MiniDB --dir=data_crash

# MVCC snapshot isolation + write conflict
javac -cp out -d out test/MvccDemo.java && java -cp out MvccDemo

# Deadlock detection
javac -cp out -d out test/DeadlockDemo.java && java -cp out DeadlockDemo

# Benchmarks
javac -cp out -d out test/Benchmark.java && java -cp out Benchmark
```

### Example session
```sql
CREATE TABLE users (id INT PRIMARY KEY, name STRING, age INT);
INSERT INTO users VALUES (1, 'Alice', 30);
INSERT INTO users VALUES (2, 'Bob', 25);
SELECT * FROM users WHERE id = 2;     -- uses the B+ tree index
SELECT name FROM users WHERE age > 28; -- sequential scan + filter
BEGIN;
DELETE FROM users WHERE id = 1;
COMMIT;
```

---

## Source Layout
```
src/minidb/
  common/Types.java          schema, RID, tuple, value types
  storage/Page.java          4KB slotted page
  storage/DiskManager.java   raw page I/O on a heap file
  storage/BufferPool.java    LRU page cache
  storage/Table.java         heap-file table (WAL-logged mutations)
  index/BPlusTree.java       B+ tree index
  sql/Catalog.java           tables, schemas, indexes, statistics
  sql/Ast.java               AST node types
  sql/Parser.java            tokenizer + recursive-descent parser
  exec/Operator.java         Volcano operators
  exec/Optimizer.java        cost-based optimizer
  exec/Executor.java         statement execution + txn context
  txn/Transaction.java       transaction state
  txn/LockManager.java       2PL + deadlock detection
  txn/TransactionManager.java txn lifecycle
  recovery/WAL.java          write-ahead log
  recovery/RecoveryManager.java  redo/undo crash recovery
  mvcc/MvccStore.java        MVCC extension (snapshot isolation)
  MiniDB.java                engine + CLI/REPL
benchmarks/  demo.sql, recovery_test.sql, results.txt
test/        MvccDemo.java, DeadlockDemo.java, Benchmark.java
docs/        architecture notes
```
