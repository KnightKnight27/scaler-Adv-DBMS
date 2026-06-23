# MiniDB: Educational Relational Database Engine

MiniDB is a C++20 Advanced DBMS capstone project. It is intentionally built as
a small relational database engine showing storage, indexing, optimization,
transactions, recovery, and an LSM-tree extension.

## Selected Track

Track C — Modern Storage

## Team Members

```text
Shreyas Reddy Singireddy
Roll Number: 24bcs10152
Email: shreyas.24bcs10152@sst.scaler.com
```

```text
Sushant Bajaj
Roll Number: 24bcs10262
Email: sushant.24bcs10262@sst.scaler.com
```

```text
Anshal Kumar
Roll Number: 24bcs10190
Email: anshal.24bcs10190@sst.scaler.com
```

```text
Akarsh Garg
Roll Number: 24bcs1081
Email: akarsh.24bcs10181@sst.scaler.com
```

## 1. Project Overview

Supported SQL subset:

```text
INSERT table key value
SELECT table WHERE id=key
DELETE table WHERE id=key
SELECT table1 JOIN table2 ON table1.id=table2.id
BEGIN
COMMIT
ABORT
```

Each table has an integer primary key `id` and a string value. Tables are
created lazily on first use to keep the SQL grammar compact for viva/demo.

## 2. Architecture Diagram

```text
        SQL text
           |
        Parser
           |
      Logical Query
           |
      Optimizer  <--- table stats: row count, page count, B+ tree height
           |
      Query Plan
           |
        Executor
           |
   +-------+--------+----------------+
   |                |                |
Storage Engine   B+ Tree       Transaction Manager
   |                |                |
Heap File      key -> RID      Strict 2PL locks
   |
Buffer Pool  <---- WAL / Recovery
   |
Disk Manager
   |
4096-byte page files

Extension Track:
MemTable -> SSTable -> Compaction  (LSM Tree)
```

## 3. Storage Layer

### Page format

- Fixed page size: `4096` bytes.
- Every page has a `page_id`.
- Records are addressed by `RID(page_id, slot_id)`.
- Heap pages use:
  - magic header,
  - slot count,
  - free-space pointers,
  - slot directory,
  - record payload area.

### Heap file

`HeapFile` supports:

- `Insert(record) -> RID`
- `Read(RID)`
- `Delete(RID)`
- `Scan()`

Delete marks a slot inactive. It does not compact the page, which keeps the page
format easy to explain.

### Buffer pool

`BufferPoolManager` provides:

- page caching,
- pin/unpin through `PageGuard`,
- dirty page tracking,
- `FlushPage` / `FlushAll`,
- simple LRU eviction.

### Disk manager

`DiskManager` persists fixed-size pages to table files. Page allocation, reads,
and writes are visible in demo traces as `[DISK]`.

## 4. Indexing

MiniDB has an in-memory B+ tree primary-key index:

```text
int key -> RID(page_id, slot_id)
```

### B+ tree node structure

- Internal nodes store separator keys and child pointers.
- Leaf nodes store sorted keys and RIDs.
- Leaf nodes are linked with `next` pointers for ordered traversal.

### Search path

Search starts at the root, follows separator keys down to a leaf, and then does
a lower-bound lookup inside the leaf.

### Insert splitting

Insert places the key in a leaf. When a node overflows:

1. split the node,
2. promote separator key,
3. recursively split parents if required,
4. create a new root if the old root splits.

### Delete limitation / tradeoff

Delete is functionally correct for this capstone, but simplified: it removes
the key by rebuilding the B+ tree from remaining entries. This avoids complex
borrow/merge/root-shrink code and keeps the implementation readable. The tradeoff
is deletion cost is higher than a production B+ tree.

## 5. Query Execution

### Parser

The parser recognizes the supported SQL subset and produces a `Query` object.

### Optimizer

The optimizer chooses:

- sequential scan,
- index scan,
- nested-loop join,
- index nested-loop join.

### Executor

The executor performs:

- heap insert + B+ tree insert,
- primary-key lookup through the B+ tree when the optimizer selects index scan,
- heap scan when sequential scan is cheaper,
- delete from heap and index,
- join execution using nested-loop or index nested-loop.

## 6. Optimizer

### Cost formulas

For selection:

```text
sequential_scan_cost = page_count
selectivity          = 1 / row_count       for primary-key equality
index_scan_cost      = tree_height + selectivity
```

For joins:

```text
outer_table          = table with smaller row_count
nested_loop_cost     = outer_rows * inner_pages
index_join_cost      = outer_rows * (inner_tree_height + 1)
```

The cheaper join algorithm is selected.

### Selectivity estimation

The current selectivity model assumes primary-key equality, so equality
selectivity is approximately `1 / row_count`.

### Join ordering

MiniDB tracks row count and page count from the heap/index and chooses the
smaller table as the outer relation.

## 7. Transactions

MiniDB implements table-level strict two-phase locking.

### Strict 2PL

- Locks are acquired before access.
- Locks are held until `COMMIT` or `ABORT`.
- This gives serializable behavior for the supported SQL subset.

### Shared/exclusive locks

- `SELECT` and joins acquire shared locks.
- `INSERT` and `DELETE` acquire exclusive locks.

### Deadlock detection

The lock manager builds a waits-for graph and detects cycles. In the transaction
demo, two threads acquire locks in opposite order to trigger deadlock detection.

### Transaction rollback

Each transaction keeps a write set:

- inserted records are removed on abort,
- deleted records are restored from the old value.

## 8. Recovery

MiniDB uses append-only logical WAL.

### WAL record format

```text
LSN TXN_ID TYPE TABLE KEY OLD_VALUE NEW_VALUE
```

Examples:

```text
1 10 BEGIN "" 0 "" ""
2 10 INSERT "users" 1 "" "Sushant"
3 10 COMMIT "" 0 "" ""
4 11 DELETE "users" 1 "Sushant" ""
```

### Analysis phase

Recovery scans the WAL and identifies:

- committed transactions,
- aborted transactions,
- incomplete transactions.

### Redo phase

Committed `INSERT` and `DELETE` records are redone idempotently.

### Undo phase

Incomplete transactions are undone in reverse log order:

- undo insert: delete the inserted key,
- undo delete: restore the old value.

This is not full ARIES; it is a compact educational WAL/redo/undo model.

## 9. LSM Extension

The LSM extension is separate from the relational heap path.

### MemTable

An ordered in-memory map stores recent writes and tombstones.

### SSTable

Flush writes sorted immutable SSTable files to disk.

### Compaction

Compaction merges SSTables, keeps the newest value for each key, and removes
tombstones.

### BTree vs LSM tradeoffs

| Aspect | Heap+BTree | LSM |
|---|---|---|
| Writes | page/WAL/index update per row | append-like MemTable flush |
| Reads | fast point lookup through B+ tree | may search MemTable and SSTables |
| Space | heap pages + WAL overhead | compact SSTables after compaction |
| Best for | read-heavy indexed tables | write-heavy workloads |

## 10. Benchmarks

Run:

```bash
./build/minidb_benchmark
```

Latest local run in this workspace:

| Metric | Heap+BTree | LSM Tree |
|---|---:|---:|
| Records inserted | 5000 | 5000 |
| Write time | 279.531 ms | 9.40 ms |
| Write throughput | 17,887.10 writes/sec | 531,688.64 writes/sec |
| Random lookups | 10000 | 10000 |
| Avg read latency | 3.441 us | 2543.384 us |
| Read throughput | 290,579.415 queries/sec | 393.177 queries/sec |
| Logical data size | 68,890 bytes | 68,890 bytes |
| Physical storage | 613,951 bytes | 88,894 bytes |
| Storage amplification | 8.91x | 1.29x |

Notes:

- Heap+BTree includes table files, WAL, and catalog bytes. The B+ tree is
  rebuilt in memory from the heap, so there is no separate persisted index file.
- LSM physical size counts SSTable files.
- These numbers are demonstration-scale, not production benchmarking.

## 11. Limitations

Honest engineering tradeoffs:

- SQL grammar is intentionally small.
- Schema is fixed to `id INT PRIMARY KEY` plus string value.
- Joins are limited to primary-key equality on `id`.
- B+ tree delete rebuilds the tree instead of implementing sibling borrow/merge.
- B+ tree pages are not persisted as index pages; the index is rebuilt from heap
  records on open.
- Recovery is logical redo/undo, not full ARIES with compensation log records.
- Locks are table-level, not row-level.
- LSM has no bloom filters, sparse index blocks, or leveled compaction.

## 12. How to Build and Run

### Build

```bash
cmake -S . -B build -DMINIDB_BUILD_TESTS=ON -DMINIDB_BUILD_BENCHMARKS=ON
cmake --build build -j2
```

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

### CLI demo

```bash
./build/minidb_cli data/demo
```

Example:

```text
INSERT users 1 Sushant
SELECT users WHERE id=1
INSERT profiles 1 student
SELECT users JOIN profiles ON users.id=profiles.id
BEGIN
INSERT users 2 Temp
ABORT
SELECT users WHERE id=2
EXIT
```

### Demo scripts

```bash
demos/storage_demo.sh
demos/index_demo.sh
demos/transaction_demo.sh
demos/recovery_demo.sh
demos/lsm_demo.sh
```

Trace tags:

```text
[BUFFER] [DISK] [BTREE] [OPTIMIZER] [LOCK] [WAL] [LSM]
```
