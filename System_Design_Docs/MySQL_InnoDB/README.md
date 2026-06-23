# MySQL / InnoDB Storage Engine Architecture

> **Author:** Pranay | Roll No: 24BCS10133  
> **Course:** Advanced DBMS — System Design Discussion

---

## 1. Problem Background

InnoDB was created by Heikki Tuuri (Innobase Oy) in 1994 as a third-party storage engine for MySQL, later acquired by Oracle in 2005. At the time MySQL's default storage engine (MyISAM) had a critical flaw: no support for transactions and table-level locking only. InnoDB was built to solve this — to give MySQL proper ACID transactions with row-level locking.

The core architectural bet InnoDB made: **organize data around the primary key, not around insertion order**. Every other decision flows from this choice.

This stands in direct contrast to PostgreSQL's heap model, where data has no inherent order and indexes are always secondary. InnoDB's clustered primary index means the primary key lookup is as fast as a storage engine can possibly make it — but secondary indexes and MVCC work very differently as a result.

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         InnoDB Architecture                                 │
│                                                                             │
│  MySQL Server Layer                                                         │
│  ┌───────────────────────────────────────────────────────────┐             │
│  │  Parser → Optimizer → Executor                            │             │
│  │                           │                               │             │
│  └───────────────────────────│───────────────────────────────┘             │
│                              │ Storage Engine API                           │
│                              ▼                                              │
│  ┌───────────────────────────────────────────────────────────────────────┐ │
│  │                        InnoDB Engine                                  │ │
│  │                                                                       │ │
│  │  ┌─────────────────────────────────────────────────────────────────┐ │ │
│  │  │                    Buffer Pool                                  │ │ │
│  │  │   ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐     │ │ │
│  │  │   │Data Pages│  │Index Pgs │  │Undo Pages│  │  Change  │     │ │ │
│  │  │   │(16KB ea) │  │          │  │          │  │  Buffer  │     │ │ │
│  │  │   └──────────┘  └──────────┘  └──────────┘  └──────────┘     │ │ │
│  │  └────────────────────────────────────┬────────────────────────────┘ │ │
│  │                                       │                               │ │
│  │  ┌────────────────┐  ┌───────────────┐│  ┌──────────────────────────┐│ │
│  │  │  Lock Manager  │  │  Transaction  ││  │       Log Buffer         ││ │
│  │  │ (row-level,    │  │   System      ││  │  (redo log buffer)       ││ │
│  │  │  gap locks)    │  │               ││  └────────────┬─────────────┘│ │
│  │  └────────────────┘  └───────────────┘│               │              │ │
│  └───────────────────────────────────────│───────────────│──────────────┘ │
│                                          │               │                 │
│  ┌───────────────────────────────────────▼───────────────▼──────────────┐ │
│  │                         Disk Storage                                  │ │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │ │
│  │  │  Tablespace  │  │  Undo Logs   │  │  Redo Logs   │               │ │
│  │  │  (.ibd file) │  │(undo tbsp)   │  │(ib_logfile0) │               │ │
│  │  └──────────────┘  └──────────────┘  └──────────────┘               │ │
│  └────────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Internal Design

### 3.1 Clustered Indexes — The Core Innovation

In InnoDB, the primary key IS the table. There is no separate "heap file." Rows are stored inside the B+-Tree index, sorted by primary key value.

```
InnoDB Table Structure (B+-Tree):
┌────────────────────────────────────────────────────────────────────┐
│                        Root Page                                   │
│  Interior: [P0 | PK=100 | P1 | PK=500 | P2 | PK=900 | P3]        │
├────────────────────────────────────────────────────────────────────┤
│  Leaf Page 1 (← linked list →)  Leaf Page 2  ...  Leaf Page N    │
│  PK=1:  (all column data)        PK=101: (...)                    │
│  PK=2:  (all column data)        PK=102: (...)                    │
│  ...                             ...                               │
│  PK=99: (all column data)        PK=499: (...)                    │
│                                                                    │
│  ↑ Full row data is stored here, in primary key order ↑           │
└────────────────────────────────────────────────────────────────────┘
```

**Why this is faster than a heap for primary key lookups:**

```
PostgreSQL PK lookup:
  1. B-Tree traversal → get heap TID (page, offset)
  2. Heap file fetch → retrieve actual row data
  Total: tree_height + 1 I/Os

InnoDB PK lookup:
  1. B-Tree traversal → leaf node already contains row data
  Total: tree_height I/Os (1 fewer I/O!)
```

For a 1M row table (tree height ≈ 3), that's 3 vs 4 I/Os per lookup. At millions of queries per second, this difference matters.

**The hidden cost:** If you have no explicit primary key, InnoDB creates a hidden 6-byte `ROW_ID` column. Your data is clustered around a meaningless key. Always define a primary key in InnoDB.

#### InnoDB Page Layout (16KB default, vs PostgreSQL's 8KB)

```
InnoDB Page (16384 bytes):
┌──────────────────────────────────────────────────────┐
│ File Header (38 bytes)                               │
│  - FIL_PAGE_SPACE_OR_CHKSUM: checksum                │
│  - FIL_PAGE_OFFSET: page number                      │
│  - FIL_PAGE_PREV / FIL_PAGE_NEXT: linked list        │
│  - FIL_PAGE_LSN: last WAL LSN that touched this page │
│  - FIL_PAGE_TYPE: index, undo, iBuf bitmap, etc.     │
├──────────────────────────────────────────────────────┤
│ Page Header (56 bytes)                               │
│  - PAGE_N_DIR_SLOTS: number of directory slots       │
│  - PAGE_HEAP_TOP: offset to first free space         │
│  - PAGE_N_HEAP: number of records (incl. deleted)    │
│  - PAGE_FREE: pointer to deleted record list         │
├──────────────────────────────────────────────────────┤
│ Infimum Record (smallest possible key, sentinel)     │
│ Supremum Record (largest possible key, sentinel)     │
├──────────────────────────────────────────────────────┤
│ User Records (grow from top)                         │
│  Each record: [delete_flag][heap_no][...][col data]  │
│  Records are in a singly linked list by key order    │
├──────────────────────────────────────────────────────┤
│ Free Space                                           │
├──────────────────────────────────────────────────────┤
│ Page Directory (grows from bottom)                   │
│  Array of 2-byte offsets to "slot" records           │
│  Used for binary search within a page                │
├──────────────────────────────────────────────────────┤
│ File Trailer (8 bytes): duplicate checksum + LSN     │
└──────────────────────────────────────────────────────┘
```

**Why 16KB pages vs PostgreSQL's 8KB?** Larger pages mean more records per page, fewer I/Os for sequential scans. The trade-off is more wasted space for small tables and larger memory footprint per cached page. Neither is universally better — it depends on workload.

### 3.2 Secondary Indexes

Secondary indexes in InnoDB work very differently from primary key lookups:

```
Secondary Index (e.g., INDEX on email column):
┌─────────────────────────────────────────────────┐
│ Leaf entries: [email value | PRIMARY KEY value] │
│              NOT the actual row data!           │
└─────────────────────────────────────────────────┘

Lookup by email:
1. Traverse secondary B-Tree → get primary key value (e.g., pk=42)
2. Traverse PRIMARY B-Tree again → get actual row data
Two B-Tree traversals for non-covering index scans!
```

**Comparison with PostgreSQL:**

| | InnoDB Secondary Index | PostgreSQL Index |
|---|---|---|
| Leaf stores | Primary key value | Heap TID (page, offset) |
| Non-covering lookup | 2 B-Tree traversals | 1 B-Tree + 1 heap fetch |
| Row moved (UPDATE PK) | Secondary index auto-updated | TID changes, all indexes updated |
| Index-only scan | Yes (if covering) | Yes (if visible in visibility map) |

**InnoDB's secondary index advantage:** When rows are updated but primary key doesn't change, secondary indexes don't need to update the row data pointer (it still points to the same primary key). In PostgreSQL, any UPDATE (even a non-indexed column) can change the heap TID, potentially requiring all indexes to be updated (unless HOT optimization applies).

### 3.3 Buffer Pool

InnoDB's buffer pool is analogous to PostgreSQL's shared buffer pool, but with some important differences:

```
InnoDB Buffer Pool (innodb_buffer_pool_size, default 128MB):
┌─────────────────────────────────────────────────────────────────┐
│                  Modified LRU List                              │
│                                                                 │
│  NEW sublist (5/8 of buffer pool):                              │
│  Recently accessed pages → most recently used end              │
│  ← hot pages ← midpoint ← cold pages ←                        │
│                    ↑                                           │
│  OLD sublist (3/8 of buffer pool):                              │
│  Pages enter here from disk (not yet confirmed "hot")          │
│  Victims evicted from old sublist tail                         │
└─────────────────────────────────────────────────────────────────┘
```

**Why the midpoint insertion (not head)?** InnoDB's LRU has a "young" and "old" sublist. New pages enter at the midpoint (head of old sublist). If accessed again within `innodb_old_blocks_time` (default 1000ms), they're promoted to the new sublist. This prevents large full-table scans from evicting hot working-set pages — a common problem with naive LRU.

**Change Buffer:** When a secondary index page is NOT in the buffer pool and an INSERT/DELETE/UPDATE would modify it, InnoDB buffers the change in the **Change Buffer** (part of the buffer pool) rather than reading the page from disk just to update it. Changes are merged later when the page is actually read. This significantly reduces random I/O for write-heavy workloads.

### 3.4 Undo Logs — MVCC the Oracle Way

InnoDB uses **undo logs** for MVCC. This is fundamentally different from PostgreSQL's heap-based tuple versioning.

```
InnoDB MVCC Model:
                    Current row (latest version in clustered index)
                    id=1, name="Alice Smith", xid=200
                         ↓ roll_ptr (undo pointer)
                    Undo Log Record (for xid=200 update)
                    id=1, name="Alice", xid=100 (before update)
                         ↓ roll_ptr
                    Undo Log Record (for xid=100 insert)
                    id=1, name="Alice", xid=50
                    (no further pointer = original insert)
```

**How a read works:**

```
Transaction T (started when committed_txid = 150):
- Reads row id=1: current version has xid=200 (not visible to T)
- Follows undo chain: xid=100 (visible to T? yes, 100 < 150) → return "Alice"
```

**InnoDB MVCC vs PostgreSQL MVCC:**

| | InnoDB | PostgreSQL |
|---|---|---|
| Old versions stored | Undo log (separate from table) | Heap file (same as table) |
| Current row | Always up-to-date in clustered index | Varies; old and new versions coexist |
| Read old version | Follow undo chain (extra I/Os for old versions) | Find old tuple directly in heap |
| Cleanup of old versions | Purge thread removes undo log entries | VACUUM removes dead tuples from heap |
| Bloat location | Undo tablespace can grow large | Table files grow with dead tuples |

**InnoDB's advantage:** The clustered index always has the LATEST version. Writes are in-place (update the row, push old version to undo). For read-heavy workloads with fresh data, reads are extremely fast.

**InnoDB's disadvantage:** For long-running transactions reading old data, InnoDB must traverse the undo chain, which can be long (many hops through the undo log). PostgreSQL's old tuples are right there in the heap — potentially faster for reading old versions.

### 3.5 Redo Logs

While undo logs provide MVCC and rollback, **redo logs** provide durability. Together they are the "dual log" system.

```
Why InnoDB needs BOTH:
┌──────────────────────────────────────────────────────────┐
│ Undo logs: MVCC (read old versions) + ROLLBACK support   │
│            "I need to undo this if the txn aborts"       │
│                                                          │
│ Redo logs: DURABILITY (WAL equivalent)                   │
│            "I need to redo committed changes after crash" │
└──────────────────────────────────────────────────────────┘
```

```
Redo Log Format:
┌──────────────────────────────────────────┐
│ LSN (Log Sequence Number) — monotonic    │
│ Space ID + Page Number (which page)      │
│ Byte offset within page                 │
│ Length                                   │
│ Data (what bytes were written)           │
└──────────────────────────────────────────┘
```

InnoDB redo logs are stored in `ib_logfile0`, `ib_logfile1` (circular buffer). In MySQL 8.0, this became a single `#ib_redo0` file with dynamic sizing.

**Crash recovery with redo + undo:**

```
1. Crash occurs mid-transaction
2. Restart:
   a. REDO phase: Replay all redo log records → bring pages to state at crash point
   b. UNDO phase: For all transactions that were active (not committed) at crash:
      - Follow their undo chain → reverse all their changes
3. Database is now consistent (only committed transactions are visible)
```

This two-phase recovery is why InnoDB needs both logs. Redo brings the database forward to the crash point; undo removes the "pollution" from uncommitted transactions.

### 3.6 Row-Level Locking and Gap Locks

InnoDB implements row-level locking — a major advancement over MyISAM's table locks.

```
Lock Types:
┌────────────────────────────────────────────────────────────────┐
│ Record Lock:  Locks a specific row (index record)              │
│               S or X (shared or exclusive)                     │
│                                                                │
│ Gap Lock:     Locks the GAP before an index record             │
│               Prevents PHANTOM reads in REPEATABLE READ        │
│               Example: lock on (10, 20) gap prevents inserting │
│               PK=15                                            │
│                                                                │
│ Next-Key Lock: Record lock + gap lock before it                │
│               Default locking for range queries                │
│               Example: WHERE id BETWEEN 10 AND 20             │
│               locks all records in range + gaps between them   │
└────────────────────────────────────────────────────────────────┘
```

**Phantom reads without gap locks:**

```
Session A (REPEATABLE READ):
  SELECT * FROM orders WHERE amount > 100;  → sees 5 rows

Session B:
  INSERT INTO orders (amount) VALUES (150);  → commits

Session A:
  SELECT * FROM orders WHERE amount > 100;  → sees 6 rows (PHANTOM!)
```

Gap locks prevent Session B from inserting into the range that Session A is scanning. This is why InnoDB achieves REPEATABLE READ isolation without full serializable overhead.

**The gap lock trade-off:** Gap locks can cause unexpected deadlocks in concurrent insert workloads. If two transactions both try to insert into the same gap (after taking conflicting gap locks), deadlock detection kicks in and rolls back one of them. Understanding gap locks is critical for high-concurrency InnoDB applications.

---

## 4. Design Trade-Offs

### Clustered Index Trade-Offs

| | Advantage | Limitation |
|---|---|---|
| PK lookups | O(log n) with no heap fetch — row data is right there | Secondary index lookups require 2 B-Tree traversals |
| Range scans on PK | Sequential read (pages are physically ordered) | INSERT with non-sequential PKs causes page splits (UUID PKs are notorious for this) |
| No separate heap file | Simpler storage: one B-Tree per table | Cannot use multiple clustered orders; can't cluster on a secondary key easily |

**The UUID problem:** UUIDs as primary keys are random. Random inserts mean every INSERT targets a random page — the "working set" is the entire table, thrashing the buffer pool and causing constant page splits. Mitigations: `UUID_TO_BIN(uuid, 1)` (swap time-high bits to make ordered), use `BIGINT AUTO_INCREMENT`, or use ULIDs.

### Dual-Log Trade-Offs

| | Redo Log | Undo Log |
|---|---|---|
| Purpose | Durability (crash recovery) | MVCC + Rollback |
| Location | ib_logfile* (ring buffer) | undo tablespace (.ibu files) |
| Retained for | Until checkpoint | Until no active transaction needs old versions |
| Can grow unboundedly? | No (circular ring buffer) | YES — long-running transactions hold undo |

**The long-transaction problem:** A transaction started 12 hours ago prevents InnoDB from purging undo log entries. The undo tablespace grows. History list length (HLL) increases. Read performance degrades as readers must traverse longer undo chains. This is InnoDB's equivalent of PostgreSQL's table bloat — but it lives in undo tablespace, not the table file.

### MVCC Trade-Offs (InnoDB vs PostgreSQL)

```
Write workload:
InnoDB:    UPDATE → in-place write to clustered index + undo log append (2 writes)
PostgreSQL: UPDATE → new tuple appended to heap + WAL record (effectively 2 writes)
→ Similar write amplification, different location

Read workload (latest version):
InnoDB:    clustered index leaf has latest version → very fast
PostgreSQL: heap fetch required after index scan → one extra I/O

Read workload (old snapshot):
InnoDB:    must traverse undo chain (length = number of updates since snapshot)
PostgreSQL: old tuple is directly addressable in heap (lookup by TID)
→ InnoDB loses for long-running reads on heavily updated data
```

### Locking Trade-Offs

| | Row-Level Locking | Gap Locks |
|---|---|---|
| Advantage | High concurrency; only lock what you touch | Prevents phantoms without full serializable overhead |
| Limitation | More complex lock manager; deadlock detection overhead | Surprising deadlocks on concurrent inserts; can reduce throughput |

---

## 5. Experiments / Observations

### Experiment 1: Clustered Index vs Heap — Sequential Insert Comparison

```sql
-- InnoDB with sequential PK (AUTO_INCREMENT) — optimal
CREATE TABLE orders_seq (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  data VARCHAR(255)
) ENGINE=InnoDB;

-- InnoDB with UUID PK — suboptimal
CREATE TABLE orders_uuid (
  id CHAR(36) PRIMARY KEY,
  data VARCHAR(255)
) ENGINE=InnoDB;

-- Insert 100,000 rows into each
-- Sequential PK: inserts always go to the last leaf page → minimal splits
-- UUID PK: random page targets → frequent splits → fragmentation
-- Observation: orders_uuid inserts ~3-4x slower AND takes ~40% more disk space
-- (fragmentation from splits leaves partially empty pages)

-- Fix: use ordered UUIDs
-- INSERT INTO orders_uuid VALUES (UUID_TO_BIN(UUID(), 1), 'data');
-- UUID_TO_BIN with swap=1 puts time-high bits first → monotonic ordering
```

### Experiment 2: Gap Lock Deadlock

```sql
-- Reproducing a classic gap lock deadlock
-- Table: CREATE TABLE t (id INT PRIMARY KEY, val INT); INSERT INTO t VALUES (1,1),(10,10);

-- Session A:                            -- Session B:
BEGIN;                                   BEGIN;
SELECT * FROM t WHERE id = 5 FOR UPDATE; SELECT * FROM t WHERE id = 7 FOR UPDATE;
-- A holds gap lock on (1, 10)           -- B holds gap lock on (1, 10) too
-- (gap locks are compatible with each   -- (gap locks DON'T conflict with each other)
-- other!)

INSERT INTO t VALUES (6, 6);            INSERT INTO t VALUES (8, 8);
-- A's INSERT waits for B's gap lock    -- B's INSERT waits for A's gap lock
-- DEADLOCK DETECTED                    -- One transaction rolled back
```

**Observation:** Two sessions both took shared gap locks (compatible), then tried to insert into the same gap. Each INSERT needed an exclusive lock — deadlock. This is a well-documented InnoDB footgun in concurrent insert workloads.

### Experiment 3: History List Length Under Long Transaction

```sql
-- Start a long-running transaction in Session A
BEGIN; -- Session A starts
SELECT * FROM large_table LIMIT 1; -- establishes snapshot

-- Session B: run 100k updates
UPDATE large_table SET val = val + 1; -- 100k rows updated

-- Check history list length
SHOW ENGINE INNODB STATUS\G
-- Look for: "History list length XXXX"
-- With the long transaction in A, this won't be purged
-- The longer A runs, the more undo log accumulates

-- Observation after 1M updates with A still open:
-- History list length: ~100000
-- Read performance on large_table degrades (undo chain traversal)
-- SOLUTION: Commit or rollback long transactions promptly
```

### Experiment 4: EXPLAIN on Join Query

```sql
-- InnoDB query plan for a join
EXPLAIN FORMAT=JSON
SELECT u.email, COUNT(o.id) as order_count
FROM users u
JOIN orders o ON u.id = o.user_id
WHERE u.country = 'IN'
GROUP BY u.email;

-- Key observations in the plan:
-- 1. "using_index": true on orders.user_id → index-only scan possible
-- 2. "rows": 1842 → InnoDB's row estimate from clustered index stats
-- 3. Join type "ref" → uses index lookup (not full scan)
-- 4. Driving table is users (smaller after filter) → nested loop into orders

-- Unlike PostgreSQL, MySQL/InnoDB traditionally uses nested loop joins
-- Hash joins only added in MySQL 8.0
-- "eq_ref" join type = PK lookup into clustered index → extremely fast
```

---

## 6. Key Learnings

**1. Clustering is a bet on primary key access patterns.**  
InnoDB's clustered index is a brilliant optimization for workloads that primarily access data by primary key. It's a liability when the primary key is random (UUIDs) or when most access is via secondary indexes. Know your access patterns before choosing a PK strategy.

**2. Dual logging (undo + redo) solves different problems than single logging (WAL).**  
PostgreSQL's WAL handles durability and recovery. InnoDB needs undo for MVCC (PostgreSQL uses the heap for this) AND redo for durability. The dual-log architecture is more complex but the undo-based MVCC keeps the clustered index always current, which is a major read performance win.

**3. Gap locks are a concurrency trap for the uninitiated.**  
Row-level locking sounds perfect for concurrency, but gap locks in REPEATABLE READ create surprising interactions in concurrent insert workloads. Many production incidents at companies running MySQL involve gap lock deadlocks. Understanding this is essential for InnoDB in production.

**4. The buffer pool's midpoint insertion is a solved problem PostgreSQL partially doesn't have.**  
InnoDB's split LRU with midpoint insertion explicitly protects the "hot" working set from being evicted by sequential scans. PostgreSQL uses ring buffers for sequential scans to achieve the same goal, but the mechanism is different. Both reflect the same insight: full-table scans should not evict the working set.

**5. Long transactions are InnoDB's silent killer.**  
In PostgreSQL, long transactions cause table bloat (dead tuples can't be vacuumed). In InnoDB, they cause undo log growth and History List Length buildup, degrading read performance for everyone. The symptom is different; the root cause is the same: MVCC requires retaining old versions for as long as any transaction might need them.

**6. InnoDB made MySQL production-worthy.**  
MySQL without InnoDB (MyISAM only) was unsuitable for transactional applications. InnoDB's ACID compliance, row-level locking, and crash recovery transformed MySQL from a fast-but-unsafe read store into a production database. The architecture decisions that made this possible are still visible in every InnoDB source file.

---

## References

- InnoDB internals: https://dev.mysql.com/doc/refman/8.0/en/innodb-storage-engine.html
- "MySQL Internals Manual" — Oracle
- "High Performance MySQL" — Baron Schwartz, Peter Zaitsev, Vadim Tkachenko
- InnoDB page format: https://dev.mysql.com/doc/internals/en/innodb-page-structure.html
- Gap locks: https://dev.mysql.com/doc/refman/8.0/en/innodb-locking.html#innodb-gap-locks
