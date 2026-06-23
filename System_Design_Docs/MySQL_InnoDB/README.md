# MySQL / InnoDB Storage Engine

## 1. Problem Background

### Why InnoDB Was Built

MySQL's original storage engine, MyISAM, had a fundamental limitation: it used table-level locking. Any write locked the entire table for all readers. For a web application serving thousands of concurrent requests — the precise use case MySQL was targeting in the late 1990s — this was a showstopper.

InnoDB was developed by Heikki Tuuri at Innobase Oy in Finland and first shipped with MySQL 3.23 in 2001. The design goals were explicit:
1. **Row-level locking** — concurrent writes to different rows without blocking each other
2. **ACID transactions** — full commit/rollback semantics, not just "best effort"
3. **Foreign key constraints** — referential integrity enforced at the engine level
4. **Crash recovery** — no manual repair after crashes (unlike MyISAM's `myisamchk`)

The architectural decisions Tuuri made to achieve these goals — clustered indexes, undo logs, redo logs, and a specific MVCC approach — are what make InnoDB genuinely different from PostgreSQL's storage model, not just different in implementation but different in fundamental design philosophy.

Oracle acquired Innobase in 2005 and MySQL in 2010, making InnoDB the default MySQL storage engine in 5.5 (2010).

---

## 2. Architecture Overview

### InnoDB System Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                      MySQL Server Layer                       │
│   SQL Parser → Query Cache → Optimizer → Execution Engine    │
└───────────────────────────────┬──────────────────────────────┘
                                │
┌───────────────────────────────▼──────────────────────────────┐
│                     InnoDB Storage Engine                     │
│                                                               │
│  ┌────────────────────────────────────────────────────────┐  │
│  │                    Buffer Pool                          │  │
│  │   Data Pages │ Index Pages │ Undo Pages │ Insert Buffer │  │
│  │                 (Adaptive Hash Index)                   │  │
│  └────────────────────────────────────────────────────────┘  │
│                                                               │
│  ┌──────────────────┐    ┌───────────────────────────────┐   │
│  │   Undo Log Space │    │  Redo Log Buffer → Redo Files  │   │
│  │  (for MVCC and   │    │  (ib_logfile0, ib_logfile1)   │   │
│  │   rollback)      │    └───────────────────────────────┘   │
│  └──────────────────┘                                        │
│                                                               │
│  Background Threads:                                          │
│    Master thread, I/O threads, Purge thread, Page cleaner    │
└────────────────────────────────────────────────────────────  ┘
                                │
                                ▼
              ┌─────────────────────────────────┐
              │  Tablespace Files               │
              │  ibdata1 (system tablespace)    │
              │  t1.ibd, t2.ibd (per-table)     │
              └─────────────────────────────────┘
```

### Data Flow: A Write Transaction

```
1. BEGIN transaction → allocate transaction ID, create undo log segment
2. UPDATE row:
   a. Read page into buffer pool (if not cached)
   b. Write undo record (old row values → undo log)
   c. Modify row in-place in the buffer page
   d. Write redo log record (what changed → redo buffer)
3. COMMIT:
   a. Flush redo buffer to redo log files (fsync)
   b. Mark transaction committed in undo log
   c. Dirty pages stay in buffer pool (written later)
4. Background purge: eventually removes undo records no longer needed by any active transaction
```

---

## 3. Internal Design

### 3.1 Clustered Indexes

This is InnoDB's most consequential architectural decision. In InnoDB, **every table IS a B+ tree indexed by the primary key**. There is no separate heap file.

```
PostgreSQL model:
  Heap file (unordered rows by physical location)
      ↑ ctid references
  B-Tree index (key → ctid pointer to heap)

InnoDB model:
  Clustered Index (B+ tree where leaf pages ARE the rows)
  Primary key → row data stored directly in leaf page
```

**What a clustered index leaf page looks like:**
```
Leaf Page (16KB default):
┌──────────────────────────────────────────────────────────────┐
│  Page Header (38 bytes)                                       │
│  - page_level=0, page_type=INDEX, space_id, page_no          │
├──────────────────────────────────────────────────────────────┤
│  Infimum record (lower bound sentinel)                        │
├──────────────────────────────────────────────────────────────┤
│  Row 1: [pk=100 | trx_id | roll_ptr | col1 | col2 | col3]    │
│  Row 2: [pk=101 | trx_id | roll_ptr | col1 | col2 | col3]    │
│  ...                                                          │
├──────────────────────────────────────────────────────────────┤
│  Supremum record (upper bound sentinel)                       │
├──────────────────────────────────────────────────────────────┤
│  Page Directory (sparse array of slot offsets)                │
│  Page Trailer (checksum, LSN)                                 │
└──────────────────────────────────────────────────────────────┘
```

Every row stores `trx_id` (the transaction that last modified it) and `roll_ptr` (a pointer into the undo log — the chain to old versions). These are system columns invisible to users but central to MVCC.

**Why clustered indexes improve performance:**

1. **Primary key lookups are one operation**: For `SELECT * FROM orders WHERE id = 12345`, InnoDB traverses the B+ tree and finds the full row in the leaf page. PostgreSQL has to: traverse B-Tree index → get ctid → go to heap page. Two I/Os vs potentially one.

2. **Range scans are sequential**: `SELECT * FROM orders WHERE id BETWEEN 1000 AND 2000` reads physically adjacent leaf pages. PostgreSQL's heap rows for this range could be scattered across many pages (because inserts were in random order relative to the primary key).

3. **No separate heap file**: Simpler storage layout, one less file to manage.

**The downside of clustered indexes:**

- If you choose a poor primary key (like a UUID that inserts randomly), you get random writes scattered across the B+ tree → frequent page splits → fragmentation → poor performance.
- Secondary indexes are more expensive because they store the primary key, not a direct pointer to the row.

### 3.2 Secondary Indexes

In InnoDB, a secondary index looks like:
```
Secondary index leaf: (secondary_key_value → primary_key_value)
```

Not a pointer to the physical row. A lookup via secondary index requires:
1. Traverse secondary index B+ tree → get primary key value
2. Traverse clustered index B+ tree → get row

This **double lookup** is called a "clustered index lookup" or "bookmark lookup." It's more expensive than PostgreSQL's secondary index → heap page lookup, but InnoDB compensates via:
- **Index condition pushdown**: Evaluate WHERE conditions during index scan
- **Covering indexes**: If all required columns are in the secondary index, skip the clustered index lookup entirely

```sql
-- This is a "covering index" scan — no clustered index lookup needed:
SELECT email FROM users WHERE region = 'North';
-- IF index is: CREATE INDEX idx ON users (region, email)
-- The email column is in the index leaf, so InnoDB never touches the clustered index
```

### 3.3 Buffer Pool

InnoDB's buffer pool is analogous to PostgreSQL's shared_buffers but with more built-in intelligence.

**Buffer Pool Structure:**
```
Buffer Pool (innodb_buffer_pool_size):
┌──────────────────────────────────────────────────────────────┐
│  LRU List (eviction):                                         │
│    Young sublist (5/8 of pool) ← recently accessed           │
│    Old sublist   (3/8 of pool) ← insertion point             │
│                                                               │
│  Flush List: dirty pages, ordered by LSN for checkpoint       │
│  Free List:  empty frames available for new pages             │
│                                                               │
│  Adaptive Hash Index:                                         │
│    Hash table over frequently-accessed B+ tree pages          │
│    Converts B+ tree traversal to O(1) hash lookup             │
└──────────────────────────────────────────────────────────────┘
```

InnoDB uses a **midpoint insertion strategy** (not pure LRU). New pages enter the buffer pool at the boundary between the "young" and "old" sublists (3/8 from the tail). They're only promoted to the "young" (hot) sublist if accessed again within `innodb_old_blocks_time` (default 1 second). This prevents a large full-table scan from evicting the entire working set — a known failure mode of pure LRU.

**Adaptive Hash Index (AHI):**
InnoDB automatically builds an in-memory hash index over B+ tree pages that are accessed repeatedly with the same prefix. This converts `O(log n)` tree traversal to `O(1)` hash lookup for hot queries. It's automatic and self-tuning, not user-controlled.

### 3.4 Undo Logs

Undo logs serve two purposes in InnoDB:

**Purpose 1: Transaction Rollback**
Before modifying a row, InnoDB writes the old row values to an undo record. If the transaction rolls back, undo records are applied in reverse to restore the original data.

**Purpose 2: MVCC (reading old versions)**
This is the key architectural difference from PostgreSQL.

```
PostgreSQL MVCC:
  Old row version still lives in the heap page
  Readers find it by scanning the heap
  VACUUM eventually removes it

InnoDB MVCC:
  Row is modified in-place in the clustered index leaf page
  Old version is stored in the undo log
  Readers with older snapshots follow the roll_ptr chain to reconstruct the old version
```

```
Undo Chain for a row:
  Current row in clustered index:
    [pk=42 | trx_id=500 | roll_ptr ──────────────────────┐ | name='Bob']
                                                          │
  Undo Log Record (rollback segment):                     ↓
    [trx_id=300 | roll_ptr ────────────────────┐ | name='Alice']
                                               │
  Older Undo Record:                           ↓
    [trx_id=100 | roll_ptr=null | name='Adam']
```

A reader whose snapshot predates transaction 300 will follow the `roll_ptr` chain twice to see `name='Adam'`.

**Trade-off vs PostgreSQL MVCC:**
- InnoDB: faster writes (in-place, no new tuple on heap), but reads of old versions require undo chain traversal
- PostgreSQL: reads of any version are fast (all on heap), but writes create dead tuples, and VACUUM is required

InnoDB's approach is called "Oracle-style MVCC" because Oracle uses the same undo-based model.

### 3.5 Redo Logs

Redo logs are InnoDB's equivalent of PostgreSQL's WAL. They record physical changes to pages for crash recovery.

**Why InnoDB needs BOTH undo and redo logs:**

- **Redo log**: "What pages were modified" — used to redo committed transactions after a crash
- **Undo log**: "What the old data was" — used to undo uncommitted transactions after a crash

This is fundamentally different from PostgreSQL, where WAL serves as the only recovery log. InnoDB needs both because of its MVCC model: undo records are needed not just for rollback but for concurrent readers to see old versions.

```
Crash recovery sequence in InnoDB:
  1. Apply redo log → bring all pages up to date (including changes from uncommitted txns)
  2. Apply undo log → roll back all transactions that were active at crash time
  Result: only committed data survives
```

**Redo log files are circular:**
```
ib_logfile0 [──────────────────────────────────────] 1GB
ib_logfile1 [──────────────────────────────────────] 1GB
              ↑ write position wraps around

If redo logs fill up before dirty pages are flushed → I/O stall
This is why innodb_log_file_size is a critical tuning parameter
```

### 3.6 Row-Level Locking and Gap Locks

InnoDB implements row-level locking as part of its commitment to high concurrency. But the locking model goes beyond simple row locks.

**Lock types:**
- **Record lock**: Lock on an individual row
- **Gap lock**: Lock on the gap *between* index records (prevents phantom reads)
- **Next-key lock**: Record lock + gap lock on the gap before the record (default under Repeatable Read)

**Why gap locks?**

The phantom read problem: Transaction A reads all rows with `salary > 50000`. Transaction B inserts a new row with `salary = 60000`. Transaction A reads again — now it sees a new "phantom" row.

Under SQL standard Repeatable Read, phantoms are allowed. Under InnoDB's Repeatable Read (which uses next-key locks), phantoms are prevented.

```sql
-- Transaction A:
SELECT * FROM employees WHERE salary > 50000 FOR UPDATE;
-- InnoDB places next-key locks on all rows with salary > 50000
-- AND on the gap after the last such row

-- Transaction B:
INSERT INTO employees VALUES (99, 'Eve', 60000);
-- BLOCKED — trying to insert into a gap that's locked by Transaction A
```

**The trade-off**: Gap locks prevent phantoms but can cause surprising lock contention. Two inserts into adjacent non-overlapping gaps can still block each other if they both need the same gap lock. Under `READ COMMITTED` isolation, gap locks are disabled — only record locks are used, improving concurrency at the cost of allowing phantoms.

---

## 4. Design Trade-Offs

### Clustered Index vs Heap: The Fundamental Split

| Scenario | InnoDB (Clustered) | PostgreSQL (Heap) |
|----------|-------------------|-------------------|
| Primary key lookup | Single B+ tree traversal | Index traversal + heap fetch |
| Range scan on PK | Sequential leaf page reads | Potentially random heap reads |
| Secondary index lookup | Double traversal | Index traversal + heap fetch |
| Sequential insert (auto-increment PK) | Appends to rightmost leaf | Appends to heap (no ordering) |
| Random insert (UUID PK) | Scattered inserts → fragmentation | No fragmentation (heap is unordered) |
| UPDATE to non-indexed column | In-place modification + undo record | New tuple appended to heap |
| Storage for old versions | Undo log (separate area) | Inline in heap (dead tuples) |

**Critical insight**: InnoDB's clustered index is a performance benefit for workloads where primary key lookups dominate (the common OLTP case). It becomes a liability when:
1. Primary keys are random (UUIDs) — random I/O pattern
2. Secondary index lookups are frequent — two index traversals
3. Rows are large — fewer rows per leaf page, more pages to scan

### Why InnoDB Needs Both Undo and Redo Logs

This question often trips people up. PostgreSQL only needs WAL (which serves as redo log). Why does InnoDB need both?

Because PostgreSQL's MVCC model keeps old versions in the heap — they're already persistent on disk, and WAL can redo modifications to them. InnoDB's MVCC model stores old versions *only* in undo logs. After a crash, undo logs must be replayed to reconstruct old row versions for any active transactions.

Redo log = "replay forward to latest committed state"
Undo log = "undo backward to remove uncommitted changes"

Both are needed because InnoDB modifies data in-place.

### MVCC Implementation Trade-offs

| Aspect | InnoDB (Undo-based MVCC) | PostgreSQL (Heap-based MVCC) |
|--------|-------------------------|------------------------------|
| Write path | Modify in-place + write undo record | Append new row version to heap |
| Read old version | Follow roll_ptr chain through undo log | Find old tuple on heap page |
| Old version storage | Undo tablespace (shared, purged by daemon) | Heap pages (purged by VACUUM) |
| Reclamation | Purge thread removes unneeded undo records | VACUUM removes dead heap tuples |
| Table bloat | Generally less (in-place updates) | More if VACUUM falls behind |
| Long-running readers | Hold undo records hostage (undo log grows) | Hold old heap tuples (no bloat until VACUUM) |

The "long transaction problem" hits InnoDB harder: a long-running read transaction prevents undo purge, causing undo log growth. PostgreSQL has the same issue (long transactions hold back VACUUM's XID horizon), but undo log growth is immediately visible while heap bloat is subtler.

---

## 5. Experiments / Observations

### Experiment 1: Clustered Index Lookup vs Secondary Index

```sql
CREATE TABLE products (
  id INT PRIMARY KEY AUTO_INCREMENT,
  sku VARCHAR(50),
  name VARCHAR(100),
  price DECIMAL(10,2),
  INDEX idx_sku (sku)
);

INSERT INTO products SELECT i, CONCAT('SKU-', i), CONCAT('Product ', i), RAND()*100
  FROM seq_1_to_1000000;  -- using sequence table or equivalent

-- Primary key lookup (single clustered index traversal):
EXPLAIN SELECT * FROM products WHERE id = 500000;
-- type: const, key: PRIMARY, rows: 1, Extra: (none)

-- Secondary index lookup (needs clustered index lookup):
EXPLAIN SELECT * FROM products WHERE sku = 'SKU-500000';
-- type: ref, key: idx_sku, rows: 1, Extra: (none — needs PK lookup)

-- Covering index (avoids clustered index lookup):
EXPLAIN SELECT id, sku FROM products WHERE sku = 'SKU-500000';
-- type: ref, key: idx_sku, Extra: "Using index" ← no clustered index read
```

The `Using index` in the third query means InnoDB found all required columns in the secondary index leaf — the clustered index was never read.

### Experiment 2: Gap Lock Behavior

```sql
CREATE TABLE accounts (id INT PRIMARY KEY, balance INT);
INSERT INTO accounts VALUES (1, 100), (5, 200), (10, 300);

-- Session A:
START TRANSACTION;
SELECT * FROM accounts WHERE id BETWEEN 3 AND 7 FOR UPDATE;
-- Acquires next-key lock on (1, 5] gap and record lock on id=5

-- Session B (concurrent):
INSERT INTO accounts VALUES (4, 150);
-- BLOCKED — id=4 falls in the (1, 5] gap locked by Session A

INSERT INTO accounts VALUES (8, 150);
-- ALSO BLOCKED — id=8 falls in the (5, 10] gap which has a gap lock

INSERT INTO accounts VALUES (0, 150);
-- NOT BLOCKED — id=0 is before the locked range
```

This demonstrates that InnoDB's Repeatable Read prevents phantom reads in the `3-7` range but creates contention that may surprise developers expecting only row-level locking.

### Experiment 3: Observing Undo Log Growth Under Long Transactions

```sql
-- Session A: Start a long-running transaction and do a read
START TRANSACTION;
SELECT COUNT(*) FROM large_table;  -- takes a snapshot

-- Session B: Run many updates
UPDATE large_table SET val = val + 1;  -- 1M rows
UPDATE large_table SET val = val + 1;  -- 1M rows again

-- Check undo log space:
SELECT name, subsystem, count
FROM information_schema.INNODB_METRICS
WHERE name LIKE '%undo%';
```

With Session A holding an old snapshot, InnoDB cannot purge undo records that might be needed for that snapshot. If Session A runs for hours, undo log segments grow unboundedly — a real production concern.

### Experiment 4: EXPLAIN Output for Join Query

```sql
EXPLAIN SELECT c.name, COUNT(o.id), SUM(oi.price * oi.quantity)
FROM customers c
JOIN orders o ON o.customer_id = c.id
JOIN order_items oi ON oi.order_id = o.id
WHERE c.region = 'North'
GROUP BY c.id;
```

Sample output:
```
id | select_type | table | type  | key        | rows  | Extra
1  | SIMPLE      | c     | ref   | idx_region | 2500  | Using where; Using temporary
1  | SIMPLE      | o     | ref   | idx_cust   | 10    | Using index
1  | SIMPLE      | oi    | ref   | idx_order  | 5     | Using index
```

`type=ref` means InnoDB is using an index to find a subset of rows. `rows=2500` is the planner estimate for matching customers. The `Using index` on orders and order_items means covering index scans.

---

## 6. Key Learnings

**Clustered indexes are not just a performance feature — they're a fundamental storage model.**
In InnoDB, the primary key is not a lookup mechanism added on top of a heap. The primary key IS the storage format. This means your choice of primary key directly determines the physical organization of all your data. Auto-increment integers are not just a convention — they're the optimal InnoDB primary key because they produce sequential writes into the rightmost leaf page.

**The undo log is the hidden cost of in-place MVCC.**
InnoDB's "clean" in-place updates look efficient, but the undo log is the hidden storage for old versions. Long transactions don't just hold locks — they prevent undo purge, causing undo log growth that can fill your disk. Monitoring `history_list_length` in InnoDB status is not optional in production.

**Why InnoDB needs both undo and redo logs (the answer that matters):**
Redo logs exist for crash durability — replay forward. Undo logs exist for atomicity — roll backward. Both are needed because in-place updates require both "replay what was committed" and "undo what was not." PostgreSQL sidesteps this with its append-only heap: old versions are already on disk in their original form, so only WAL (redo) is needed.

**Gap locks are the source of most "unexpected lock wait" incidents.**
Most MySQL developers think row-level locking means only the modified row is locked. In Repeatable Read, gap locks on range queries and index scans create implicit locks on gaps between records. Understanding next-key locks is essential for debugging lock timeout issues in production.

**InnoDB's buffer pool is smarter than most databases' caches.**
The midpoint insertion strategy prevents table scans from evicting the working set. The adaptive hash index automatically converts hot B+ tree traversals into O(1) hash lookups. Both features exist because web workloads have highly skewed access patterns — InnoDB was built for that reality.

---

*References: MySQL Source Code (storage/innobase/), InnoDB Internals by Percona, "MySQL Internals Manual" (MySQL 8.0), "High Performance MySQL" — Schwartz, Zaitsev, Tkachenko*
