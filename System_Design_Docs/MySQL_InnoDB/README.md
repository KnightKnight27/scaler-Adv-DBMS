
# MySQL / InnoDB Storage Engine

---

## 1. Problem Background

### Why InnoDB Exists

MySQL's original storage engine, MyISAM, had a fundamental limitation: it used table-level locking. Every `INSERT`, `UPDATE`, or `DELETE` locked the entire table, making concurrent write-heavy workloads a bottleneck. MyISAM also had no transaction support and no crash-safe recovery — a server crash could corrupt tables silently.

InnoDB was developed by Innobase Oy (later acquired by Oracle) specifically to solve these problems:

- **Row-level locking** instead of table-level locking
- **ACID transactions** with full crash recovery
- **MVCC** for consistent reads without blocking writers
- **Foreign key constraints** enforced at the engine level

InnoDB became the default MySQL storage engine in MySQL 5.5 (2010). It represents a different design philosophy from PostgreSQL: while PostgreSQL uses an append-only heap with external indexes, InnoDB uses **clustered indexes** — the data itself is organized as a B+-tree ordered by primary key.

---

## 2. Architecture Overview

```
  Client Query (SQL)
         │
         ▼
  ┌──────────────────────────────────────────────────┐
  │              MySQL Server Layer                  │
  │  ┌──────────┐  ┌───────────┐  ┌──────────────┐   │
  │  │  Parser  │  │ Optimizer │  │   Executor   │   │
  │  └──────────┘  └───────────┘  └──────┬───────┘   │
  └─────────────────────────────────────-┼───────────┘
                                         │  Storage Engine API
                                         ▼
  ┌──────────────────────────────────────────────────┐
  │              InnoDB Storage Engine               │
  │                                                  │
  │  ┌───────────────────────────────────────────┐   │
  │  │           Buffer Pool                     │   │
  │  │  ┌──────────────┐  ┌──────────────────┐   │   │
  │  │  │  Data Pages  │  │  Index Pages     │   │   │
  │  │  │ (clustered   │  │ (secondary       │   │   │
  │  │  │  B+-tree)    │  │  B+-trees)       │   │   │
  │  │  └──────────────┘  └──────────────────┘   │   │
  │  │  ┌────────────────────────────────────┐   │   │
  │  │  │  Insert Buffer / Change Buffer     │   │   │
  │  │  └────────────────────────────────────┘   │   │
  │  └───────────────────────────────────────────┘   │
  │                                                  │
  │  ┌───────────────┐  ┌────────────────────────┐   │
  │  │  Undo Logs    │  │    Redo Logs (WAL)     │   │
  │  │ (rollback +   │  │  (crash recovery +     │   │
  │  │   MVCC reads) │  │   durability)          │   │
  │  └───────────────┘  └────────────────────────┘   │
  │                                                  │
  │  ┌───────────────────────────────────────────┐   │
  │  │           Lock Manager                    │   │
  │  │  (row locks, gap locks, next-key locks)   │   │
  │  └───────────────────────────────────────────┘   │
  └──────────────────────┬───────────────────────────┘
                         │
              ┌──────────▼──────────┐
              │    Disk Storage     │
              │  tablespace.ibd     │  ← data + indexes together
              │  ib_logfile0/1      │  ← redo logs
              │  undo tablespace    │  ← undo logs
              └─────────────────────┘
```

**Critical difference from PostgreSQL:** In InnoDB, the `.ibd` file contains both the table data and all its indexes in a single file-per-table. The data is stored as a B+-tree ordered by primary key — not as an unordered heap.

---

## 3. Internal Design

### 3.1 Clustered Indexes

InnoDB's most distinctive feature: the table IS the primary key B+-tree. Row data lives in the leaf pages of the primary key index.

```
Clustered B+-Tree (primary key = id):

              [Root: 500]
             /           \
    [250 | 400]          [700 | 900]
    /    |    \          /    |    \
[Leaf]  [Leaf] [Leaf]  [Leaf][Leaf][Leaf]
 1-249  250-399 400-499 500-699 700-899 900+

Each leaf page contains:
┌────────────────────────────────────────────────────┐
│  Page Header                                       │
│  ┌──────────────────────────────────────────────┐  │
│  │ Row: id=250, name="Alice", email="a@b.com"   │  │
│  │ Row: id=251, name="Bob",   email="b@b.com"   │  │
│  │ ...                                          │  │
│  └──────────────────────────────────────────────┘  │
│  Linked list pointers to prev/next leaf pages      │
└────────────────────────────────────────────────────┘
```

**Why clustered indexes improve lookup performance:**

- A lookup by primary key is a single B+-tree traversal → leaf page → row data, done. No second hop.
- Range scans on primary key are sequential leaf-page reads — physically ordered on disk.
- Compared to PostgreSQL's heap approach: PG does B-tree traversal → get `ctid` → fetch heap page (random I/O). InnoDB eliminates that second random I/O for primary key lookups.

**The cost:** Inserts must go into the "correct" position in the B+-tree by primary key. With sequential auto-increment keys, inserts always go to the rightmost leaf — efficient. With random UUIDs as primary keys, each insert goes to a random position, causing frequent page splits and fragmentation (the "UUID primary key anti-pattern").

### 3.2 Secondary Indexes

Secondary indexes in InnoDB are B+-trees that store the indexed column(s) plus the **primary key value** (not a physical page pointer).

```
Secondary Index on (email):

   B+-tree leaf nodes:
   ┌─────────────────────────────────────┐
   │  email="a@b.com"  → PK=250          │
   │  email="b@b.com"  → PK=251          │
   │  email="c@b.com"  → PK=99           │
   └─────────────────────────────────────┘

Lookup: SELECT * WHERE email = "b@b.com"
1. Traverse secondary index B+-tree → find PK=251
2. Traverse clustered index with PK=251 → get full row
```

This is called a **"double lookup"** or **covering index miss**. Two B+-tree traversals for a non-PK lookup. If the secondary index contains all needed columns (a covering index), step 2 is skipped.

**Why store PK instead of physical pointer?** Because the clustered B+-tree reorganizes pages during splits. A physical pointer would become invalid after a split. The PK is stable — it is always findable via the clustered index regardless of how data pages move.

### 3.3 Buffer Pool

InnoDB's buffer pool is analogous to PostgreSQL's shared buffers — it caches 16KB pages (InnoDB's default page size) in memory.

```
Buffer Pool (LRU-based):

  ┌──────────────────────────────────────────────────┐
  │                                                  │
  │  "Young" sublist (most recently used, 5/8 pool)  │
  │  ┌────┬────┬────┬────┬────┬────┬────┬────┐       │
  │  │ P1 │ P2 │ P3 │ P4 │ P5 │ P6 │ P7 │ P8 │       │
  │  └────┴────┴────┴────┴────┴────┴────┴────┘       │
  │                          ↑ midpoint              │
  │  "Old" sublist (3/8 pool, candidates for evict)  │
  │  ┌────┬────┬────┬────┬────┐                      │
  │  │ P9 │P10 │P11 │P12 │P13 │                      │
  │  └────┴────┴────┴────┴────┘                      │
  │                                                  │
  └──────────────────────────────────────────────────┘
```

InnoDB uses a **midpoint insertion strategy**: newly loaded pages enter the midpoint (not the head). This protects the "young" sublist from a full table scan that would otherwise flush all hot pages. Pages migrate to the young sublist only on a second access. This is InnoDB's solution to the same sequential scan problem PostgreSQL solves with ring buffers.

### 3.4 Undo Logs

Undo logs record the **before-image** of every row modification. They serve two purposes:

**Purpose 1 — Transaction Rollback:**

```
UPDATE accounts SET balance = 200 WHERE id = 1;
-- Undo log records: "id=1 had balance=100"

ROLLBACK;
-- InnoDB reads undo log, restores balance=100
```

**Purpose 2 — MVCC Read Consistency (Oracle-style):**

```
Timeline:
  T1 starts (snapshot: balance=100)
  T2 updates balance=200, commits
  T1 reads balance of id=1

InnoDB does NOT keep multiple versions in the table.
Instead:
1. Read current row: balance=200, modified by T2
2. T2 committed after T1's snapshot → not visible to T1
3. Follow the undo log pointer → reconstruct old version: balance=100
4. Return balance=100 to T1
```

This is fundamentally different from PostgreSQL. PostgreSQL keeps multiple tuple versions in the heap (append-only). InnoDB keeps the current version in the B+-tree and reconstructs old versions by applying undo records backwards.

**Trade-off:** InnoDB's in-place update + undo reconstruction is more space-efficient in the table (no dead tuples in heap), but undo reconstruction adds CPU cost for old-snapshot reads. Long-running transactions accumulate large undo logs, which have their own space and performance implications.

### 3.5 Redo Logs (WAL)

InnoDB's redo log is its WAL — the crash recovery mechanism.

```
Write path:
1. Transaction modifies buffer pool page (in memory)
2. Redo log record written to redo log buffer
3. On COMMIT: redo log buffer flushed to ib_logfile0/1 (fsync)
4. Modified buffer pool pages are NOT immediately written to disk
5. Background thread (page cleaner) eventually flushes dirty pages

Crash recovery:
1. Open ib_logfile0/1, find last checkpoint
2. Re-apply all redo records after checkpoint
3. Read undo logs to roll back any uncommitted transactions
4. Database is in consistent committed state
```

**Why both undo and redo?**

- **Redo** replays committed changes that didn't make it to disk (crash recovery for durability)
- **Undo** rolls back uncommitted changes that did make it to disk (crash recovery for atomicity)

PostgreSQL only needs WAL because its MVCC model doesn't modify tuples in-place — a crash leaves no partial writes that need undo. InnoDB modifies tuples in-place, so a crash can leave a partial write, requiring undo to reverse it. The two-log design is a direct consequence of the in-place update strategy.

### 3.6 Locking: Row Locks, Gap Locks, Next-Key Locks

InnoDB supports row-level locking, which is far more granular than table-level locking.

**Record Lock:** Locks a single index record.

```sql
SELECT * FROM accounts WHERE id = 5 FOR UPDATE;
-- Locks the row where id=5
```

**Gap Lock:** Locks the gap between index records (prevents phantom reads).

```sql
SELECT * FROM accounts WHERE id BETWEEN 5 AND 10 FOR UPDATE;
-- Locks the gap (5, 10) — no other transaction can insert id=6,7,8,9
```

**Next-Key Lock:** A record lock + gap lock on the preceding gap. This is the default lock type in `REPEATABLE READ` isolation level.

```
Index: [1, 3, 5, 7, 9]
Next-key lock on 5 = lock record 5 + gap (3, 5)
```

Next-key locks prevent phantom reads — the scenario where a range query returns different rows in two reads within the same transaction because another transaction inserted into the range. Gap locks make this impossible.

**Trade-off:** Gap locks prevent phantoms but reduce write concurrency. Two transactions doing `INSERT` into the same range can deadlock on gap locks even if they insert different rows.

---

## 4. Design Trade-Offs

### Clustered Index: Fast PK Lookups vs Insert Fragmentation

| Scenario                             | InnoDB (Clustered)    | PostgreSQL (Heap)        |
| ------------------------------------ | --------------------- | ------------------------ |
| `SELECT * WHERE id=X`              | 1 B-tree traversal    | B-tree + heap fetch      |
| `SELECT col WHERE id=X` (covering) | 1 traversal, done     | B-tree + heap fetch      |
| Sequential PK insert                 | Fast (rightmost leaf) | Always O(1), append      |
| Random UUID insert                   | Fragmentation, splits | No fragmentation         |
| `SELECT *` full scan               | Clustered, sequential | Heap order (may scatter) |

### MVCC Model: In-Place vs Append-Only

|                      | InnoDB                | PostgreSQL                 |
| -------------------- | --------------------- | -------------------------- |
| Update mechanism     | In-place + undo log   | Append new tuple           |
| Old version location | Undo log chain        | Heap (same page)           |
| Space for dead rows  | Undo tablespace grows | Heap bloat                 |
| Cleanup mechanism    | Purge thread (undo)   | VACUUM (heap)              |
| Long txn impact      | Undo grows large      | Heap bloat, VACUUM delayed |

**InnoDB advantage:** No heap bloat — the primary key B+-tree stays compact.
**InnoDB disadvantage:** Reconstructing old versions requires following the undo chain, which can be slow for very old snapshots or deeply nested undo chains.

### Locking Model Trade-Off

InnoDB's gap locking at `REPEATABLE READ` prevents phantoms but creates more lock contention. Applications that need high insert throughput on a range often drop to `READ COMMITTED`, which removes gap locks at the cost of allowing phantom reads.

---

## 5. Experiments / Observations

### Experiment 1: Clustered Index vs Secondary Index Lookup

```sql
-- Table setup
CREATE TABLE users (
  id INT AUTO_INCREMENT PRIMARY KEY,
  email VARCHAR(255),
  name VARCHAR(100),
  INDEX idx_email (email)
);

-- Insert 1M rows
INSERT INTO users (email, name)
  SELECT CONCAT('user', seq, '@example.com'), CONCAT('User ', seq)
  FROM seq_1_to_1000000;

-- Primary key lookup (1 traversal)
EXPLAIN SELECT * FROM users WHERE id = 500000;
-- type: const, rows: 1, Extra: (none)  ← single direct lookup

-- Secondary index lookup (2 traversals: secondary → clustered)
EXPLAIN SELECT * FROM users WHERE email = 'user500000@example.com';
-- type: ref, key: idx_email, rows: 1, Extra: (none)
-- Note: InnoDB does secondary index lookup → PK → clustered lookup

-- Covering index (1 traversal, no clustered lookup needed)
EXPLAIN SELECT id, email FROM users WHERE email = 'user500000@example.com';
-- type: ref, key: idx_email, rows: 1, Extra: Using index
-- "Using index" = covered by index, no table access needed
```

**Observation:** The "Using index" flag confirms a covering index avoids the second B+-tree traversal. For read-heavy workloads on specific columns, designing covering indexes is a significant performance optimization in InnoDB.

### Experiment 2: MVCC and Undo Log Growth

```sql
-- Start a long-running transaction (simulate analytics query)
START TRANSACTION;
SELECT COUNT(*) FROM orders;  -- take snapshot

-- In another session, run 100k updates
UPDATE orders SET status = 'processed' WHERE status = 'pending' LIMIT 100000;

-- Back in long transaction — read old version
SELECT COUNT(*) FROM orders WHERE status = 'pending';
-- InnoDB reads current rows, finds they were modified after our snapshot,
-- follows undo log chain to reconstruct pre-update versions
-- This is slower than reading current data

-- Check undo log usage
SELECT * FROM information_schema.INNODB_TRX\G
-- trx_query, trx_rows_modified, trx_id visible for the long transaction

COMMIT;  -- undo log can now be purged
```

**Observation:** The long-running transaction holds a read snapshot. During its lifetime, InnoDB's purge thread cannot reclaim the undo records those modifications need. Undo tablespace grows. After commit, the purge thread catches up. This is the InnoDB equivalent of PostgreSQL's "long transaction blocking VACUUM" problem.

### Experiment 3: Gap Lock Deadlock

```sql
-- Session A
BEGIN;
SELECT * FROM orders WHERE id BETWEEN 100 AND 200 FOR UPDATE;
-- Acquires next-key lock on (100, 200]

-- Session B (concurrent)
BEGIN;
INSERT INTO orders (id, ...) VALUES (150, ...);
-- BLOCKED — id=150 falls in gap locked by Session A

-- If Session A then tries to insert into a range Session B has locked:
INSERT INTO orders (id, ...) VALUES (180, ...);
-- DEADLOCK detected — InnoDB rolls back one transaction
```

**Observation:** Gap locks prevent phantoms but introduce deadlock scenarios that wouldn't exist with `READ COMMITTED` isolation. Application code must handle deadlock errors (retry logic) when using `REPEATABLE READ` with range queries.

---

## 6. Key Learnings

**Clustered indexes are an architectural commitment.** The decision to make the primary key the physical row order is baked into InnoDB's design. This makes primary key selection critical — auto-increment integers are ideal; random UUIDs cause fragmentation. This is not a configurable option; it is the storage model.

**Two logs (undo + redo) are the cost of in-place updates.** PostgreSQL's append-only MVCC only needs WAL. InnoDB modifies rows in place, which requires undo for both rollback and MVCC reads, and redo for crash recovery of committed changes. More complexity, but tighter storage in the B+-tree.

**Gap locks are a deliberate phantom-prevention mechanism.** They are not a bug or a limitation — they are the implementation of `REPEATABLE READ` isolation semantics. If your application doesn't need phantom prevention, `READ COMMITTED` removes gap locks and improves write concurrency significantly.

**The buffer pool midpoint insertion is the same insight as PostgreSQL's ring buffers.** Both systems independently arrived at the same solution: protect the hot page cache from full table scans by not promoting pages to the most-recently-used position on first access.

**InnoDB and PostgreSQL chose opposite ends of the MVCC spectrum.** PostgreSQL: store all versions in the heap, no undo needed, must VACUUM periodically. InnoDB: store current version in B+-tree, undo chain for old versions, purge thread handles cleanup. Both achieve snapshot isolation; the difference is where "old" data lives and how it is reclaimed.
