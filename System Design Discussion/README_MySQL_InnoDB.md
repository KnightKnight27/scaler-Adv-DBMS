# MySQL / InnoDB Storage Engine Architecture

## 1. Problem Background

InnoDB was not MySQL's original storage engine. MySQL originally shipped with MyISAM, which had a fatal flaw: no transactions. MyISAM didn't support ACID guarantees — if your server crashed mid-write, you lost data. For web applications in the late 1990s, this was acceptable. For anything involving money or critical data, it was not.

InnoDB (created by Heikki Tuuri at Innobase Oy, Finland, in 1995) was built to bring Oracle-style transactional semantics to MySQL. The key word is "Oracle-style" — InnoDB's MVCC mechanism, undo log approach, and clustered index design are explicitly inspired by Oracle's architecture. When Oracle acquired Sun (and MySQL) in 2010, there was a certain irony: Oracle now owned a system that had been designed to replicate Oracle's architecture on cheaper hardware.

Today, InnoDB is the default and essentially mandatory MySQL storage engine. Understanding it means understanding MySQL.

The core problem InnoDB solves: **How do you support concurrent transactions with ACID guarantees while maintaining high performance for the mixed read-write workloads typical of web applications?**

---

## 2. Architecture Overview

### InnoDB Architecture Diagram

```
[MySQL Server Layer]
  SQL Parser → Query Cache → Optimizer → Executor
                                              │
                    ┌─────────────────────────▼──────────────────────────┐
                    │              InnoDB Storage Engine                  │
                    │                                                     │
                    │  ┌──────────────────────────────────────────────┐  │
                    │  │              Buffer Pool                      │  │
                    │  │  ┌────────────┐  ┌──────────┐  ┌─────────┐  │  │
                    │  │  │ Data Pages │  │Index Pgs │  │Undo Logs│  │  │
                    │  │  │(clustered) │  │(secondary│  │(rollback│  │  │
                    │  │  │            │  │ index)   │  │ segs)   │  │  │
                    │  │  └────────────┘  └──────────┘  └─────────┘  │  │
                    │  │  ┌────────────┐  ┌──────────┐               │  │
                    │  │  │Adaptive    │  │Insert    │               │  │
                    │  │  │Hash Index  │  │Buffer    │               │  │
                    │  │  └────────────┘  └──────────┘               │  │
                    │  └──────────────────────────────────────────────┘  │
                    │                                                     │
                    │  ┌─────────────┐   ┌────────────────────────────┐ │
                    │  │  Log Buffer │──▶│  Redo Log (ib_logfile0/1)  │ │
                    │  └─────────────┘   └────────────────────────────┘ │
                    │                                                     │
                    │  Background Threads:                                │
                    │  Master Thread │ Purge Thread │ Page Cleaner       │
                    │  Read-Ahead   │ Write Threads│ Undo Purge          │
                    └────────────────────────────┬────────────────────────┘
                                                 │
                                    ┌────────────▼────────────┐
                                    │    .ibd files (per table)│
                                    │    ibdata1 (system tbs)  │
                                    └─────────────────────────┘
```

---

## 3. Internal Design

### 3.1 Clustered Indexes: The Core InnoDB Idea

This is the most important architectural decision in InnoDB, and the one most different from PostgreSQL.

**In InnoDB, the primary key IS the table.** There is no separate "heap" file where rows live. Rows are stored in the leaf pages of a B+ tree ordered by primary key. This is called a **clustered index**.

```
InnoDB Clustered Index (PRIMARY KEY = id):

Internal Pages:
         [100 | 500 | 900]          ← key boundaries
        /         |         \
    [1-99]    [100-499]   [500-899]  ← child page pointers

Leaf Pages (actual row data lives here):
┌──────────────────────────────────────────────────┐
│ id=1, name='Alice', email='a@ex.com', balance=500│
│ id=2, name='Bob',   email='b@ex.com', balance=300│
│ ...                                              │
│ id=99, name='Zara', email='z@ex.com', balance=100│
│ prev_page_ptr ←──────────────────── next_page_ptr│
└──────────────────────────────────────────────────┘
```

**Implications of Clustered Storage:**

1. **Range scans are extremely fast.** `SELECT * FROM orders WHERE id BETWEEN 1000 AND 2000` reads sequentially through leaf pages — no random I/O.

2. **Primary key lookups are single B-tree traversal.** The row is at the leaf.

3. **Primary key choice matters enormously.** Auto-increment integers are ideal: new rows always append to the rightmost leaf, no page splits. UUIDs as primary keys cause random inserts across the entire tree, fragmenting pages and causing constant splits — a well-known InnoDB performance pitfall.

4. **The clustered index is always there.** If you don't define a PRIMARY KEY, InnoDB creates a hidden 6-byte `rowid` column. This matters because it consumes space.

**PostgreSQL Comparison:** PostgreSQL stores rows in unordered heap files. The concept of a clustered index exists (`CLUSTER` command) but it's a one-time operation, not maintained on insert. PostgreSQL's heap model is more flexible (any column can be indexed efficiently) but loses the range-scan locality benefit of InnoDB's clustered model.

### 3.2 Secondary Indexes

Secondary indexes in InnoDB work differently from primary key indexes because rows are not stored in them.

```
Secondary Index on (email):

Leaf pages contain:
┌──────────────────────────────────────┐
│ email='a@ex.com'  → pk_value = 1    │  ← NOT a pointer to physical page
│ email='b@ex.com'  → pk_value = 2    │
│ email='z@ex.com'  → pk_value = 99   │
└──────────────────────────────────────┘
```

Secondary index leaf pages store the **primary key value**, not a pointer to a physical row location (ctid in PostgreSQL terms). 

**Secondary index lookup path:**
```
Query: SELECT * FROM users WHERE email = 'b@ex.com'

Step 1: Search secondary index (email) → find pk_value = 2
Step 2: Search clustered index (id=2) → find actual row
         (two B-tree traversals)
```

This double lookup is called a **"bookmark lookup"** or **"clustered index lookup."** It's slightly more expensive than PostgreSQL's ctid-based lookup for secondary indexes, but it has a critical advantage: **row moves (due to updates) don't invalidate secondary indexes.** In PostgreSQL, moving a row to a new ctid requires updating all secondary indexes. In InnoDB, because secondary indexes store the PK value (which doesn't change on row move), they remain valid.

**Covering indexes:** If a query only needs columns that are in the secondary index + the primary key, InnoDB can avoid the bookmark lookup entirely. This is called an "index-covering" optimization and is a significant performance technique.

### 3.3 Buffer Pool

InnoDB's buffer pool is analogous to PostgreSQL's shared_buffers: an in-memory page cache for recently accessed data and index pages.

**Buffer Pool Organization:**

```
Buffer Pool:
┌─────────────────────────────────────────────────────┐
│  LRU List (Least Recently Used eviction)            │
│  ┌──────────────────────────────────────────────┐   │
│  │  New Sublist (5/8 of pool)  │ Old Sublist    │   │
│  │  [hot pages, recently used] │ [cold pages]   │   │
│  │                             │ [eviction end] │   │
│  └──────────────────────────────────────────────┘   │
│                                                     │
│  Free List: available buffer slots                  │
│  Flush List: dirty pages (need writing to disk)     │
└─────────────────────────────────────────────────────┘
```

InnoDB uses a **midpoint insertion LRU** strategy. New pages enter at the midpoint (boundary of new/old sublist), not at the head. This prevents table scans (which read every page once) from flushing the entire hot cache. Pages only "graduate" to the new sublist if they're accessed again within `innodb_old_blocks_time` milliseconds.

**Adaptive Hash Index:** InnoDB monitors which B-tree lookups are frequent and automatically builds a hash index in memory for those access patterns. This converts B-tree lookups (O(log N), multiple page accesses) to O(1) hash lookups for hot keys. It's transparent — you don't configure it, InnoDB manages it automatically. PostgreSQL has no equivalent.

### 3.4 Undo Logs: Oracle-Style MVCC

This is the most important architectural difference from PostgreSQL's MVCC.

**PostgreSQL approach:** Keep multiple versions of a tuple in the heap. Old versions sit in the heap until VACUUM reclaims them.

**InnoDB approach:** Store only the current version in the clustered index (in-place update). Keep old versions in a separate **undo log**. To read an old version, reconstruct it by applying undo records backwards.

```
InnoDB MVCC Read (transaction T3 needs to see row as of T1):

Current clustered index page:
  id=1, name='Charlie', trx_id=T3, roll_ptr → undo log

Undo Log (rollback segment):
  undo record 1: [trx_id=T3, prev=undo record 0, before: name='Bob']
  undo record 0: [trx_id=T2, prev=null, before: name='Alice']

T1 wants the row → follow roll_ptr chain until trx_id ≤ T1_start
  T3 → too new; T2 → still too new; T1's version = 'Alice'
```

**Key insight:** InnoDB reconstructs old versions on-demand by traversing the undo chain. This is called "consistent read" view reconstruction.

**Advantages:**
- Current data is always in the main clustered index — no heap scan needed for fresh reads
- No VACUUM needed for old row versions — undo log is purged by a background "purge thread"
- More predictable write amplification — updates are in-place, not appended

**Disadvantages:**
- Long-running transactions hold undo log space for the duration of the transaction
- Reconstructing old versions of heavily-updated rows is expensive (long undo chain traversal)
- Undo log space exhaustion (`innodb_undo_tablespace`) can cause transaction aborts

### 3.5 Redo Logs

InnoDB's redo log is analogous to PostgreSQL's WAL: it ensures durability.

```
Write Path:
  Transaction commits
       │
       ▼
  Log Buffer (in memory)
       │ flush on commit (or innodb_flush_log_at_trx_commit setting)
       ▼
  ib_logfile0, ib_logfile1 (circular redo log files)
       │ asynchronously
       ▼
  .ibd files (actual tablespace pages)
```

**Why both undo and redo logs?** They serve different purposes:
- **Redo log** = durability (replay changes after crash)
- **Undo log** = rollback + MVCC (reverse uncommitted changes, reconstruct old versions)

PostgreSQL uses only WAL for durability. Old versions live in the heap (no separate undo log). InnoDB's approach requires maintaining both, but separating them allows in-place updates in the main data file.

**`innodb_flush_log_at_trx_commit` setting:**
- `1` (default): fsync on every commit — fully durable, slower
- `2`: write to OS buffer on commit, fsync every second — lose last second of data on OS crash
- `0`: write to log buffer only — fastest, risk losing last second on MySQL crash

This is a deliberate durability vs performance trade-off the user controls.

### 3.6 Locking: Row-Level Locks and Gap Locks

InnoDB implements **row-level locking** — dramatically more granular than MyISAM's table locks or SQLite's file locks.

**Lock Types:**
```
S  (Shared)     — multiple txns can hold; for reading
X  (Exclusive)  — single txn; for writing
IS (Intention Shared)  — "I intend to acquire S on a row"
IX (Intention Exclusive) — "I intend to acquire X on a row"
```

**Gap Locks:** This is unique to InnoDB.

```sql
-- Transaction A:
SELECT * FROM orders WHERE id BETWEEN 10 AND 20 FOR UPDATE;
-- This locks not just rows 10-20, but the GAP before row 10
-- and the gap after row 20.

-- Transaction B:
INSERT INTO orders VALUES (15, ...);  -- BLOCKED by gap lock
```

Gap locks prevent **phantom reads** — the scenario where a transaction reads a range twice and gets different rows because another transaction inserted in between. Gap locks ensure that under REPEATABLE READ isolation, range queries are stable. The trade-off: gap locks reduce concurrency, especially for range queries.

**NEXT-KEY Locks:** In practice, InnoDB uses next-key locks = record lock + gap lock on the gap before the record. This is the default locking mode for most indexed operations under REPEATABLE READ.

---

## 4. Design Trade-offs

### Clustered Index Trade-offs

**Benefit:** Range scans on the primary key are locality-optimized. Rows are physically adjacent, minimizing I/O.

**Cost:** If your access pattern doesn't align with the primary key (e.g., you mostly query by `email`, not `id`), secondary index lookups always incur a double B-tree traversal. Choosing the wrong primary key (e.g., UUIDs) causes insert fragmentation. There's no escape from this — the physical layout is determined by the PK at table creation.

PostgreSQL's heap is more flexible: every index is equally a secondary index (they all use ctids), so no one column has physical storage priority.

### In-Place Update vs. Append-Only

| | InnoDB | PostgreSQL |
|---|---|---|
| Update mechanism | In-place + undo log | Append new tuple to heap |
| Old version location | Undo log (separate) | Heap (same page or nearby) |
| Current data access | Always fast (clustered) | Fast (heap) |
| Historical version access | Traverse undo chain | Read heap tuples |
| Cleanup mechanism | Purge thread | VACUUM |
| Long-running transactions | Hold undo log space | Hold heap space (bloat) |

For workloads with many small updates to "hot" rows (e.g., account balances), InnoDB's in-place model is more cache-friendly — the current value is always at the same location. PostgreSQL will scatter updated versions across heap pages.

### Row Locking vs. MVCC Reads

InnoDB uses both MVCC (for consistent reads) and explicit locking (for current reads with `SELECT ... FOR UPDATE`). This dual mechanism is sometimes confusing: a plain `SELECT` uses the MVCC snapshot and never blocks, but `SELECT ... FOR UPDATE` acquires row locks and can cause deadlocks. PostgreSQL's model is similar but the lock granularity and gap lock behavior differ.

### Undo Log Scalability

Long-running transactions in InnoDB are dangerous because they hold undo log space for all changes made since the transaction started (across the entire instance). A 6-hour transaction preventing undo purge can cause undo tablespace to grow to dozens of GBs. PostgreSQL has a similar problem with dead tuple accumulation, but it's distributed per-table rather than centralized.

---

## 5. Experiments / Observations

### Clustered Index Range Scan

```sql
EXPLAIN SELECT * FROM orders WHERE id BETWEEN 1000 AND 2000;
-- Expected: type=range, key=PRIMARY
-- InnoDB reads leaf pages sequentially → very fast

EXPLAIN SELECT * FROM orders WHERE email BETWEEN 'a@' AND 'm@';
-- Expected: type=range on secondary index + bookmark lookup
-- May be slower due to double traversal
```

### Gap Lock Demonstration

```sql
-- Session A:
BEGIN;
SELECT * FROM products WHERE price BETWEEN 10 AND 20 FOR UPDATE;

-- Session B (concurrent):
INSERT INTO products VALUES (99, 'Widget', 15.00);
-- This BLOCKS until Session A commits — gap lock in action
```

### Undo Log Growth Under Long Transaction

```sql
-- Monitor undo log usage:
SELECT name, count FROM information_schema.INNODB_METRICS
WHERE name LIKE 'trx_rseg_history%';
-- History list length grows with uncommitted/unpurged transactions
-- High history list length → slow secondary index reads (undo traversal)
```

### Buffer Pool Hit Rate

```sql
SHOW STATUS LIKE 'Innodb_buffer_pool_read%';
-- Innodb_buffer_pool_reads: pages read from disk
-- Innodb_buffer_pool_read_requests: total page requests
-- Hit rate = 1 - (reads / read_requests)
-- Target: > 99% for well-tuned systems
```

---

## 6. Key Learnings

**The clustered index is not just an optimization — it's a fundamental storage decision.** Once you set the primary key, the physical layout of your data is determined. This makes InnoDB much more sensitive to primary key choice than PostgreSQL. Auto-increment integers are the canonical best practice not out of convention, but because they guarantee sequential inserts with no page splits.

**InnoDB needs two logs because it made different trade-offs than PostgreSQL.** The undo log exists because InnoDB chose in-place updates (to avoid VACUUM-style bloat). The redo log exists for crash safety (same purpose as PostgreSQL's WAL). Understanding why both are needed — and what each prevents — is the key insight into InnoDB's architecture.

**MVCC and locking are complementary, not alternatives.** InnoDB uses MVCC for reads (non-blocking) and row/gap locks for writes (coordinated blocking). Mixing them correctly requires understanding when InnoDB takes which approach. `SELECT` uses MVCC snapshot; `SELECT FOR UPDATE` takes row locks. This distinction causes many subtle bugs in application code.

**Gap locks are a pragmatic solution to a real problem.** Phantom reads in REPEATABLE READ are prevented by gap locks, but at the cost of reduced insert concurrency. Many applications set `transaction_isolation = READ-COMMITTED` to eliminate gap locks and improve insert throughput, accepting that phantom reads are possible. This is a valid engineering trade-off once you understand what you're giving up.

**Buffer pool sizing and secondary index design are the primary InnoDB performance levers.** Unlike PostgreSQL where VACUUM tuning is a major concern, InnoDB's main operational knobs are: buffer pool size, primary key design, secondary index coverage, and undo log retention. Getting these right matters more than almost any other configuration.

---

*References: MySQL 8.0 Reference Manual (dev.mysql.com), InnoDB source code (github.com/mysql/mysql-server), "High Performance MySQL" (Baron Schwartz et al.), Jeremy Cole's InnoDB deep-dive blog (blog.jcole.us)*
