# MiniDB Architecture

This document walks the system from the bottom up, following the path a tuple
takes from disk to a query result.

## 1. Disk and pages

Everything persistent lives in three files next to the database path:

- `<db>.data` — the heap pages, each `PAGE_SIZE` (4096) bytes
- `<db>.wal` — the write-ahead log
- `<db>.meta` — the catalog (table schemas and page lists)

`DiskManager` (`storage/disk_manager.h`) is the only code that touches the data
file. It reads and writes whole pages at `page_id * PAGE_SIZE` and hands out new
page ids.

A page is a **slotted page** (`storage/page.h`). A small header holds the slot
count, the boundary of the free space, and the page LSN. A slot directory grows
forward from the header while tuple data grows backward from the end of the
page; the gap between them is free space. Each slot stores a tuple's offset and
length, and an offset of `-1` marks a deleted slot. This layout lets tuples vary
in length and lets deletes leave stable RIDs (page id + slot id) behind.

## 2. Buffer pool

`BufferPool` (`storage/buffer_pool.h`) caches a fixed number of pages in memory.
Callers `fetch_page` (which pins the frame so it can't be evicted mid-use) and
`unpin` it when done, flagging whether they dirtied it. When all frames are
full, the least-recently-used unpinned frame is evicted, and if it is dirty it
is written back first.

The buffer pool enforces the **write-ahead rule**: before writing any dirty page
to disk it calls `LogManager::flush(page.lsn)`, guaranteeing the log records
describing a change reach disk before the changed page does.

## 3. Heap files

`HeapFile` (`storage/heap_file.h`) presents a table's pages as one logical heap.
It appends into the most recent page (allocating a new page when that one
fills), reads and deletes tuples by RID, and scans every live tuple. The page
list it works on is owned by the catalog, so it survives between operations.

## 4. Indexing

`BPlusTree` (`index/btree.h`) is an in-memory B+ Tree from int64 key to RID.
Internal nodes hold only separator keys; all values live in a linked list of
leaves. Inserts split full nodes and push a separator up; deletes that drop a
node below half capacity borrow from a sibling or merge with one. Each table's
first integer column gets a primary index, which the optimizer uses for
equality lookups. The index is rebuilt by scanning the base data at startup.

## 5. Catalog

`Catalog` (`catalog/catalog.h`) maps a table name to its schema, the list of
pages holding its rows, its index, and a cached row count. It serialises itself
to `<db>.meta` on every commit and DDL statement so the structure survives a
crash; the row count and index are reconstructed from the data during recovery.

## 6. Query processing

A statement flows through three stages:

1. **Parser** (`query/parser.h`) — a hand-written lexer and recursive-descent
   parser turn SQL text into a `Statement` struct.
2. **Optimizer** (`query/optimizer.h`) — builds a tree of operators, choosing an
   index scan over a sequential scan when an equality predicate hits the indexed
   column, and ordering a join's two inputs by estimated cardinality.
3. **Operators** (`query/operators.h`) — a Volcano (iterator) pipeline. Every
   operator exposes `open` / `next` / `close`; `next` pulls one row at a time
   from its child. Operators: `SeqScan`, `IndexScan`, `Filter`, `Project`,
   `NestedLoopJoin`.

The `Executor` drives the root operator and collects the rows.

## 7. Transactions

`LockManager` (`txn/lock_manager.h`) implements strict two-phase locking at
tuple granularity. Reads take shared locks, writes take exclusive locks, and all
locks are held until commit or abort — which gives serializable isolation.

Deadlock is prevented with **wound-wait**: a transaction id doubles as a
timestamp, so a smaller id is older. When a transaction wants a lock held
incompatibly, an older requester aborts ("wounds") the holder and proceeds,
while a younger requester aborts itself. No cycle of waiters can form.

## 8. Recovery

`LogManager` (`recovery/wal.h`) appends a record per change. Inserts log the
after-image at a RID; deletes log the before-image. Commit writes a commit
record and forces the log.

On startup `Database::recover` (`db.h`) performs ARIES-style recovery:

1. **Analysis** — scan the log to find which transactions committed.
2. **Redo** — replay every logged change in order (repeating history). Redo is
   idempotent because it writes tuples back at their original RID.
3. **Undo** — for transactions that never committed, reverse their changes in
   log order from newest to oldest.

After redo/undo the pages are flushed and the indexes and row counts are rebuilt
from the recovered base data.

## 9. Putting it together

`Database` (`db.h`) owns all of the above and is the single entry point.
`execute(sql)` parses the statement and dispatches it, wrapping each statement
in a transaction — the explicit one opened by `BEGIN`, or an implicit
single-statement transaction otherwise.
