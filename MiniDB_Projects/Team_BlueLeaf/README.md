# MiniDB

A small relational database engine built for the Advanced DBMS capstone. It puts together a
page-based storage engine, a B+Tree index, a SQL query layer, a cost-based optimizer, transactions
with Two-Phase Locking, and Write-Ahead-Logging crash recovery.

Extension track: C, Modern Storage. Besides the B+Tree/heap row store, MiniDB has a second storage
engine based on an LSM-tree (MemTable plus SSTables plus compaction). Both engines implement the same
StorageEngine interface, so we can benchmark them against each other on write throughput, read
latency, and space amplification.

Status: complete. It was built milestone by milestone (M1 storage, M2 index and parser, M3 execution
and optimizer, M4 transactions, M5 recovery, LSM, and benchmarks). See the docs/ folder for the
architecture, the design decisions, and a demo guide.

## Team

Team name: Team_BlueLeaf

| Name | Roll number | Scaler email |
|------|-------------|--------------|
| Naverdo Sandeep | 24BCS10076 | naverdo.24BCS10076@sst.scaler.com |
| Anishka Nase | 24BCS10075 | nase.24BCS10075@sst.scaler.com |

## 1. Project Overview

Problem: understand how a real relational database works from the inside by building one. That means
storing rows on disk in pages, indexing them, parsing and running SQL, choosing a good plan, running
concurrent transactions safely, and recovering committed data after a crash.

Goals: correctness and clarity over feature count. Every required part is implemented and can be
demonstrated, with an LSM-based modern-storage extension and a benchmark against the classic
B+Tree/heap design.

Chosen extension track: C, Modern Storage (LSM-tree).

## 2. System Architecture

```
   SQL text
     |
   Parser        lexer turns text into tokens, then a recursive-descent parser builds an AST
     |
   Optimizer     picks the access method (table scan vs index scan) and the join plan
     |
   Executor      Volcano operators: open / next / close, pulling one tuple at a time
     |
   StorageEngine     one interface, two engines:
     |                 RowStore  = heap file + B+Tree
     |                 LsmEngine = MemTable + SSTables
   BufferPool      caches 4 KB pages, clock-sweep replacement, STEAL + NO-FORCE
     |
   DiskManager     one database file, 4 KB pages, CRC32 checksum per page

   Alongside these: a LockManager (2PL) for transactions, and a WAL plus a recovery manager.
```

Everything above the StorageEngine line works in rows and tuples. The buffer pool and below work in
fixed 4 KB pages. The slotted page is where the two meet.

## 3. Storage Layer

- Page format (slotted page): each 4 KB page has a header (checksum, page LSN, next-page link, slot
  count, free pointer), then a slot directory growing down and records growing up, with free space in
  the middle. A record id is (page_id, slot), and the slot index stays the same even when records
  move during compaction.
- Heap files: a table's rows live in a linked chain of slotted pages. Inserts append to the tail page
  in O(1); rows are read by their record id.
- Buffer pool: a fixed set of in-memory frames, clock-sweep replacement (each frame has a usage count
  that the clock hand decrements), pin counts, and dirty write-back on eviction. A CRC32 in every page
  is written on flush and checked on read.

## 4. Indexing

A page-backed B+Tree is the primary key index. Internal nodes only hold keys (so the tree stays
short), leaves hold (key, record id) pairs, and leaves are linked so range scans walk sideways. It
supports search, insert (with node splits), delete, and range scans.

## 5. Query Execution

A recursive-descent parser (lexer to AST) handles CREATE TABLE, INSERT, SELECT (with WHERE, JOIN, and
GROUP BY), and DELETE. Execution uses the Volcano iterator model with these operators: sequential
scan, index scan, filter, project, nested-loop join, hash join, aggregate, plus insert and delete.

## 6. Optimizer

The optimizer is cost based. From a primary-key predicate plus a quick selectivity estimate it
chooses a table scan or an index scan. For a join it picks hash join or nested-loop and builds the
hash table on the smaller table.

## 7. Transactions and Concurrency

Strict Two-Phase Locking with shared and exclusive row locks, held until commit or abort. Deadlocks
are caught with a waits-for graph: if granting a lock would close a cycle, the requester is aborted.
This gives serializable behavior.

## 8. Recovery

Write-Ahead Logging: a transaction's row operations are written to the log and the commit record is
flushed before the statement returns, while data pages are written lazily. On restart the recovery
manager redoes the operations of committed transactions and skips the ones that never committed, so
committed work survives a crash and uncommitted work is rolled back. The docs explain how this
compares to full ARIES (page-level logging with an undo pass and compensation log records).

## 9. Extension Track: Modern Storage (LSM)

The LsmEngine keeps writes in an in-memory MemTable, then flushes it to an immutable on-disk SSTable
(sorted, with a key index and a Bloom filter per file). Deletes write a tombstone. Reads check the
MemTable first, then the SSTables from newest to oldest, using the Bloom filter to skip files that
cannot contain the key. Size-tiered compaction merges SSTables, keeps the newest value per key, and
drops tombstones, which reclaims space. It implements the same StorageEngine interface as the row
store, so the benchmark drives both the same way.

## 10. Benchmarks

Run `make bench` and then `./build/minidb_bench <N> <M>` (N inserts, M random reads). It reports write
throughput, point-read latency, and space amplification for both engines and writes
benchmarks/results/bench.csv. A representative run at N = 100,000 (the working set fits in the buffer
pool) on our machine:

| metric | row store | LSM |
|---|---|---|
| write throughput (ops/sec) | about 155,000 | about 1,000,000 |
| point-read latency (microseconds/op) | about 1.2 | about 4.4 |
| space amplification | about 1.3x | about 1.1x (1.12x after compaction) |

This is the expected trade-off: the LSM is roughly 6x faster on writes (sequential MemTable plus
flushes), while the B+Tree is roughly 3.6x faster on point reads (the LSM pays read amplification
across runs). At larger N, once the row store no longer fits in the buffer pool its random reads start
hitting disk and slow down, while the LSM stays Bloom-filter bounded. The design doc has the analysis.

## 11. Limitations

Single node. The index and LSM use a single INT primary key. Join planning is for two tables.
Statistics are gathered on demand (there is no stored ANALYZE). The 2PL uses row-level locks with no
gap locks, so range phantoms are not handled. Recovery is logical redo under a NO-STEAL assumption
rather than full ARIES undo. The LSM keeps a full in-memory index per SSTable rather than a sparse
block index. Each of these is a deliberate, documented simplification (see docs/DESIGN_DECISIONS.md).

## 12. How to Run

```bash
cd MiniDB_Projects/Team_BlueLeaf
make            # builds ./minidb  (or ./build.sh, which uses cmake if it is installed)
make test       # builds and runs the unit tests

./minidb run <db> <file.sql>    # run a SQL script
./minidb exec <db> "<sql>"       # run SQL from the command line
./minidb repl <db>              # interactive SQL shell
./minidb selftest               # storage-layer self test (M1)
./minidb concurrency            # 2PL concurrency and deadlock demo (M4)
./minidb recover-demo           # crash and recovery demo (M5)
make bench && ./build/minidb_bench 100000 50000   # row store vs LSM benchmark
```

See docs/DEMO.md for a full walkthrough. Requirements: a C++17 compiler (g++ or clang++) and make.
cmake is optional.
