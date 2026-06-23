# Topic 3: MySQL / InnoDB Storage Engine

> **Author:** Akshansh Sinha | Advanced DBMS — System Design Discussion

---

## 1. Problem Background

### Why InnoDB Exists

MySQL was originally designed in the mid-1990s with a pluggable storage engine architecture. The early default engine, **MyISAM**, was fast but offered no transactions, no foreign keys, and no crash recovery. For serious OLTP workloads, this was untenable.

**InnoDB** was developed by Innobase Oy (acquired by Oracle in 2005) and became MySQL's default storage engine in version 5.5 (2010). It was designed to solve exactly what MyISAM could not:

| Feature | MyISAM | InnoDB |
|---|---|---|
| Transactions | ❌ | ✅ ACID |
| Foreign Keys | ❌ | ✅ |
| Crash Recovery | ❌ (manual repair) | ✅ Redo log replay |
| Row-level Locking | ❌ Table locks only | ✅ |
| MVCC | ❌ | ✅ (undo log based) |

InnoDB's designers made one fundamental architectural bet that distinguishes it from PostgreSQL: **clustered primary key storage**. This single decision ripples through every other aspect of InnoDB's design — from secondary indexes to buffer pool management to insert performance.

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                       MySQL Server Layer                            │
│  Connection Manager → SQL Parser → Query Optimizer → Executor       │
└────────────────────────────────┬────────────────────────────────────┘
                                 │  Storage Engine API (handler interface)
                                 ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      InnoDB Storage Engine                          │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                      Buffer Pool                             │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐   │   │
│  │  │  Data Pages  │  │ Index Pages  │  │  Undo Log Pages  │   │   │
│  │  │  (Clustered  │  │  (Secondary  │  │  (Old row ver.)  │   │   │
│  │  │   B-Tree)    │  │   Indexes)   │  │                  │   │   │
│  │  └──────────────┘  └──────────────┘  └──────────────────┘   │   │
│  │  LRU List (Midpoint Insertion Strategy)                      │   │
│  └──────────────────────────┬───────────────────────────────────┘   │
│                             │                                       │
│  ┌──────────────────────────▼───────────────────────────────────┐   │
│  │                 Transaction System                           │   │
│  │   ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │   │
│  │   │  Lock System │  │  Undo Logs   │  │   Redo Logs      │  │   │
│  │   │  (Row-level, │  │  (Rollback   │  │   (WAL for       │  │   │
│  │   │   Gap locks) │  │   segments)  │  │    durability)   │  │   │
│  │   └──────────────┘  └──────────────┘  └──────────────────┘  │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                             │                                       │
└─────────────────────────────┼───────────────────────────────────────┘
                              │
                    ┌─────────▼─────────┐
                    │   .ibd Files      │
                    │  (tablespace)     │
                    │  ibdata1 (system) │
                    │  ib_logfile0/1    │
                    │  (redo log ring)  │
                    └───────────────────┘
```

---

## 3. Internal Design

### 3.1 Clustered Indexes — The Defining Architectural Choice

InnoDB stores **every table as a B-Tree indexed by the primary key**. This is called a **clustered index** because the actual row data lives at the leaf nodes of the primary key B-Tree.

```
Clustered Index (Primary Key B-Tree):
                    [50 | 100]
                   /     |     \
            [10|30]    [60|80]    [120|150]
           /  |  \    /  |  \    /   |   \
         [5] [20] [40][55][70][90][110][130][180]
          ↑
    Each leaf node contains:
      - Primary key value
      - ALL non-key column values (the entire row)
      - Undo log pointer (for MVCC old versions)
      - Transaction ID (DB_TRX_ID)
      - Rollback pointer (DB_ROLL_PTR)
```

**Why is this a significant design decision?**

**Advantage**: A primary key lookup is a single B-Tree traversal that ends with the complete row at the leaf node. There is no separate "heap fetch" step (unlike PostgreSQL's heap+index separation).

```
PostgreSQL (non-clustered):         InnoDB (clustered):
  1. Traverse B-Tree index           1. Traverse B-Tree index
  2. Get ctid (page, offset)         2. Leaf node = actual row data ✓
  3. Fetch heap page (random I/O)
  4. Read tuple from heap page
```

For range queries on the primary key (e.g., `WHERE id BETWEEN 1000 AND 2000`), InnoDB can scan contiguous leaf pages — all the data is physically ordered by PK.

**Disadvantage**: If the primary key is not monotonically increasing (e.g., UUID primary keys), every insert lands at a random position in the B-Tree, causing **page splits** and **fragmentation**. This is why InnoDB documentation strongly recommends `AUTO_INCREMENT` integer primary keys.

### 3.2 Secondary Indexes

In InnoDB, secondary indexes do NOT store row data directly. Instead, they store the **primary key value** as a pointer:

```
Secondary Index on (last_name):
  Leaf nodes: [last_name value] → [primary_key value]

To resolve: SELECT * FROM users WHERE last_name = 'Singh':
  1. Search secondary index → get primary key (e.g., pk=42)
  2. Use pk=42 to search clustered index → get full row

This is called a "double index lookup" or "bookmark lookup"
```

**Covering index optimization**: If all columns needed by the query are in the secondary index, InnoDB skips the second clustered index lookup:

```sql
-- Table: users(id PK, email, last_name, city)
-- Index: (last_name, email)
SELECT email FROM users WHERE last_name = 'Singh';
-- ✅ Covered by index: only secondary index scanned, no clustered index lookup
```

**Implication**: InnoDB secondary indexes are slightly more expensive than PostgreSQL's (require PK lookup), but InnoDB compensates with the faster primary key path. Also, if the primary key is large (e.g., UUID, 36 bytes), every secondary index stores that large PK value, inflating secondary index size.

### 3.3 Buffer Pool

InnoDB's buffer pool serves the same purpose as PostgreSQL's shared buffers but uses a **midpoint insertion LRU** strategy instead of clock sweep.

```
Buffer Pool LRU List (split into two sub-lists):
┌────────────────────────────────────────────────────────────┐
│  New Sublist (5/8 of buffer pool)  │  Old Sublist (3/8)   │
│  ← most recently accessed         │  midpoint insertion → │
│                                   ↑                        │
│                              New pages enter here          │
└────────────────────────────────────────────────────────────┘
```

**Why midpoint insertion?**

When a full table scan reads many pages sequentially, all those pages would normally evict hot working-set pages from a traditional LRU. With midpoint insertion:
1. New pages enter the **old sublist**
2. If a page is accessed again while in the old sublist, it moves to the **new sublist** (hot zone)
3. One-time scan pages stay in the old sublist and are evicted first

This is InnoDB's equivalent of PostgreSQL's ring buffer strategy for sequential scans — protecting the hot working set.

### 3.4 Undo Logs — InnoDB's MVCC Mechanism

This is the deepest architectural difference from PostgreSQL. InnoDB uses **undo logs** to reconstruct old row versions, rather than storing old versions directly in the table pages.

```
Current row in clustered index leaf:
  [pk=42 | name='Bob' | age=30 | DB_TRX_ID=500 | DB_ROLL_PTR ──────────]
                                                                        │
                                                         ┌──────────────▼───────┐
                                                         │  Undo Log Record     │
                                                         │  (pk=42, name='Alice'│
                                                         │   age=28, trx_id=400)│
                                                         └──────────────┬───────┘
                                                                        │
                                                         ┌──────────────▼───────┐
                                                         │  Older Undo Record   │
                                                         │  (pk=42, name='Ali', │
                                                         │   age=25, trx_id=300)│
                                                         └──────────────────────┘
```

**Read View (Snapshot) mechanism:**

When a transaction `T` begins (in REPEATABLE READ), InnoDB creates a **read view** containing:
- `low_limit_id`: first trx ID that was committed after the read view was taken
- `up_limit_id`: minimum active trx ID when read view was taken
- `trx_ids`: set of active (in-progress) transaction IDs at read view time

To read a row, InnoDB:
1. Reads the current version from the clustered index
2. Checks if `DB_TRX_ID` is visible to the read view
3. If NOT visible: follow `DB_ROLL_PTR` to the undo log, reconstruct the older version
4. Repeat until a visible version is found (or row is not visible = it didn't exist at snapshot time)

**Key difference from PostgreSQL**: In PostgreSQL, old tuple versions live in the heap alongside current versions (requiring VACUUM). In InnoDB, old versions live in the **undo segment** — a separate area of the tablespace. Undo is reclaimed by the **purge thread** (InnoDB's equivalent of VACUUM).

### 3.5 Redo Logs — InnoDB's Durability Mechanism

InnoDB uses a **redo log** (Write-Ahead Log) in a **circular ring buffer** format:

```
ib_logfile0 (1GB) + ib_logfile1 (1GB):
  ┌────────────────────────────────────────────────────────┐
  │ [LSN 1000][LSN 1001][LSN 1002]...[LSN 9999][LSN 1000] │
  │                                              ↑         │
  │                                         wraps around   │
  │  write_lsn ───────────────►              flushed_lsn   │
  └────────────────────────────────────────────────────────┘
```

**Why both undo AND redo?**

This is a fundamental question and the answer illuminates the design:

| Log Type | Purpose | When Written | When Discarded |
|---|---|---|---|
| **Redo log** | Crash recovery: replay committed changes | Before dirty page flush | After checkpoint |
| **Undo log** | MVCC: provide old row versions to concurrent readers; also enable ROLLBACK | Before any row modification | After no transaction needs the old version (purge) |

PostgreSQL uses **tuple versioning in the heap** for MVCC (no undo log) + **WAL** for durability. InnoDB uses **undo logs** for MVCC + **redo logs** for durability. Both approaches solve the same problems but with different data structures and different maintenance costs.

**Redo log write path:**
```
Transaction does UPDATE:
  1. Modify row in buffer pool (in-memory page)
  2. Write redo log record to redo log buffer
  3. Write undo log record (for MVCC + rollback capability)
  4. On COMMIT: flush redo log buffer to ib_logfile (fsync)
  5. Dirty page written to .ibd file asynchronously later
```

### 3.6 Locking: Row Locks, Gap Locks, and Next-Key Locks

InnoDB implements **fine-grained locking** that PostgreSQL also has, but with one notable addition: **gap locks**.

```
Table: orders with id: 5, 10, 20, 30

Gap locks protect "gaps" between existing index values:
  Gap before 5:  (-∞, 5)
  Gap 5→10:      (5, 10)
  Gap 10→20:     (10, 20)
  Gap 20→30:     (20, 30)
  Gap after 30:  (30, +∞)
```

**Next-Key Lock** = row lock + gap lock on the preceding gap. This prevents **phantom reads** in REPEATABLE READ isolation level.

```sql
-- Transaction T1:
SELECT * FROM orders WHERE id BETWEEN 10 AND 20 FOR UPDATE;
-- Acquires: row lock on id=10, id=20 + gap lock on (10,20)

-- Transaction T2 (concurrent):
INSERT INTO orders VALUES (15, ...);
-- BLOCKED by T1's gap lock on (10,20)
-- Without gap lock: T1's re-read of BETWEEN 10 AND 20 would see id=15 (phantom)
```

PostgreSQL achieves phantom-read prevention at SERIALIZABLE level using predicate locks (SSI — Serializable Snapshot Isolation). InnoDB uses gap locks at REPEATABLE READ. Both approaches work; gap locks are coarser but simpler to implement.

---

## 4. Design Trade-Offs

### 4.1 Clustered vs Non-Clustered Storage

| Trade-off | InnoDB (Clustered) | PostgreSQL (Heap) |
|---|---|---|
| PK range scans | ✅ Sequential I/O (data physically ordered) | ❌ Random I/O (heap fetch after index) |
| PK point lookup | ✅ Single tree traversal | ❌ Tree + heap fetch |
| Secondary index | ❌ Double lookup (secondary → PK → clustered) | ✅ Single lookup (index → heap) |
| INSERT order | ❌ UUID PKs cause random splits | ✅ Inserts anywhere in heap |
| UPDATE in-place | ✅ (undo log preserves old version) | ❌ Append new version to heap |
| Table scan | Similar | Similar |

**Lesson**: Clustered indexes make a strong bet that most access will be by primary key. For workloads that access data primarily via secondary indexes (e.g., analytical queries with many different filter columns), the double-lookup penalty adds up.

### 4.2 Undo Log MVCC vs Tuple Versioning MVCC

| Aspect | InnoDB (Undo logs) | PostgreSQL (Tuple versions in heap) |
|---|---|---|
| Old version storage | Separate undo segment | Alongside current data in heap |
| Read amplification | May follow undo chain | May scan dead tuples |
| Write amplification | Undo log write + redo log write | WAL write only |
| Cleanup | Purge thread (background) | VACUUM (configurable, also background) |
| Long transactions | Undo segment grows (undo retention) | Heap bloat (dead tuples accumulate) |
| Crash behavior | Undo logs survive crash → rollback on recovery | VACUUM of dead tuples not needed post-crash |

Both models have a "garbage collection" problem. InnoDB's purge thread and PostgreSQL's VACUUM are solving the same logical problem: **reclaim space held by old row versions that no transaction can see anymore**.

### 4.3 InnoDB's Redo Log Ring — A Dangerous Constraint

Because InnoDB's redo log is a **fixed-size circular ring**, if transactions produce redo records faster than the checkpointer can flush dirty pages, the log wraps around and must **stall all writes** until enough dirty pages are flushed to reclaim log space.

This is the infamous **"InnoDB checkpoint stall"** — a sudden stop of all write activity. It's caused by:
1. Large dirty page footprint (too many unflushed modifications)
2. Small redo log size (`innodb_log_file_size`)
3. Burst write workloads

PostgreSQL's WAL design avoids this: WAL files are archived rather than overwritten in a ring. Old WAL files are deleted after checkpointing confirms they're no longer needed for recovery. There's no fixed ring size constraint.

---

## 5. Experiments / Observations

### Experiment 1: Observing Clustered Index Structure

```sql
-- Create a table and check index internals
CREATE TABLE accounts (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100),
    balance DECIMAL(15,2),
    INDEX idx_name (name)
);

INSERT INTO accounts (name, balance)
SELECT CONCAT('User', n), RAND()*10000
FROM (SELECT @row := @row + 1 AS n FROM information_schema.columns, (SELECT @row:=0) r LIMIT 100000) t;

-- Check optimizer's use of clustered index
EXPLAIN SELECT * FROM accounts WHERE id BETWEEN 1000 AND 2000;
-- Expected: type=range, key=PRIMARY, Extra=Using index condition
-- No "Using filesort" or "Using temporary" for ordered access by PK

EXPLAIN SELECT * FROM accounts WHERE name = 'User500';
-- Expected: type=ref, key=idx_name, Extra=Using index
-- Shows secondary index lookup → followed by PK clustered index lookup
```

### Experiment 2: Observing Undo Log Growth Under Long Transactions

```sql
-- Session 1: Start a long transaction (holds a read view)
START TRANSACTION;
SELECT * FROM accounts WHERE id = 1;

-- Session 2: Run many updates (creates undo log entries)
UPDATE accounts SET balance = balance + 1;  -- 100,000 rows

-- Check undo log size
SELECT NAME, SUBSYSTEM, COMMENT
FROM information_schema.INNODB_METRICS
WHERE NAME LIKE 'trx_rseg%';

-- Session 1: Long transaction holds read view, purge cannot clean undo
-- Undo segment keeps growing...

-- Commit Session 1 → purge thread can now reclaim undo space
COMMIT;
```

**Observation**: The long-running transaction in Session 1 prevents the purge thread from reclaiming undo space because it holds a read view that might need to see old row versions. This is why long-running transactions in InnoDB are operationally dangerous — they cause undo segment bloat.

### Experiment 3: Gap Lock Behavior

```sql
-- Session 1
START TRANSACTION;
SELECT * FROM accounts WHERE id BETWEEN 100 AND 200 FOR UPDATE;

-- Session 2 (concurrent)
INSERT INTO accounts (id, name, balance) VALUES (150, 'NewUser', 500);
-- → BLOCKED (gap lock from Session 1)

-- Session 1 COMMIT or ROLLBACK → Session 2 proceeds
```

**Observation**: This demonstrates that InnoDB's gap locks prevent phantom reads in REPEATABLE READ isolation without needing full Serializable isolation. PostgreSQL would require SERIALIZABLE isolation level for the same guarantee.

### Experiment 4: UUID vs AUTO_INCREMENT Insert Performance

```sql
-- Table with UUID primary key (bad for InnoDB clustered index)
CREATE TABLE uuid_test (id VARCHAR(36) PRIMARY KEY, data TEXT);

-- Table with AUTO_INCREMENT (good for InnoDB)
CREATE TABLE int_test (id BIGINT AUTO_INCREMENT PRIMARY KEY, data TEXT);

-- Insert 100,000 rows into each and compare time
-- UUID: random page splits, high fragmentation, index page fill drops
-- INT: sequential inserts at rightmost leaf page, minimal splits

-- After insertion, check fragmentation:
SELECT table_name, data_free, data_length
FROM information_schema.tables
WHERE table_name IN ('uuid_test', 'int_test');
```

**Expected observation**: `uuid_test` will show much higher `data_free` (fragmented space) and slower insert time compared to `int_test`. This is because UUID-keyed inserts must find the correct leaf page (random I/O) and frequently trigger page splits.

---

## 6. Key Learnings

### Architectural Lessons

1. **Clustered storage is a bet on access patterns**: InnoDB's clustered primary key is excellent when your dominant access is by primary key (common in most OLTP: `SELECT ... WHERE id = ?`). It becomes a liability when secondary index access dominates, due to the double-lookup overhead.

2. **Undo logs and redo logs solve different problems**: This is counterintuitive at first. The redo log is about durability (replay on crash). The undo log is about read consistency (MVCC) and atomicity (ROLLBACK). They are orthogonal concerns, which is why InnoDB needs both.

3. **Long transactions hurt InnoDB differently than PostgreSQL**: In PostgreSQL, a long transaction prevents VACUUM from cleaning dead tuples (heap bloat). In InnoDB, it prevents the purge thread from cleaning undo logs (undo bloat). The problem is the same — a transaction holding an old read view prevents garbage collection — but the bloat lands in different places.

4. **Gap locks are a trade-off between safety and concurrency**: Gap locks prevent phantom reads but also cause more deadlocks and lock contention than row-only locking. Applications that encounter unexpected deadlocks in InnoDB are often surprised to find that gap locks — not row locks — are the culprit.

5. **The redo log ring size is a critical tuning parameter**: A redo log that's too small is one of the most common causes of InnoDB performance problems in production. With `innodb_log_file_size` too small, the checkpoint stall becomes frequent and write throughput collapses. In modern MySQL, the redo log is dynamically resizable.

### Surprising Observations

- InnoDB's MVCC uses a **read view** (snapshot of active transactions) rather than an LSN-based snapshot. This means two transactions can start at the "same time" and see different data if one started slightly earlier.
- The `DB_ROLL_PTR` column in every InnoDB row is invisible to users but is 7 bytes per row. Combined with `DB_TRX_ID` (6 bytes), every InnoDB row carries 13 bytes of MVCC overhead — regardless of row size.
- MySQL's `SHOW ENGINE INNODB STATUS` output is one of the richest diagnostic tools in any database. It reveals: transaction lock waits, undo history length, buffer pool hit rate, redo log write position, and the purge lag.

---

*References:*
- *InnoDB Source Code: `storage/innobase/` in MySQL source tree*
- *MySQL Documentation: Chapter 15 — The InnoDB Storage Engine*
- *"MySQL Internals Manual" — Oracle*
- *Ramakrishnan, R., Gehrke, J. "Database Management Systems" (3rd ed.) — Chapter 16*
- *Tanenbaum, A. "Modern Operating Systems" — for undo/redo log theory*
