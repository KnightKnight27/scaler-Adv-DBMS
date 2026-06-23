# Topic 3: MySQL / InnoDB Storage Engine

> **Course:** Advanced DBMS | **Name:** Penta Guna Sai Kumar | **Roll Number:** 24BCS10070

---

## Table of Contents
1. [Problem Background](#1-problem-background)
2. [Architecture Overview](#2-architecture-overview)
3. [Internal Design](#3-internal-design)
4. [Design Trade-Offs](#4-design-trade-offs)
5. [Experiments / Observations](#5-experiments--observations)
6. [Directly Answering the Core Study Questions](#6-directly-answering-the-core-study-questions)
7. [Key Learnings](#7-key-learnings)

> All architecture diagrams in this document are original ASCII diagrams authored by me; external material is credited in the References footer.

---

## 1. Problem Background

MySQL began as a simple, fast database in 1995 — prioritizing speed over strict ACID compliance. The original MyISAM storage engine had no transactions, no foreign keys, and no crash recovery. For many web applications of the early 2000s, this was acceptable.

**InnoDB** (developed by Innobase Oy, acquired by Oracle in 2005, bundled with MySQL since 5.5 as the default engine) was created to give MySQL full ACID transactional support while remaining competitive on performance. The design team made a fundamentally different set of choices than PostgreSQL:

- **Clustered indexes** instead of heap files
- **Undo logs** for MVCC instead of keeping old tuple versions in the heap
- **Redo logs** separate from general WAL
- **Row-level locking** with gap locks for range queries

Understanding *why* these choices were made — and how they compare to PostgreSQL — is the core of this document.

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          MySQL Server Layer                             │
│                                                                         │
│  Client → Connection Manager → Thread per connection                    │
│           ↓                                                             │
│  SQL Parser → Query Cache (deprecated 8.0) → Optimizer → Executor       │
│                                                                         │
└───────────────────────────────────┬─────────────────────────────────────┘
                                    │ Storage Engine API (handler.h)
┌───────────────────────────────────▼─────────────────────────────────────┐
│                         InnoDB Storage Engine                           │
│                                                                         │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                      Buffer Pool                                 │   │
│  │  [Data Pages][Index Pages][Undo Pages][Insert Buffer/Change Buf] │   │
│  │  LRU list (young + old sublists) | Flush list | Free list        │   │
│  └────────────────────────────────┬─────────────────────────────────┘   │
│                                   │                                     │
│  ┌────────────────────────────────▼───────────────────────────────────┐ │
│  │  Adaptive Hash Index (AHI) — auto-built on hot index pages         │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                                                         │
│  ┌─────────────────────┐    ┌──────────────────────────────────────┐    │
│  │   Undo Logs         │    │  Redo Log (ib_logfile0, ib_logfile1) │    │
│  │   (rollback segs)   │    │  (circular buffer, MySQL 8: innodb_  │    │
│  │   Old row versions  │    │   redo_log_capacity)                 │    │
│  │   for MVCC          │    │  Write-ahead for crash recovery      │    │
│  └─────────────────────┘    └──────────────────────────────────────┘    │
│                                                                         │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │  .ibd files (tablespace per table, or shared ibdata1)            │   │
│  │  Clustered Index (B+tree keyed on Primary Key)                   │   │
│  │  Secondary Indexes (B+trees, leaf = (secondary_key, PK))         │   │
│  └──────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

### Key Architectural Differences: InnoDB vs PostgreSQL

| Component | InnoDB | PostgreSQL |
|---|---|---|
| Row storage | Clustered index (B+tree) | Heap files (unordered pages) |
| MVCC mechanism | Undo logs | Tuple versioning in heap |
| Old version cleanup | Undo log purge thread | VACUUM |
| Write logging | Redo log (separate) | WAL (unified) |
| Update model | In-place update + undo log | Append new tuple version |
| Locking | Row-level + gap locks | Row-level + predicate locks (serializable) |

---

## 3. Internal Design

### 3.1 Clustered Index

This is InnoDB's defining architectural decision. Unlike PostgreSQL which stores rows in a **heap** (unordered pages), InnoDB stores every row **inside** a B+tree sorted by the primary key. This B+tree is called the **clustered index**.

```
InnoDB Table: orders (PRIMARY KEY = order_id)

Clustered Index B+tree:
                    [Interior: 500 | 1000]
                   /          |           \
          [Interior: 250]  [Interior: 750]  [Interior: 1250]
          /      \              ...
  [Leaf: 1-250]  [Leaf: 251-500]
  ┌─────────────────────────────────────────┐
  │ (order_id=1, user_id=42, amount=99.99,  │
  │  status='SHIPPED', created_at=...)      │  ← actual row data here
  │ (order_id=2, user_id=17, amount=149.00, │
  │  ...)                                   │
  │ ...                                     │
  └─────────────────────────────────────────┘
```

**Why clustered indexes improve lookup performance:**

```sql
SELECT * FROM orders WHERE order_id = 12345;
```

In InnoDB: traverse the B+tree for order_id=12345 → reach the leaf page → **the row is right there**. One lookup, data in hand.

In PostgreSQL: traverse B-tree index for id=12345 → get (page=42, offset=7) → read heap page 42 → fetch row. Two separate I/Os.

For primary key lookups, InnoDB's clustered index eliminates the heap fetch entirely.

**The downside**: If your primary key is a random UUID (not sequential), inserts scatter across the entire B+tree, causing **page splits on every insert** and terrible write performance. Sequential auto-increment PKs are critical for InnoDB performance.

#### Secondary Indexes

Secondary indexes in InnoDB store `(secondary_key_value, primary_key_value)` in their leaf nodes — not a direct heap page pointer.

```
Secondary Index on orders.user_id:
Leaf node: (user_id=42, order_id=1), (user_id=42, order_id=8), ...
                              ↑
                        primary key value — not a page pointer

To fetch full row:
1. Search secondary index for user_id=42 → get order_id=1
2. Search clustered index for order_id=1 → get full row data
```

This is called a **double lookup** or **index lookup + primary key lookup**. It means secondary index scans in InnoDB are slightly more expensive than in PostgreSQL for full row fetches. However, if the query only needs columns in the secondary index (a **covering index**), the second lookup is avoided.

---

### 3.2 Buffer Pool

The InnoDB buffer pool is analogous to PostgreSQL's shared_buffers — it caches data and index pages in memory. But InnoDB's buffer pool management is more sophisticated.

#### LRU with Young/Old Sublists

InnoDB divides the LRU list into two regions:

```
Buffer Pool LRU List:
┌────────────────────────────┬──────────────────────────────┐
│   Young Sublist (~5/8)     │   Old Sublist (~3/8)         │
│   (recently accessed)      │   (new insertions go here)   │
│   Head ← most recently used│   Tail → eviction candidates │
└────────────────────────────┴──────────────────────────────┘
                              ↑
                     midpoint (innodb_old_blocks_pct)
```

When a page is first loaded, it enters the **old sublist** (midpoint). It only graduates to the young sublist if it is accessed again within `innodb_old_blocks_time` (default 1 second). This protects the young sublist from being polluted by large table scans — pages read by a one-time full scan never make it to young, so they don't evict frequently-used data.

#### Change Buffer (formerly Insert Buffer)

When a secondary index page is **not in the buffer pool**, instead of loading the page just to insert an entry, InnoDB buffers the change in the **Change Buffer** (a separate B+tree in the system tablespace). The change is merged later when the page is eventually loaded.

This reduces random I/O for secondary index updates — especially beneficial when the table is much larger than the buffer pool.

---

### 3.3 Undo Logs

**This is InnoDB's MVCC mechanism — fundamentally different from PostgreSQL.**

PostgreSQL keeps old tuple versions **in the heap file** (multiple versions coexist in the same data pages). InnoDB keeps the **current version in the clustered index** and maintains a **chain of undo log records** to reconstruct older versions.

```
InnoDB MVCC for a row update:

Clustered Index Leaf (current state):
┌──────────────────────────────────────────────────────┐
│ order_id=1, status='SHIPPED', amount=99.99           │
│ trx_id=1050  (last modifier)                         │
│ roll_ptr → undo log record                           │
└──────────────────────────────────────────────────────┘
                        │
                        ▼ roll_ptr chain
┌──────────────────────────────────────────────────────┐
│ Undo Log Record (in rollback segment)                │
│ "Before update by txn 1050: status='PENDING'"        │
│ roll_ptr → previous undo record                      │
└──────────────────────────────────────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────┐
│ Undo Log Record                                      │
│ "Before insert by txn 900: row did not exist"        │
└──────────────────────────────────────────────────────┘
```

When a transaction with an older snapshot reads order_id=1:
1. Read current version from clustered index
2. Check trx_id=1050 — is it visible to my snapshot?
3. If NO: follow roll_ptr to undo log, reconstruct the previous version
4. Repeat until a visible version is found

**Advantage over PostgreSQL**: Data pages only hold the current version — no dead tuples to vacuum from the main table. The table doesn't bloat from MVCC overhead.

**Disadvantage**: Undo log I/O adds latency for long-running transactions that need to read many old versions. The undo log itself can grow very large if a long transaction holds a snapshot open.

#### Undo Log Purge

InnoDB's **purge thread** is the equivalent of PostgreSQL's VACUUM. It runs in the background and removes undo log records that are no longer needed by any active transaction. Unlike VACUUM, the purge doesn't touch the main data pages — it only cleans the undo log tablespace. This is why InnoDB tables don't suffer from the same table bloat problem as PostgreSQL.

---

### 3.4 Redo Log

The redo log is InnoDB's crash recovery mechanism. It is a **circular buffer** on disk (`ib_logfile0`, `ib_logfile1` in pre-8.0; `#ib_redo*` files in 8.0+).

```
Redo Log Flow:
1. Transaction begins modifying data pages in buffer pool
2. Every modification generates a redo log record
3. Redo log records written to redo log buffer (in memory)
4. On COMMIT: redo log buffer flushed to redo log files (fdatasync)
5. Data pages in buffer pool are written to .ibd files lazily (by background flush)
6. On crash: replay redo log from last checkpoint → reconstruct unflushed pages
```

**Why both undo AND redo?**

This confuses many students. They serve entirely different purposes:

| Log | Purpose | When used |
|---|---|---|
| **Redo log** | Crash recovery — replay committed changes that weren't flushed | On startup after crash |
| **Undo log** | MVCC + Rollback — provide old versions for readers, undo uncommitted writes | During normal operation (reads + aborts) |

A transaction that commits needs its changes to be durable → redo log ensures they survive a crash. A transaction that aborts needs its partial changes rolled back → undo log provides the "before image" to restore. These are fundamentally different problems requiring different data structures.

---

### 3.5 Row-Level Locking and Gap Locks

InnoDB implements row-level locking — not page-level or table-level. This enables high concurrency for OLTP workloads.

#### Lock Types

- **Record Lock**: locks a specific index record. `SELECT ... FOR UPDATE` acquires an exclusive record lock on matching rows.
- **Gap Lock**: locks the gap *between* index records (or before the first / after the last). Prevents INSERTs into the gap.
- **Next-Key Lock**: combination of record lock + gap lock on the preceding gap. This is the default lock for range queries.
- **Intention Locks**: table-level locks that signal intent to acquire row-level locks (IX for exclusive, IS for shared).

#### Why Gap Locks Exist

```sql
-- Session 1:
BEGIN;
SELECT * FROM orders WHERE amount BETWEEN 100 AND 200 FOR UPDATE;
-- InnoDB places next-key locks on all records in [100, 200]
-- AND on the gap (200, next_value)

-- Session 2:
INSERT INTO orders VALUES (150, ...);  -- BLOCKED by gap lock
-- Without gap locks, Session 2's INSERT would be visible to Session 1
-- on a second read → "phantom read" → violates REPEATABLE READ
```

Gap locks prevent **phantom reads** — a transaction reading a range gets the same set of rows on every read within the transaction, because no new rows can be inserted into that range while the gap lock is held.

PostgreSQL uses **predicate locks** (for SERIALIZABLE isolation) or relies on MVCC snapshots (for REPEATABLE READ) to prevent phantoms differently.

---

### 3.6 Transaction Processing and Isolation Levels

InnoDB supports all 4 SQL isolation levels:

| Level | Dirty Read | Non-Repeatable Read | Phantom Read | InnoDB Mechanism |
|---|---|---|---|---|
| READ UNCOMMITTED | Possible | Possible | Possible | No version checking |
| READ COMMITTED | No | Possible | Possible | Snapshot per statement |
| REPEATABLE READ (default) | No | No | No* | Snapshot at txn start + next-key locks |
| SERIALIZABLE | No | No | No | All reads become locking reads |

*InnoDB's REPEATABLE READ actually prevents phantoms in most cases via next-key locks — stronger than the SQL standard requires.

---

## 4. Design Trade-Offs

### Clustered Index

| Advantage | Limitation |
|---|---|
| PK lookups need only 1 B+tree traversal (no heap fetch) | Random PK (UUID) causes scattered inserts → page fragmentation |
| Range scans on PK are sequential reads | All secondary indexes store PK → large secondary indexes if PK is big |
| No separate heap file to maintain | Updating PK requires moving the entire row in the B+tree (expensive) |

**PostgreSQL's approach**: Heap files allow out-of-order inserts naturally. Indexes are separate. UPDATE uses append-only model (no B+tree movement). The trade-off is one extra I/O for index lookups vs. InnoDB.

### Undo Log MVCC

| Advantage | Limitation |
|---|---|
| Main data pages don't accumulate dead tuples | Undo log can grow large with long-running transactions |
| No VACUUM required on main tables | MVCC reads of old data require undo log traversal (slower for very old data) |
| Undo log purge is independent of table access | Undo tablespace still needs management and purge monitoring |

**PostgreSQL's approach**: Dead tuples in heap pages are simple to reason about (just scan and reclaim). But VACUUM must visit data pages, not just a separate log. Large dead-tuple accumulation degrades every query touching those pages.

### Redo Log Architecture

| Advantage | Limitation |
|---|---|
| Separate from general data files — can be placed on fast storage | Fixed-size circular buffer (pre-8.0: needed careful sizing) |
| Write-combining reduces fsync frequency | Not used for replication (MySQL uses binlog separately) |
| Fast sequential writes | Two-phase commit with binlog adds overhead for replication |

**PostgreSQL's approach**: Single WAL stream serves both crash recovery AND replication. Simpler architecture but can't independently tune recovery vs. replication performance.

---

## 5. Experiments / Observations

> **Environment:** MariaDB 11.8 (InnoDB engine, API-compatible with MySQL 8) | Database: `advdbms_innodb` | Schema: `users_m` (10K), `products_m` (2K), `orders_m` (100K)

### Experiment 1: Clustered Index — EXPLAIN Output

**Query 1: Primary key (clustered index) lookup**
```sql
EXPLAIN SELECT * FROM orders_m WHERE id = 50000;
```

**Actual output:**
```
+----+-------------+----------+-------+---------------+---------+---------+-------+------
| id | select_type | table    | type  | possible_keys | key     | key_len | ref   | rows | Extra |
+----+-------------+----------+-------+---------------+---------+---------+-------+------
|  1 | SIMPLE      | orders_m | const | PRIMARY       | PRIMARY | 4       | const |    1 |       |
+----+-------------+----------+-------+---------------+---------+---------+-------+------
```
`type=const` — single row lookup via clustered B+ tree, data and row co-located.

**Query 2: Secondary index lookup**
```sql
EXPLAIN SELECT id, user_id, status FROM orders_m WHERE user_id = 100;
```

**Actual output:**
```
+----+-------------+----------+------+--------------------+--------------------+---------
| id | select_type | table    | type | possible_keys      | key                | key_len | ref   | rows | Extra |
+----+-------------+----------+------+--------------------+--------------------+---------
|  1 | SIMPLE      | orders_m | ref  | idx_orders_user_id | idx_orders_user_id | 5       | const |   11 |       |
+----+-------------+----------+------+--------------------+--------------------+---------
```
`type=ref` — secondary index traversal followed by clustered index lookup (double-read) for each match.

**Query 3: Covering index (no heap/clustered lookup)**
```sql
EXPLAIN SELECT id, status FROM orders_m WHERE status = 'PENDING';
```

**Actual output:**
```
+----+-------------+----------+------+--------------------+--------------------+---------
| id | select_type | table    | type | possible_keys      | key                | key_len | ref   | rows  | Extra       |
+----+-------------+----------+------+--------------------+--------------------+---------
|  1 | SIMPLE      | orders_m | ref  | idx_orders_status  | idx_orders_status  | 83      | const | 52270 | Using index |
+----+-------------+----------+------+--------------------+--------------------+---------
```
`Using index` — covering index: query satisfied from index alone, no clustered B+ tree traversal.

**Query 4: Full table scan (no usable index)**
```sql
EXPLAIN SELECT * FROM orders_m WHERE total_amount > 500;
```

**Actual output:**
```
+----+-------------+----------+------+---------------+------+---------+------+-------
| id | select_type | table    | type | possible_keys | key  | key_len | ref  | rows  | Extra       |
+----+-------------+----------+------+---------------+------+---------+------+-------
|  1 | SIMPLE      | orders_m | ALL  | NULL          | NULL | NULL    | NULL | 99883 | Using where |
+----+-------------+----------+------+---------------+------+---------+------+-------
```
`type=ALL` — full scan of all 99,883 rows. Optimizer correctly chose no index (high selectivity, ~50% rows match).

---

### Experiment 2: Redo Log — Commit Latency (`innodb_flush_log_at_trx_commit`)

`innodb_flush_log_at_trx_commit` controls how aggressively InnoDB flushes the redo log on each transaction commit.

**Benchmark: 1,000-row bulk INSERT, then 200 individual single-row INSERTs**

```
flush=1 (fdatasync on every commit):   bulk=54.3ms | individual 200 inserts=5734ms (35 TPS)
flush=2 (write to OS cache, 1s flush): bulk=48.0ms | individual 200 inserts=5619ms (36 TPS)
Speedup (bulk): 1.13x
```

**Interpretation:** On a VM/SSD environment where fdatasync is fast, the difference between flush=1 and flush=2 is small (~13% for bulk, ~2% for individual). On spinning disk or high-latency storage, this difference can be 5–10x. flush=1 is the only option that guarantees no data loss on crash. flush=2 risks losing up to 1 second of committed transactions. Many production deployments use flush=2 in replicated setups where the replica provides the durability guarantee.

---

### Experiment 3: Gap Locks

**Setup:**
```sql
DROP TABLE IF EXISTS gap_test;
CREATE TABLE gap_test (id INT PRIMARY KEY, val VARCHAR(20)) ENGINE=InnoDB;
INSERT INTO gap_test VALUES (10,'a'),(20,'b'),(30,'c'),(40,'d'),(50,'e');
```

**Range query with FOR UPDATE (acquires gap locks):**
```sql
EXPLAIN SELECT * FROM gap_test WHERE id BETWEEN 15 AND 25 FOR UPDATE;
```

**Actual output:**
```
+----+-------------+-----------+-------+---------------+---------+---------+------+------
| id | select_type | table     | type  | possible_keys | key     | key_len | ref  | rows | Extra       |
+----+-------------+-----------+-------+---------------+---------+---------+------+------
|  1 | SIMPLE      | gap_test  | range | PRIMARY       | PRIMARY | 4       | NULL |    1 | Using where |
+----+-------------+-----------+-------+---------------+---------+---------+------+------
```

**Result from FOR UPDATE:**
```
id | val
20 | b
```

**Explanation of gap locking behavior:**
```
When session A runs: SELECT * FROM gap_test WHERE id BETWEEN 15 AND 25 FOR UPDATE;

InnoDB acquires:
  - Gap lock on (10, 20)   → no new rows can be inserted with id ∈ (10,20)
  - Record lock on id=20   → row 20 is exclusively locked
  - Gap lock on (20, 30)   → no new rows can be inserted with id ∈ (20,30)

Concurrent session B: INSERT INTO gap_test VALUES (18,'x');  → BLOCKED
Concurrent session B: INSERT INTO gap_test VALUES (22,'y');  → BLOCKED
Concurrent session B: INSERT INTO gap_test VALUES (35,'z');  → ALLOWED (outside gap)
```

Gap locks prevent phantom reads in REPEATABLE READ but are a common source of deadlocks in high-concurrency applications. Switching to READ COMMITTED eliminates gap locks at the cost of allowing phantom reads.

---

### Experiment 4: Buffer Pool Efficiency

**After warming the buffer pool with several queries:**
```sql
SELECT COUNT(*) FROM orders_m WHERE status = 'DELIVERED';    -- 25070
SELECT SUM(total_amount) FROM orders_m WHERE user_id BETWEEN 1 AND 1000;  -- 5091635.72
```

```sql
SHOW STATUS LIKE 'Innodb_buffer_pool%';
```

**Actual output (selected rows):**
```
Innodb_buffer_pool_read_requests      | 1162754
Innodb_buffer_pool_reads              | 174
Innodb_buffer_pool_pages_data         | 1312
Innodb_buffer_pool_pages_free         | 6800
Innodb_buffer_pool_pages_total        | 8112
Innodb_buffer_pool_bytes_data         | 21495808   (~20.5 MB)
Innodb_buffer_pool_bytes_dirty        | 17580032   (~16.8 MB dirty)
```

**Calculated hit ratio:**
```
Hit ratio = 1 - (reads / read_requests)
          = 1 - (174 / 1162754)
          = 99.985%
```

**InnoDB Engine Status:**
```
Buffer pool hit rate 1000 / 1000
Free buffers: 6800 / 8112 pages (83.8% free — dataset fits comfortably in pool)
History list length: 0  (purge thread is keeping up with undo log cleanup)
```

---

### Experiment 5: Undo Log Growth Under Long Transaction

InnoDB keeps undo records to support MVCC reads and rollbacks. A long-running transaction prevents the **purge thread** from cleaning the undo log.

```sql
BEGIN;
UPDATE orders_m SET status = 'PROCESSING' WHERE id BETWEEN 1 AND 10000;
UPDATE orders_m SET status = 'REVIEWING'  WHERE id BETWEEN 1 AND 10000;
UPDATE orders_m SET status = 'APPROVED'   WHERE id BETWEEN 1 AND 10000;
-- (transaction left open — simulating a long-running txn)

-- InnoDB STATUS shows:
-- History list length 3  (3 update rounds not yet purged)
-- Purge done for trx's n:o < 924 undo n:o < 0 state: running but idle

ROLLBACK;
-- After rollback: history list returns to 0
```

**Observation from `SHOW ENGINE INNODB STATUS`:**
```
TRANSACTIONS
Trx id counter 925
Purge done for trx's n:o < 924 undo n:o < 0 state: running but idle
History list length 0
```

**Interpretation:** Each UPDATE generates undo records. While a transaction is open, InnoDB cannot purge those records — other transactions may need them for MVCC reads. If a transaction stays open for hours during heavy write activity, the history list length can reach hundreds of thousands, forcing all MVCC reads to traverse a long undo chain to find the correct tuple version — degrading read performance system-wide, even for unrelated queries.

---

## 6. Directly Answering the Core Study Questions

**Q: Why does InnoDB need both undo and redo logs?**
They solve opposite problems. **Redo** is *roll-forward*: it records the physical change to a page so that committed-but-unflushed work can be replayed after a crash — it provides Durability. **Undo** is *roll-back + read-back*: it records the *before-image* so an aborting transaction can be reversed, and so MVCC readers can reconstruct an older version visible to their snapshot. Redo answers "what did I promise to keep?"; undo answers "what must I be able to take back / look back at?". You cannot get both crash-durability and non-blocking MVCC from a single log.

**Q: What advantages do clustered indexes provide?**
The primary-key lookup *is* the row fetch — one B+tree traversal lands directly on the row, eliminating the separate heap I/O PostgreSQL needs. Range scans on the PK become sequential reads of adjacent leaf pages (great for `WHERE id BETWEEN ...` and pagination), and there is no separate heap file to maintain. The cost: secondary indexes must store the PK (so a fat PK bloats every index), random PKs (UUIDs) scatter inserts and cause page splits, and changing a PK physically relocates the row.

**Q: Why did PostgreSQL choose a different MVCC model?**
PostgreSQL keeps versions **in the heap** rather than in undo segments largely for **extensibility and simplicity of abort**. A uniform heap means any index access method (B-tree, GiST, GIN, BRIN) and any user-defined type works without understanding an undo subsystem — central to Postgres's pluggable design. Rollback is also cheap: an aborted transaction's tuples simply become invisible (no undo replay), whereas InnoDB must apply undo records to reverse in-place changes. The price PostgreSQL pays is dead-tuple bloat and the VACUUM obligation; InnoDB instead pays with undo-segment growth and purge lag. Same correctness guarantee, opposite cleanup location.

---

## 7. Key Learnings

1. **Clustered index is both a performance gift and a constraint**: PK lookups are faster than PostgreSQL, but the choice of primary key (sequential vs. random) has massive implications for insert performance and index fragmentation. Always use AUTO_INCREMENT or sequential IDs as PKs in InnoDB.

2. **Undo and redo logs solve different problems**: This is the most commonly confused aspect of InnoDB. Redo = crash recovery (what was committed but not flushed). Undo = MVCC + rollback (what wasn't committed but was partially written). Both are necessary.

3. **MVCC without heap bloat, but with undo log complexity**: InnoDB avoids PostgreSQL's dead-tuple problem but shifts the cleanup burden to the undo log. A forgotten long-running transaction in InnoDB can cause undo log growth that degrades reads far more subtly than PostgreSQL's table bloat.

4. **Gap locks are unintuitive but necessary for REPEATABLE READ**: They prevent phantoms but introduce lock contention that surprises developers. Many production MySQL deployments use READ COMMITTED to eliminate gap locks, accepting the trade-off.

5. **The buffer pool is the most important tuning knob**: Like PostgreSQL's shared_buffers, InnoDB's `innodb_buffer_pool_size` has the highest single-parameter impact on performance. The Young/Old sublist design is a clever solution to the sequential scan cache-pollution problem.

6. **InnoDB and PostgreSQL make the same fundamental trade-offs differently**: Both achieve ACID. Both use MVCC. Both use write-ahead logging. The *implementations* differ in ways that make InnoDB faster for PK-heavy OLTP and more prone to issues with long transactions, while PostgreSQL is more flexible for complex queries but requires careful VACUUM management.

---

*References: MySQL 8.0 Reference Manual (InnoDB Architecture); "How MySQL Actually Works" (dev.to); "Deep Dive into MySQL InnoDB Tablespace Architecture" (Medium); "MySQL InnoDB Internals" (YouTube, Hussein Nasser); MySQL EXPLAIN documentation; Percona blog on InnoDB buffer pool and undo logs*