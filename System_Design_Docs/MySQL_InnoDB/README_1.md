# MySQL / InnoDB Storage Engine

**Student:** Romit Raj Sahu | 24BCS10436

---

## 1. Problem Background

InnoDB was not MySQL's original storage engine. MySQL shipped with MyISAM, which was fast and simple but had no transactions, no foreign key enforcement, and table-level locking. An application crash in the middle of a multi-row update could leave the table in a corrupt, inconsistent state with no way to recover.

Innobase Oy (later acquired by Oracle) built InnoDB in 1998 to solve this. The design goal was clear: bring ACID transactions to MySQL without sacrificing the performance characteristics MySQL users expected. This meant row-level locking instead of table locks, crash-safe writes, and support for concurrent reads and writes.

The architectural decisions InnoDB made to achieve this — clustered indexes, undo logs, redo logs — are different in important ways from PostgreSQL's approach to the same problems. Understanding why InnoDB chose differently reveals a lot about the trade-off space in database storage engine design.

---

## 2. Architecture Overview

```
MySQL Server Layer
┌────────────────────────────────────────────────────────┐
│  Connection Manager → Parser → Optimizer → Executor    │
│                                    ↕                   │
│                          Storage Engine API             │
│                    (handler interface in handler.h)     │
└─────────────────────────┬──────────────────────────────┘
                          │ pluggable storage engine API
                          ▼
InnoDB Storage Engine
┌────────────────────────────────────────────────────────┐
│                                                        │
│  ┌────────────────────────────────────────────────┐   │
│  │  Buffer Pool (innodb_buffer_pool_size)          │   │
│  │  ┌─────────────────┐  ┌─────────────────────┐  │   │
│  │  │  LRU old sublist│  │  LRU new sublist    │  │   │
│  │  │  (5/8 of pool)  │  │  (3/8 of pool)      │  │   │
│  │  └─────────────────┘  └─────────────────────┘  │   │
│  └────────────────────────────────────────────────┘   │
│                                                        │
│  ┌──────────────────┐  ┌─────────────────────────┐   │
│  │  Undo Log        │  │  Redo Log (ib_logfile)  │   │
│  │  (rollback segs) │  │  Sequential write log   │   │
│  └──────────────────┘  └─────────────────────────┘   │
│                                                        │
│  ┌──────────────────────────────────────────────────┐ │
│  │  Tablespace files (.ibd per table)               │ │
│  │  Clustered Index (primary key B+Tree)            │ │
│  │  Secondary Indexes (separate B+Trees)            │ │
│  └──────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────────┘
```

---

## 3. Internal Design

### 3.1 Clustered Indexes — The Core Architectural Decision

InnoDB's most important structural difference from PostgreSQL: **the table IS the primary key B+Tree.** There is no separate heap.

In PostgreSQL, a table is a heap file (pages of unsorted rows). An index is a separate file. An index lookup finds a TID (page, slot), then fetches the row from the heap.

In InnoDB, the primary key B+Tree's leaf nodes contain the complete row data. There is no heap. Finding a row by primary key is a single B+Tree traversal. There is no second step.

```
InnoDB Clustered Index (primary key = id):

Internal node:     [5 | 15 | 25]
                  /    |    |    \
Leaf nodes:  [1,2,3,4] [5..14] [15..24] [25,26,27]
              (full rows at leaves)

Each leaf entry:
  key = primary key value
  data = ALL other columns of the row
```

**Consequence of no heap:** The physical order of rows on disk is the primary key order. A range scan on the primary key (`WHERE id BETWEEN 1000 AND 2000`) reads contiguous leaf pages — excellent sequential I/O. A range scan on any other column requires a secondary index plus a clustered index lookup for each row.

**What if there is no primary key?**

InnoDB generates an invisible 6-byte `row_id` column as the cluster key. This is a global counter across all InnoDB tables, which can become a contention point in very high-write workloads. Always define an explicit primary key.

---

### 3.2 Secondary Indexes

Secondary indexes in InnoDB do not store row pointers (page number + slot). They store the primary key value.

```
Secondary index on (last_name):

Leaf entry: last_name value + primary key value
                                    ↓
                     Used to look up in clustered index
```

**Why not store the physical location?**

In PostgreSQL's heap model, row positions are stable — a tuple's TID (page, slot) does not change unless VACUUM moves it. In InnoDB's clustered model, when a B+Tree leaf page splits, rows move to the new page. Their physical positions change. If secondary indexes stored physical locations, every page split would require updating all secondary indexes — an O(indexes × rows_moved) operation.

By storing the primary key instead, page splits only update the clustered index and the parent separator. Secondary indexes remain correct because the primary key does not change.

**The lookup cost:**

A secondary index lookup does two B+Tree traversals: once in the secondary index to find the primary key, once in the clustered index to find the row. This is called a "double lookup" or "bookmark lookup." For queries that touch many rows via a secondary index, this doubles the index traversal cost compared to PostgreSQL's single TID lookup.

InnoDB mitigates this with **index condition pushdown** (ICP) and **covering indexes**: if all columns needed by the query are in the secondary index, the clustered index lookup is skipped entirely.

---

### 3.3 Buffer Pool

InnoDB's buffer pool serves the same purpose as PostgreSQL's shared_buffers — keeping hot pages in RAM to avoid disk I/O. The implementation differs in one important way: **midpoint insertion**.

**Standard LRU problem:** A full table scan reads every page once. If the table is larger than the buffer pool, this evicts all hot (frequently accessed) pages and fills the pool with pages that will never be accessed again.

**InnoDB's solution: split the LRU into two sublists:**

```
LRU List:
┌─────────────────────────────────────────────────────┐
│  New Sublist (3/8 of pool)    │  Old Sublist (5/8)  │
│  Hot pages — stay here long   │  Cold pages         │
└─────────────────────────────────────────────────────┘
                                ↑
                         Midpoint
                   New pages inserted here
```

When a new page is brought into the buffer pool, it goes to the head of the old sublist (the midpoint). If it is accessed again within `innodb_old_blocks_time` (default 1 second), it graduates to the new sublist. If not (i.e., it was a one-time sequential scan read), it ages toward the tail of the old sublist and gets evicted.

This means a full table scan fills only the old sublist and gets evicted quickly. Frequently accessed pages in the new sublist are protected.

---

### 3.4 MVCC via Undo Logs

InnoDB and PostgreSQL both implement MVCC, but through fundamentally different mechanisms.

**PostgreSQL approach:** Keep all tuple versions in the heap. A reader checks each tuple's xmin/xmax against its snapshot.

**InnoDB approach:** Store only the current version of each row in the clustered index. Keep old versions in a separate undo log. When a reader needs an older version, it follows the undo log chain backward.

```
Clustered index (current state):
  row id=5: name='Bob', age=30, trx_id=205, roll_ptr → undo log

Undo log chain:
  undo record (trx=205): name='Bob', age=30 → previous: trx=100
  undo record (trx=100): name='Alice', age=25 → previous: trx=50
  ...
```

When a transaction with `start_trx_id=150` reads row id=5:
1. It sees the current version was written by trx 205 (which started after our snapshot)
2. It follows roll_ptr to the undo log
3. It finds the version written by trx 100 (which committed before our snapshot)
4. That version is returned

**Implications:**

- InnoDB updates are **in-place**: the page is modified directly. Old versions go to the undo log, not kept in the heap.
- Long-running transactions cause undo log growth. If a transaction holds a snapshot for hours while other transactions write heavily, the undo log must retain all versions created during that time.
- InnoDB's MVCC reads are more expensive in the "old data" case: following the undo log chain requires reading from undo tablespace, potentially from disk.
- InnoDB does not need VACUUM for dead tuple cleanup (there are no dead tuples in the clustered index). The purge thread handles undo log cleanup instead.

---

### 3.5 Redo Log and Crash Recovery

InnoDB's redo log (`ib_logfile0`, `ib_logfile1`, written to `ib_redo` in MySQL 8) is a circular buffer of fixed-size log files.

```
Redo log: circular buffer
┌─────────────────────────────────────────────────────────┐
│  LSN (Log Sequence Number) increases monotonically      │
│                                                         │
│  [checkpoint LSN .... current LSN]                      │
│   ↑                                ↑                    │
│   Pages up to here are             New log records      │
│   flushed to disk                  written here         │
└─────────────────────────────────────────────────────────┘
```

Every modification writes a physical redo record before the page is modified in the buffer pool. On commit, the redo log is fsynced. Dirty pages in the buffer pool are written lazily by the background I/O threads.

**Crash recovery:**

```
1. Find the last checkpoint in the redo log
2. Replay all redo records from checkpoint to end of log
3. Roll back any transactions that were active at crash time (using undo log)
```

Step 3 is important: redo gives InnoDB durability for committed transactions, and undo gives InnoDB atomicity for uncommitted ones. This is why InnoDB needs both logs. PostgreSQL needs only WAL because it doesn't do in-place updates — uncommitted changes in PostgreSQL are simply dead tuples with an uncommitted xmin, visible to no one.

---

### 3.6 Locking: Row Locks and Gap Locks

InnoDB uses row-level locking for concurrent write transactions. But row locks alone are not sufficient to prevent phantom reads.

**Phantom read problem:**

```
Transaction T1 (REPEATABLE READ):
  SELECT * FROM orders WHERE amount > 100;   -- sees 5 rows

Transaction T2 (concurrent):
  INSERT INTO orders (amount) VALUES (150);
  COMMIT;

Transaction T1:
  SELECT * FROM orders WHERE amount > 100;   -- sees 6 rows (phantom!)
```

**Gap locks:** InnoDB locks not just existing rows but the gaps between index entries.

```
Index: [10] [20] [30] [50]

Query: SELECT ... WHERE id BETWEEN 20 AND 40 FOR UPDATE

Locks acquired:
  Row lock on id=20
  Row lock on id=30
  Gap lock on (20, 30)    -- prevents insert of 21..29
  Gap lock on (30, 40)    -- prevents insert of 31..39
  Gap lock on (40, 50)    -- prevents insert of 40..49
```

A gap lock prevents any transaction from inserting a row whose index key falls in the locked range. This prevents phantoms in REPEATABLE READ without requiring SERIALIZABLE isolation.

**The downside:** Gap locks increase the chance of deadlock. Transaction T1 locks gap (20, 30). Transaction T2 locks gap (30, 40). Both then try to insert into the other's gap. Neither can proceed. InnoDB detects this via a wait-for graph and aborts one transaction.

---

## 4. Design Trade-offs

### PostgreSQL vs InnoDB MVCC

| Aspect | PostgreSQL | InnoDB |
|--------|------------|--------|
| Old row versions | Kept in heap (table bloat) | Kept in undo log (undo bloat) |
| Read old version | Check tuple header (fast) | Follow undo chain (slower for old data) |
| Update mechanism | Insert new version + mark old dead | Modify in place + write undo record |
| Cleanup mechanism | VACUUM (explicit background process) | Purge thread (automatic, internal) |
| Long txn impact | Prevents vacuum, table bloat | Undo log grows, slows reads |
| Read performance | Consistent regardless of age | Slower for very old snapshots |

Neither is strictly better. PostgreSQL's heap-based MVCC is simpler to reason about and faster for readers that need recent data. InnoDB's undo-based MVCC is faster for writers (in-place updates avoid finding free space) and avoids the table file size growth that PostgreSQL's dead tuples cause.

### Clustered vs Heap Storage

| Aspect | InnoDB (clustered) | PostgreSQL (heap) |
|--------|-------------------|-------------------|
| PK range scan | Sequential I/O (adjacent pages) | Random I/O (heap scattered) |
| Secondary index | Double lookup (index + PK) | Single lookup (index + TID) |
| Page split cost | Only clustered index affected | N/A (heap doesn't split) |
| Row ordering | Determined by PK | Insertion order |
| Good PK choice | Sequential (avoids random inserts) | Any |

The clustered index is a significant advantage for primary key range queries. It is a disadvantage for tables with many secondary indexes (each secondary index lookup costs extra) and for tables with UUID primary keys (random inserts cause random page splits and B+Tree fragmentation — a well-known MySQL performance problem).

---

## 5. Experiments and Observations

### Observing clustered index range scan vs secondary index scan

```sql
-- Range scan on primary key (clustered): efficient
EXPLAIN SELECT * FROM orders WHERE id BETWEEN 1000 AND 2000;
-- Output: clustered index range scan, ~1000 rows, sequential pages

-- Range scan on secondary index column: double lookup
EXPLAIN SELECT * FROM orders WHERE customer_id BETWEEN 50 AND 60;
-- Output: secondary index range scan + rows → primary key lookup
-- If many rows match, optimizer may choose full table scan instead
```

The optimizer switches from secondary index to full table scan when the selectivity is low enough that the double-lookup cost (for each row: secondary index access + primary key traversal) exceeds the cost of a single sequential scan. This threshold is approximately 20–30% of the table.

### Undo log growth under long transaction

```sql
-- Start a long-running read transaction
START TRANSACTION;
SELECT * FROM large_table LIMIT 1; -- takes snapshot

-- In another session, run 100,000 updates
UPDATE large_table SET col = col + 1;

-- Check undo log size:
SELECT NAME, SUBSYSTEM, COUNT FROM INFORMATION_SCHEMA.INNODB_METRICS
WHERE NAME LIKE '%undo%';
```

Undo log grows proportionally to the number of writes performed while the long transaction holds its snapshot. The purge thread cannot clean undo records that are still visible to the oldest active snapshot.

### Gap lock deadlock example

```sql
-- Session 1:
BEGIN;
SELECT * FROM t WHERE id = 10 FOR UPDATE;
-- Acquires: row lock on id=10, gap lock (5, 10)

-- Session 2:
BEGIN;
SELECT * FROM t WHERE id = 5 FOR UPDATE;
-- Acquires: row lock on id=5, gap lock (5, 10) — WAIT

-- Session 1:
INSERT INTO t VALUES (7, 'x');
-- Tries to insert into gap (5, 10) — DEADLOCK
```

InnoDB detects this via wait-for graph cycle detection and rolls back one transaction with: `ERROR 1213: Deadlock found when trying to get lock; try restarting transaction`.

---

## 6. Key Learnings

**The clustered index is not just a performance feature — it is a storage architecture.** In InnoDB, you cannot separate the table from the primary key. They are the same B+Tree. This has implications for every other design decision: secondary indexes must store the primary key (because row positions change), updates must go in-place (because there is no heap to insert a new version into), and UUID primary keys are genuinely problematic (not just a style concern).

**InnoDB needs both undo and redo logs because of in-place updates.** PostgreSQL needs only WAL because its append-only heap model means uncommitted changes are simply invisible tuples — no undo is needed. InnoDB's in-place update model is more space-efficient (no dead tuples in the table), but requires undo to reconstruct old versions for MVCC and to roll back uncommitted transactions.

**Gap locks are a pragmatic compromise.** REPEATABLE READ in InnoDB prevents phantoms (which SQL standard says it does not need to) using gap locks. This is operationally convenient — most applications get phantom safety without needing SERIALIZABLE — but it increases deadlock probability and must be accounted for in application design.

**The buffer pool midpoint insertion is a great example of hardware-aware algorithm design.** The problem it solves (full table scans evicting hot data) is caused by the specific access pattern of sequential reads. The solution is elegant: treat sequentially-read pages as "probationary" until they are accessed again, then promote them. This is O(1), requires no statistics collection, and works automatically.
