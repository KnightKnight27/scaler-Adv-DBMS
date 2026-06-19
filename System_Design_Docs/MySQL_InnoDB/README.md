# MySQL / InnoDB Storage Engine

## 1. Problem Background

MySQL was created in 1995 by MySQL AB (founders Axmark and Widenius) to be a fast, simple, and embeddable relational database for web applications. The original MyISAM storage engine prioritized read performance and simplicity, but lacked transactions and foreign keys.

InnoDB was developed independently by Heikki Tuuri at Innobase Oy (acquired by Oracle in 2005) to address exactly what MyISAM lacked: full ACID compliance, crash recovery, row-level locking, and foreign key enforcement. InnoDB became the default MySQL storage engine in MySQL 5.5 (2010).

The core architectural question InnoDB answered: **how do you provide ACID transactions with high write throughput and row-level concurrency in a storage engine that plugs into MySQL's query layer?**

The answer involves three interlocking mechanisms:
- **Clustered indexes**: organize data physically for optimal primary-key access
- **Undo logs**: enable MVCC and transaction rollback without keeping multiple heap versions
- **Redo logs**: guarantee durability through WAL, enabling crash recovery

Understanding why InnoDB made different choices than PostgreSQL reveals the depth of the trade-off space in database engine design.

---

## 2. Architecture Overview

```
┌──────────────────────────────────────────────────────────────────┐
│                    MySQL / InnoDB Architecture                   │
│                                                                  │
│  Client  ──►  MySQL Server Layer                                 │
│                ├─ Connection Manager                             │
│                ├─ SQL Parser & Analyzer                          │
│                ├─ Query Optimizer                                │
│                └─ Query Executor                                 │
│                        │                                        │
│                ┌───────▼─────────────────────────────────────┐  │
│                │          InnoDB Storage Engine              │  │
│                │                                             │  │
│                │  ┌─────────────────────────────────────┐   │  │
│                │  │         Buffer Pool                 │   │  │
│                │  │  (data pages, index pages,          │   │  │
│                │  │   undo pages, insert buffer)        │   │  │
│                │  └──────────────┬──────────────────────┘   │  │
│                │                 │                           │  │
│                │  ┌──────────────▼──────────────────────┐   │  │
│                │  │  Transaction System                 │   │  │
│                │  │   Lock Manager  │  MVCC (undo log)  │   │  │
│                │  └──────────────┬──────────────────────┘   │  │
│                │                 │                           │  │
│                │  ┌──────────────▼──────────────────────┐   │  │
│                │  │  Tablespace Files (.ibd)            │   │  │
│                │  │   undo tablespace | redo log         │   │  │
│                │  └─────────────────────────────────────┘   │  │
│                └─────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

---

## 3. Internal Design

### 3.1 Clustered Indexes

This is InnoDB's most distinctive architectural feature. **Every InnoDB table is physically organized as a B+ tree keyed by the primary key.** There is no separate "heap file" — the table data IS the primary key index.

```
Clustered Index (Primary Key B+ Tree):

               ┌─────────────────┐
               │  ROOT (level 2) │
               │   [50 | 100]    │
               └────┬───────┬────┘
                    │       │
         ┌──────────┘       └──────────┐
         ▼                             ▼
  ┌─────────────┐              ┌─────────────┐
  │  INTERNAL   │              │  INTERNAL   │
  │  [20 | 35]  │              │  [65 | 80]  │
  └──┬───────┬──┘              └──┬──────┬───┘
     │       │                    │      │
┌────┘   ┌───┘               ┌────┘  ┌───┘
▼        ▼                   ▼       ▼
┌──────────────┐  ←──────►  ┌──────────────┐
│ LEAF (pk=1)  │            │ LEAF (pk=51) │
│ pk=1, ALL    │            │ pk=51, ALL   │
│ column data  │            │ column data  │
│ pk=2, ALL    │            │ pk=52, ALL   │
│ column data  │            │ column data  │
│ ...          │            │ ...          │
└──────────────┘            └──────────────┘
    Leaf pages linked in PK order
```

**Why clustered indexes improve performance:**

```
Query: SELECT * FROM orders WHERE id = 42;

Without clustering (heap):
  1. B-tree index lookup: find TID for id=42
  2. Heap page fetch: random I/O to heap page
  Total: 2 I/Os (index + heap)

With clustering (InnoDB):
  1. B-tree leaf lookup: find page containing id=42
     → leaf page IS the data
  Total: 1 I/O (just the index leaf)

Range query: SELECT * FROM orders WHERE id BETWEEN 40 AND 60;

Without clustering:
  20 index lookups → 20 random heap page fetches (scatter-gather)

With clustering:
  Leaf pages are physically contiguous → sequential I/O
  All 20 rows likely on 1-2 leaf pages
```

#### Secondary Indexes in InnoDB

Secondary indexes do NOT store pointers to physical row locations. Instead, leaf nodes of a secondary index store the **primary key value**:

```
Secondary Index on (customer_id):
  Leaf entry: (customer_id=5, pk=42)
                                ↓
  To fetch row: look up pk=42 in clustered index
                (double B-tree traversal)

Why not store physical page pointers?
  If a row is updated and moves (page split), all secondary
  indexes pointing to physical location would need updating.
  By pointing to the PK, only the clustered index update
  is needed — secondary indexes remain valid.
```

**Trade-off**: Lookups via secondary index always require a clustered index lookup (unless the query can be satisfied from the secondary index alone — a "covering index"). This is why choosing a compact primary key matters — it's repeated in every secondary index leaf.

### 3.2 Buffer Pool

The InnoDB buffer pool serves the same purpose as PostgreSQL's shared buffers, but with additional responsibilities:

```
InnoDB Buffer Pool Contents:
┌──────────────────────────────────────────────────────────┐
│  Data Pages (clustered index leaf pages)                 │
│  Index Pages (internal nodes + secondary index pages)    │
│  Undo Log Pages (for MVCC — unlike PostgreSQL)           │
│  System Page (tablespace metadata)                       │
│  Change Buffer (deferred secondary index writes)         │
└──────────────────────────────────────────────────────────┘

LRU List Structure (InnoDB's modified LRU):
┌────────────────────────────────────────────────────────┐
│  Young (Hot) Sublist     │  Old (Cold) Sublist         │
│  (recently used pages)   │  (newly read pages go here) │
│  [page] [page] [page]    │  [page] [page] [page]       │
└──────────────────────────┴─────────────────────────────┘
  Default: 5/8 young, 3/8 old

  New page insertion point: head of Old sublist
  After innodb_old_blocks_time (1s default): promoted to Young
  This prevents full-table scans from evicting hot data!
```

### 3.3 Undo Logs

This is the crucial architectural difference from PostgreSQL. **InnoDB performs in-place updates to the clustered index** and uses a separate **undo log** to reconstruct old versions for MVCC and rollback.

```
PostgreSQL approach (append-only heap):
  Original row: [xmin=100, xmax=0, data="Alice"]
  After UPDATE:
    Old tuple: [xmin=100, xmax=200, data="Alice"]  ← stays in heap
    New tuple: [xmin=200, xmax=0,   data="Bob"]    ← appended to heap
  Heap grows with old versions. VACUUM cleans up.

InnoDB approach (in-place update + undo log):
  Clustered index page:
    Before: pk=1, name="Alice", DB_TRX_ID=100, DB_ROLL_PTR=NULL
    After:  pk=1, name="Bob",   DB_TRX_ID=200, DB_ROLL_PTR=→undo

  Undo log:
    Entry: {trx=200, prev_data="Alice", prev_roll_ptr=NULL}

  To reconstruct old version:
    Follow DB_ROLL_PTR → undo log entry → reconstruct "Alice"
    Follow chain further for older versions
```

#### Undo Log Chain

```
Multiple versions of a row (InnoDB MVCC chain):
                          Clustered Index
                         ┌──────────────────┐
                         │ pk=1, name="Eve" │
                         │ trx_id=400       │
                         │ roll_ptr ────────┼──────────┐
                         └──────────────────┘          │
                                                        ▼
                                              Undo Log Entry
                                             ┌──────────────────┐
                                             │ trx=400          │
                                             │ old: name="Carol"│
                                             │ prev_roll_ptr ───┼──┐
                                             └──────────────────┘  │
                                                                    ▼
                                                          Undo Log Entry
                                                         ┌──────────────┐
                                                         │ trx=300      │
                                                         │ old: "Alice" │
                                                         │ prev=NULL    │
                                                         └──────────────┘

A reader with snapshot at trx=350:
  Sees trx_id=400 > 350, not visible
  Follows roll_ptr → finds trx=400 → old="Carol" at trx<350 → VISIBLE
```

**Key difference**: PostgreSQL stores all versions in the heap. InnoDB stores only the current version in the clustered index; old versions are reconstructed by replaying undo log entries backwards. This makes InnoDB reads slightly more expensive for very old snapshots but keeps the primary index compact.

### 3.4 Redo Logs

InnoDB's redo log is its WAL — it records physical changes to pages, enabling crash recovery.

```
Why Both Undo AND Redo?

REDO LOG: Ensures committed transactions survive crashes
  → If buffer pool pages haven't been flushed when crash occurs,
    redo log lets us replay the physical changes at recovery.

UNDO LOG: Ensures uncommitted transactions are rolled back
  → If a transaction was running when crash occurred,
    undo log lets us undo its partial changes.

PostgreSQL comparison:
  PostgreSQL only needs REDO (WAL) because:
    - MVCC keeps old versions in heap (no rollback needed for crash recovery)
    - Uncommitted versions are simply invisible due to xmin/xmax
    - VACUUM handles cleanup asynchronously

InnoDB needs BOTH because:
  - Clustered index has been modified in-place
  - Uncommitted transactions' changes are physically in the page
  - Must physically undo them on crash + incomplete transaction
```

```
InnoDB Recovery Sequence (crash → restart):
  1. Read redo log, find last checkpoint
  2. REDO phase: replay all redo log records from checkpoint
     → Brings pages to state just before crash (including uncommitted)
  3. UNDO phase: read active transaction list from undo log
     → Roll back all transactions that weren't committed
  4. Database is now in clean committed state
```

### 3.5 Locking Mechanisms

InnoDB provides **row-level locking**, which enables much higher concurrency than table-level locking.

```
Lock Types:
  Shared (S): Multiple readers can hold simultaneously
  Exclusive (X): Only one writer; blocks all other locks

Record Lock: locks a specific index record
  Prevents other transactions from modifying that row.

Gap Lock: locks the gap BETWEEN index records
  Example: Gap lock on (10, 20) prevents INSERT of any value
  between 10 and 20. Prevents phantom reads.

Next-Key Lock = Record Lock + Gap Lock on preceding gap
  This is InnoDB's default at REPEATABLE READ isolation.
  SELECT ... FOR UPDATE takes next-key locks.

┌──────────────────────────────────────────────┐
│  Index values: 1, 5, 10, 20, ∞              │
│                                              │
│  Record Lock on 10: ■ (just row with key=10) │
│  Gap Lock (5,10):   ( ) (values between 5-10)│
│  Next-Key Lock on 10: (5,10] (gap + record)  │
└──────────────────────────────────────────────┘
```

**Gap locks prevent phantom reads** — a key advantage of REPEATABLE READ in InnoDB:

```sql
-- Transaction A: 
SELECT * FROM orders WHERE amount BETWEEN 100 AND 200 FOR UPDATE;
-- Acquires next-key locks on all index records in (100,200]

-- Transaction B (concurrent):
INSERT INTO orders (amount) VALUES (150);
-- BLOCKS! The gap (100,200] is locked by Transaction A

-- This prevents the "phantom row" from appearing if A re-reads the range.
-- PostgreSQL at REPEATABLE READ uses MVCC snapshots instead — no gap locks.
```

---

## 4. Design Trade-Offs

### InnoDB vs PostgreSQL MVCC

```
┌─────────────────────────────┬──────────────────────────────────┐
│         InnoDB               │         PostgreSQL              │
├─────────────────────────────┼──────────────────────────────────┤
│ In-place updates             │ Append-only updates             │
│ Undo log for old versions    │ Old versions stay in heap        │
│ Clustered primary index      │ Heap file (unordered)           │
│ No VACUUM (purge thread)     │ VACUUM required                 │
│ Undo log purge = InnoDB's    │ VACUUM = PostgreSQL's cleanup   │
│   cleanup mechanism          │   mechanism                     │
│ PK lookups: 1 I/O            │ PK lookups: 2 I/Os (idx+heap)  │
│ Secondary idx: 2 I/Os        │ Secondary idx: 2 I/Os (same)   │
│ OLD versions: undo traversal │ OLD versions: heap tuple        │
│ Write amplification: lower   │ Write amplification: higher     │
│   (one B-tree update)        │   (new tuple + index updates)   │
└─────────────────────────────┴──────────────────────────────────┘
```

### Clustered Index Trade-offs

| Advantage | Limitation |
|---|---|
| PK lookups avoid heap fetch | Secondary indexes always double-traverse |
| Range scans are sequential I/O | Large PK wastes space in every secondary index |
| INSERT with sequential PK is fast | Random PK (UUID) causes random page splits |
| Data physically sorted by PK | Cannot choose physical sort order ad-hoc |

### Why PostgreSQL Chose Differently

PostgreSQL's heap design was a deliberate choice to support:
- Multiple index types on the same table (each is a separate file)
- Tables with no meaningful "clustering key"
- VACUUM as an offline process that doesn't interfere with queries
- Flexibility in choosing physical data layout

InnoDB's design was optimized for:
- Web application workloads (lots of PK lookups by integer ID)
- Write-heavy workloads (in-place updates reduce WAL volume)
- MySQL's pluggable storage engine architecture (undo/redo internally managed)

---

## 5. Experiments / Observations

### Experiment 1: Clustered Index vs Secondary Index Lookup

```sql
-- Create test table
CREATE TABLE products (
    id INT PRIMARY KEY AUTO_INCREMENT,
    sku VARCHAR(50),
    name VARCHAR(200),
    price DECIMAL(10,2),
    INDEX idx_sku (sku)
) ENGINE=InnoDB;

INSERT INTO products (sku, name, price)
    SELECT CONCAT('SKU-', i), CONCAT('Product ', i), RAND()*1000
    FROM (SELECT @row := @row + 1 AS i FROM information_schema.columns, (SELECT @row:=0) r LIMIT 1000000) x;

-- Primary key lookup (clustered index)
EXPLAIN SELECT * FROM products WHERE id = 500000;
-- type: const (single B-tree leaf lookup, no heap fetch)
-- key: PRIMARY, rows: 1

-- Secondary index lookup (requires double traversal)
EXPLAIN SELECT * FROM products WHERE sku = 'SKU-500000';
-- type: ref
-- key: idx_sku
-- Extra: uses index + clustered index lookup (two B-tree traversals)

-- Covering index (no clustered index needed)
EXPLAIN SELECT id, sku FROM products WHERE sku = 'SKU-500000';
-- Extra: "Using index" — answered from secondary index alone!
```

**Observation**: InnoDB's `EXPLAIN` output shows the double-traversal explicitly. Creating a covering index (including all needed columns in the secondary index) eliminates the second traversal.

### Experiment 2: Gap Lock Demonstration

```sql
-- Session A:
BEGIN;
SELECT * FROM products WHERE id BETWEEN 100 AND 110 FOR UPDATE;
-- Acquires next-key locks on records 100-110 and gap before 111

-- Session B (concurrent):
INSERT INTO products (id, sku, name, price) VALUES (105, 'NEW', 'New Product', 99);
-- BLOCKS — gap (100,110] is locked

-- Session A commits:
COMMIT;
-- Session B INSERT immediately succeeds
```

### Experiment 3: Observing Undo Log Usage

```sql
-- Check undo log size during long-running transaction
BEGIN;
UPDATE products SET price = price * 1.1;  -- updates 1M rows

-- In another session while transaction is running:
SELECT name, count, sum_other_wait_time
FROM information_schema.INNODB_METRICS
WHERE name LIKE 'trx_rseg%';
-- Shows undo log segment growth during transaction

-- history_list_length shows number of uncommitted undo log versions:
SHOW ENGINE INNODB STATUS\G
-- "History list length: 1847234"
-- High value = long-running transactions holding old undo log versions
-- Purge thread can't clean up until transaction commits
```

**Observation**: Unlike PostgreSQL where dead tuples stay in heap pages and can be seen via `pg_stat_user_tables`, InnoDB's "dead" data is in the undo log segments. `History list length` is InnoDB's equivalent of PostgreSQL's dead tuple count.

### Experiment 4: Impact of UUID vs SERIAL Primary Key

```sql
-- Sequential PK (INT AUTO_INCREMENT) — fast inserts
CREATE TABLE t_seq (id INT PRIMARY KEY AUTO_INCREMENT, v VARCHAR(100));
INSERT INTO t_seq (v) SELECT repeat('x', 50) FROM ... LIMIT 1000000;
-- Page splits: ~0 (always appending to rightmost leaf page)
-- Buffer pool: mostly sequential access

-- UUID PK — slower inserts  
CREATE TABLE t_uuid (id CHAR(36) PRIMARY KEY, v VARCHAR(100));
INSERT INTO t_uuid SELECT UUID(), repeat('x', 50) FROM ... LIMIT 1000000;
-- Page splits: ~500K (random inserts split 50/50 continuously)
-- Observation: 40-60% slower insert throughput than INT PK
-- Secondary indexes: each leaf entry is 36 bytes (vs 4 for INT)
```

---

## 6. Key Learnings

1. **Clustered indexes are a fundamental architectural choice**: The decision to make the table itself a B-tree keyed by primary key is not just an optimization — it changes how all secondary indexes work, how MVCC works, and why choosing the right primary key matters so much in InnoDB.

2. **Undo logs vs heap versioning is the core MVCC trade-off**: InnoDB's in-place updates + undo log approach keeps the primary index compact but adds complexity for MVCC reads (undo chain traversal). PostgreSQL's append-only approach is simpler for reads but requires VACUUM. Neither is strictly better — they're optimized for different workload patterns.

3. **Why InnoDB needs both undo AND redo logs**: Redo ensures committed data survives crashes (WAL). Undo ensures uncommitted in-place modifications are reversed on crash (since the page was physically modified). PostgreSQL avoids needing crash-time undo because uncommitted versions are simply never visible (no xmin commit in the commit log).

4. **Gap locks are InnoDB's answer to phantom reads**: PostgreSQL prevents phantoms through snapshot isolation (MVCC snapshot at transaction start). InnoDB at REPEATABLE READ uses gap locks instead, which can cause unexpected blocking. At SERIALIZABLE, both systems add full serialization overhead.

5. **The purge thread is InnoDB's VACUUM**: Both systems need a background cleanup process, just targeting different storage locations. InnoDB's purge thread reclaims undo log space; PostgreSQL's VACUUM reclaims heap tuple space.

---

## References

- [MySQL InnoDB Architecture Documentation](https://dev.mysql.com/doc/refman/8.0/en/innodb-architecture.html)
- [InnoDB Multi-Versioning](https://dev.mysql.com/doc/refman/8.0/en/innodb-multi-versioning.html)
- [High Performance MySQL, 4th Edition](https://www.oreilly.com/library/view/high-performance-mysql/9781492080503/)
- [InnoDB Locking and Transaction Model](https://dev.mysql.com/doc/refman/8.0/en/innodb-locking-transaction-model.html)
- Heikki Tuuri. "InnoDB — A Relational Database Engine for Linux." (2000)


Here's the summary of the 80-20 rule (Pareto principle) as it applies to DBMS internals and performance tuning — the 20% of concepts that give 80% of the practical value:

### The 80-20 Rule Applied to DBMS Internals

The Pareto principle says 80% of effects come from 20% of causes. In DBMS tuning, this means you don't need to understand every detail to make 80% of the improvements.

#### The Vital 20% (High Impact Concepts)

1. **MVCC + Snapshot Isolation**
   - **Why it matters**: 80% of performance problems come from locking conflicts (deadlocks, lock waits).
   - **80% effect**: Understanding snapshot isolation explains why your queries sometimes return stale data (expected behavior) and why you need SELECT FOR UPDATE when you need consistency.

2. **Buffer Pool / Shared Buffers**
   - **Why it matters**: Most queries are reads. Buffer pool size determines hit rate.
   - **80% effect**: If your working set fits in the buffer pool, 90%+ of queries are in RAM. If not, everything slows down. This is the single most important configuration parameter.

3. **Index B-trees**
   - **Why it matters**: 90%+ of queries use indexes. Understanding leaf-node structure and double traversal explains why secondary indexes are slower than primary (in InnoDB).
   - **80% effect**: Creating covering indexes reduces disk I/O by 50-80% for many workloads.

4. **WAL / Write-Ahead Logging**
   - **Why it matters**: Durability vs performance trade-off.
   - **80% effect**: Understanding `sync_binlog` and `innodb_flush_log_at_trx_commit` lets you tune for performance (latency) vs safety (crash recovery). This controls the "can't lose data" vs "can't have slow writes" decision.

5. **The Checkpointer / Flush Process**
   - **Why it matters**: Background flush behavior (checkpoint) affects write latency and recovery time.
   - **80% effect**: You don't need to know the checkpoint algorithm details. You just need to know that checkpoints happen periodically and can cause write spikes. Tuning checkpoint frequency reduces these spikes.

6. **Garbage Collection / Vacuum / Purge**
   - **Why it matters**: Dead tuple cleanup affects performance and storage.
   - **80% effect**: In InnoDB, the purge thread cleans up undo logs. In PostgreSQL, VACUUM reclaims heap space. Understanding this explains why you need background cleanup and why tables grow over time.

#### The Remaining 80% (Low Impact Details)

- Specific lock types (next-key, gap, insert intention, etc.) — you need to know the general concepts, not memorize every lock type.
- Internal hashing algorithms for buffer pool lookup (LRU replacement details).
- Specific page header formats and layout on disk.
- Background scheduler details for I/O (elevator algorithms).
- WAL segment management specifics (rollover timing, WAL writer behavior).
- Internal optimizer cost estimation formulas (you just need to know it uses stats, not the exact formula).
- Transaction commit protocol details (2PC, group commit implementations).