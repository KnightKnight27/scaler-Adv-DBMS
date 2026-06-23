# MiniDB - Team_Cash

A small relational database engine built from scratch in C++17 (standard
library only). It runs a SQL shell over a real storage engine: 4 KB pages, a
buffer pool, a B+ tree index, a Volcano executor, a cost-based optimizer,
two-phase locking, write-ahead recovery, and an MVCC extension.

## 1. Project Overview

- **Problem.** Understand how a relational database actually works by building
  one, rather than treating it as a black box.
- **Goals.** Parse SQL, store rows on disk in pages, index them with a B+ tree,
  choose a sensible plan per query, run transactions with serializable
  isolation, and recover committed work after a crash, with every design choice
  simple enough to explain.
- **Extension track.** Track B - Concurrency (MVCC).

## 2. Team

| Name | Roll | Scaler email |
|------|------|--------------|
| Praveen Kumar (lead) | 24BCS10048 | praveen.24bcs10048@sst.scaler.com |
| Tanishq Singh | 24BCS10303 | tanishq.24bcs10303@sst.scaler.com |
| Saswata Das | 24BCS10248 | saswata.24bcs10248@sst.scaler.com |
| Tanishq Jain | 24BCS10039 | tanishq.24bcs10039@sst.scaler.com |

## 3. System Architecture

```
   SQL text
      |
   parser     lexer + recursive-descent parser -> AST
      |
   optimizer  AST -> plan (index scan vs seq scan, join order)
      |
   executor   Volcano iterator operators pull rows up the tree
      |
   catalog    per-table schema + heap file + B+ tree index
     /   \
  btree   storage   (heap file -> buffer pool -> disk manager -> 4 KB pages)

   txn / recovery / mvcc   transactions, durability, and the MVCC extension
```

Data flows down as a query is planned and back up as rows are produced.
`engine` ties the layers together; `main` is the shell.

## 4. Storage Layer

- **Disk manager** reads and writes whole 4 KB pages to one file per table.
- **Slotted page**: a small header, a slot directory growing from the front, and
  length-tagged tuple bytes packed from the back.
- **Buffer pool** caches pages with least-recently-used eviction and writes
  dirty pages back on eviction (hit/miss/eviction counters included).
- **Heap file** stores a table's rows across pages and scans them back.

## 5. Indexing

- A **B+ tree** maps each primary key to the RID (page, slot) of its row.
- Supports search, insert with node splits, range scan over linked leaves, and
  delete.
- It is held in memory and rebuilt by scanning the heap when the database opens,
  so it is never stale.

## 6. Query Execution

- The **parser** supports `CREATE TABLE`, `INSERT`, `SELECT` (with `WHERE`,
  `AND`, comparison operators, and a two-table `JOIN ... ON`), and `DELETE`.
- The **executor** uses the Volcano model: `SeqScan`, `IndexScan`, `Filter`,
  `Project`, and `NestedLoopJoin` are pull-based iterators that pass rows up.

## 7. Optimizer

Cost-based, with two decisions:

- **Index scan vs sequential scan.** An equality on an indexed primary key
  matches at most one row, so the optimizer uses a B+ tree lookup; otherwise it
  reads the table sequentially.
- **Join order.** For a two-table join the smaller table (by row count) becomes
  the inner, buffered relation of the nested-loop join.

`.explain <SELECT ...>` in the shell prints the chosen plan.

## 8. Transactions and Concurrency

Strict two-phase locking (2PL) with shared and exclusive locks per key. A
request that conflicts with a current holder is recorded as an edge in a
waits-for graph; a cycle is a deadlock, and the youngest transaction is aborted
to break it. Strict 2PL holds all locks until commit/abort, giving serializable
isolation. See `make demos` (deadlock_demo).

## 9. Recovery

A write-ahead log records every change (BEGIN, UPDATE with before/after images,
COMMIT) before it touches the data. After a crash, recovery replays the log:
redo the updates of committed transactions, undo the rest using their
before-images. See `make demos` (recovery_demo).

## 10. Extension Track (Track B - MVCC)

Each key keeps a chain of versions tagged with the creating transaction id. A
reader uses a snapshot and sees the newest committed version within it; a writer
appends a new version. Readers never block writers and vice versa. The benchmark
shows that under write contention 2PL blocks 100% of reads while MVCC blocks 0%.

## 11. Benchmarks

`make bench` runs both:

- **Index vs scan:** the index lookup stays near-constant as the table grows
  (4 to 10 us from 500 to 8000 rows) while a full scan grows linearly (up to
  47 ms per lookup).
- **MVCC vs 2PL:** under write contention, 2PL blocks all reads, MVCC blocks
  none.

Full numbers and analysis are in `benchmarks/RESULTS.md`.

## 12. Limitations

- INT primary key on the first column; one index per table.
- `WHERE` compares a column against a constant; joins are two-table equi-joins.
- Deleted space is tombstoned, not reclaimed (no page compaction).
- Concurrency control is logical (interleaved lock requests), with single-
  threaded physical page access; recovery is demonstrated on a key/value store.

## How to Run

Requires a C++17 compiler (g++ or clang++) and make. No packages to install.

```bash
cd MiniDB_Projects/Team_Cash

make           # build the SQL shell
./minidb       # start it

make test      # run all six unit-test suites
make demos     # deadlock + crash-recovery demonstrations
make bench     # index and MVCC benchmarks
```

Example session:

```sql
minidb> CREATE TABLE students (id INT, name TEXT, grade INT);
minidb> INSERT INTO students VALUES (1, 'Alice', 95);
minidb> SELECT name FROM students WHERE id = 1;
minidb> .explain SELECT * FROM students WHERE id = 1;
```
