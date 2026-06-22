# MySQL / InnoDB Storage Engine

**Author:** Praveen Kumar | 24BCS10048

---

## 1. Problem Background

InnoDB became MySQL's default storage engine in version 5.5 (2010), replacing MyISAM. The fundamental motivation was simple: MyISAM had no crash recovery and no row-level locking. A crashed MyISAM table required a full repair. A write to any row locked the entire table.

InnoDB was originally developed by Innobase Oy (Heikki Tuuri, Finland) and later acquired by Oracle. Its design draws heavily from Oracle Database's architecture: clustered indexes, undo logs, redo logs, and Oracle-style MVCC. This makes InnoDB architecturally very different from PostgreSQL, even though both are ACID-compliant RDBMS engines.

---

## 2. Architecture Overview

```
MySQL Server Layer
  |
  +-- Parser -> Optimizer -> Executor
  |
  v
InnoDB Storage Engine API
  |
  +-----+------+------+------+
  |     |      |      |      |
  v     v      v      v      v
Buffer  Redo   Undo   Lock   Purge
Pool    Log    Log    Mgr    Thread
  |     |      |
  v     v      v
  +-----------+
  | Tablespace Files |
  | (*.ibd)          |
  +------------------+
```

InnoDB's core principle: **in-place updates with undo logs for versioning and redo logs for durability.** This is the opposite of PostgreSQL's append-only heap.

---

## 3. Internal Design

### 3.1 Clustered Index

This is the most important architectural difference from PostgreSQL: **the table data IS the primary key index.**

In InnoDB, every table is stored as a B+ tree ordered by the primary key. The leaf pages of this B+ tree contain the actual row data. There is no separate heap file.

```
Clustered Index (B+ tree):

        [Internal: 10, 20, 30]
       /        |        |        \
  [1-9]     [10-19]   [20-29]   [30-39]
  (rows)    (rows)    (rows)    (rows)
```

Consequences:
- **Primary key lookup is a single B+ tree traversal.** No separate heap fetch needed.
- **Range scans on the primary key are sequential I/O.** Adjacent keys are on adjacent pages.
- **Inserts with a non-sequential primary key cause page splits.** Using UUIDs as PKs is expensive in InnoDB because random inserts fragment the tree. Auto-increment IDs avoid this.
- **Secondary indexes store the primary key value (not a row pointer) as the "address."** A secondary index lookup requires two B+ tree traversals: one in the secondary index to find the PK, then one in the clustered index to fetch the row. This is called a "double lookup."

PostgreSQL avoids the double-lookup problem because its indexes store physical TIDs (file offset, page number). But PostgreSQL pays a different cost: heap pages can become fragmented after updates/deletes.

### 3.2 Secondary Indexes

A secondary index in InnoDB is a B+ tree where:
- Internal nodes contain `(indexed column values, child pointer)`
- Leaf nodes contain `(indexed column values, primary key value)`

```
Secondary index on "name":

  Leaf entry:  ("Alice", PK=1)
  Leaf entry:  ("Bob",   PK=2)
  ...
```

To resolve a secondary index lookup:
1. Traverse the secondary index B+ tree to find the matching leaf entry.
2. Read the PK value from the leaf entry.
3. Traverse the clustered index B+ tree using that PK to fetch the full row.

**Covering indexes** avoid step 3: if all columns needed by the query are in the secondary index, InnoDB reads them directly from the index leaf page without touching the clustered index. This is why `CREATE INDEX idx ON t(a, b, c)` can be powerful for queries that only need columns a, b, c.

### 3.3 Buffer Pool

InnoDB's buffer pool (`innodb_buffer_pool_size`) caches both data pages and index pages. It uses a modified LRU with two sublists:

```
Buffer Pool LRU:
  [Young sublist: hot pages] -- [Old sublist: recently loaded pages]
       (5/8 of pool)                    (3/8 of pool)
```

New pages enter at the head of the old sublist, not the young sublist. A page moves to the young sublist only after it's accessed again after a configurable delay (`innodb_old_blocks_time`, default 1000ms). This prevents a full table scan from evicting the entire working set -- scan pages enter the old sublist and are evicted quickly if not re-accessed.

This is more sophisticated than PostgreSQL's clock sweep. The trade-off: the two-sublist LRU requires more bookkeeping per access but gives better protection against scan pollution.

### 3.4 Undo Logs

When InnoDB modifies a row, it first writes the old version to the undo log (a separate area within the tablespace). This serves two purposes:

1. **Rollback:** If the transaction is aborted, the undo log entries are applied in reverse to restore the original rows.
2. **MVCC:** Other transactions that need to see older versions follow the undo chain from the current row back through previous versions until they find one visible to their snapshot.

```
Current row (in clustered index):
  name = "Bob_v3", trx_id = 300, rollback_ptr -> undo log

Undo log:
  [entry 3]: name was "Bob_v2", trx_id = 200, prev -> entry 2
  [entry 2]: name was "Bob_v1", trx_id = 100, prev -> entry 1
  [entry 1]: name was "Bob",    trx_id = 50,  prev -> NULL
```

A reader at snapshot 150 follows the chain: current row has trx_id=300 (too new), undo entry 3 has trx_id=200 (too new), undo entry 2 has trx_id=100 (visible). Returns "Bob_v1".

**Comparison with PostgreSQL:** PostgreSQL keeps all versions in the heap. InnoDB keeps only the latest version in the B+ tree and older versions in the undo log. This means:
- InnoDB's clustered index stays compact (no dead tuples).
- InnoDB doesn't need VACUUM.
- But: long-running transactions in InnoDB prevent undo log truncation, causing "undo tablespace bloat" -- the same fundamental problem, different location.

### 3.5 Redo Logs (WAL equivalent)

InnoDB's redo log serves the same purpose as PostgreSQL's WAL: durability. Before any page modification is written to the tablespace file, the change is recorded in the redo log.

```
Redo log pipeline:

  1. Transaction modifies a page in buffer pool
  2. Change is recorded in the log buffer (in memory)
  3. On COMMIT: log buffer is flushed to redo log files on disk (fsync)
  4. Modified page stays in buffer pool (dirty)
  5. Eventually, checkpoint writes dirty pages to tablespace files
  6. After checkpoint, old redo log entries are no longer needed
```

InnoDB uses a circular redo log: two (or more) fixed-size files that wrap around. When the redo log is full, a checkpoint is forced. This is why `innodb_log_file_size` is critical for write-heavy workloads -- too small and checkpoints happen too frequently, causing write stalls.

**Why InnoDB needs BOTH undo and redo logs:**
- Redo log = "what changes were made" (for crash recovery: redo committed work)
- Undo log = "what the old values were" (for rollback: undo uncommitted work, and for MVCC: reconstruct old versions)

PostgreSQL doesn't need undo logs because old tuple versions persist in the heap. The WAL alone is sufficient for recovery because PostgreSQL's MVCC keeps all versions in-place.

### 3.6 Locking: Row-Level and Gap Locks

InnoDB implements row-level locking with several lock types:

| Lock Type | What it locks | Purpose |
|-----------|-------------|---------|
| Record lock | A single index record | Prevent other txns from modifying this row |
| Gap lock | The gap between two index records | Prevent inserts into ranges (phantom prevention) |
| Next-key lock | Record + gap before it | Default for REPEATABLE READ; prevents phantoms |
| Insert intention lock | A gap (for INSERT) | Signals intent to insert; doesn't block other inserters at different positions |

**Gap locks are unique to InnoDB** and are how it achieves REPEATABLE READ without full table locks. PostgreSQL uses a different approach: Serializable Snapshot Isolation (SSI) with predicate locks.

Example of gap locking:
```sql
-- Transaction 1 (REPEATABLE READ):
SELECT * FROM orders WHERE amount BETWEEN 100 AND 200 FOR UPDATE;
-- This locks: all existing rows with amount in [100, 200]
--             PLUS the gaps between them
--             PLUS the gap before 100 and after 200

-- Transaction 2:
INSERT INTO orders (amount) VALUES (150);
-- BLOCKED: gap lock prevents insertion in [100, 200] range
-- This prevents "phantom reads" for Transaction 1
```

---

## 4. Design Trade-Offs

| Decision | InnoDB | PostgreSQL |
|----------|--------|-----------|
| Update strategy | In-place + undo log | Append new tuple + VACUUM |
| Clustered index | Yes (PK = table storage) | No (heap + separate indexes) |
| Secondary index cost | Double lookup (index -> PK -> clustered) | Single lookup (index -> TID -> heap) |
| MVCC old versions | Undo log (separate storage) | Heap (same table file) |
| Cleanup mechanism | Purge thread (trims undo log) | VACUUM (marks dead tuples reusable) |
| Phantom prevention | Gap locks | SSI predicate locks |
| Buffer replacement | Two-sublist LRU | Clock sweep |
| Random PK insert cost | High (B+ tree splits) | Low (append to heap) |

**Key insight:** InnoDB's clustered index design is optimized for **read-heavy OLTP with sequential primary keys** (auto-increment). PostgreSQL's heap design is optimized for **workloads with diverse access patterns** where any column might be queried.

---

## 5. Experiments / Observations

### Clustered vs. non-clustered PK performance

```sql
-- InnoDB: primary key range scan
SELECT * FROM orders WHERE id BETWEEN 1000 AND 2000;
-- Reads ~10 contiguous leaf pages (sequential I/O)

-- PostgreSQL: same query on a heap table
SELECT * FROM orders WHERE id BETWEEN 1000 AND 2000;
-- B-tree index scan -> up to 1000 random heap page fetches (random I/O)
```

For primary key range scans, InnoDB can be 10-50x faster because the data is physically sorted by PK. PostgreSQL's Bitmap Index Scan partially mitigates this by batching heap fetches, but can't match true sequential I/O.

### Undo log growth under long transactions

```sql
-- Start a long-running read transaction
START TRANSACTION;
SELECT * FROM large_table LIMIT 1;
-- (don't commit)

-- Meanwhile, other transactions update rows in large_table
-- InnoDB cannot purge undo log entries because our snapshot needs them
```

After running updates on 1M rows while a read transaction stays open:
```
Undo tablespace: grew from 10 MB to 450 MB
```

This is analogous to PostgreSQL's table bloat from dead tuples. The problem is the same: long-running transactions prevent cleanup. The location differs (undo tablespace vs. heap pages), but the operational impact is similar.

### Gap lock contention observation

```sql
-- Two transactions at REPEATABLE READ:
-- T1: SELECT * FROM t WHERE val BETWEEN 10 AND 20 FOR UPDATE;
-- T2: INSERT INTO t (val) VALUES (15);
-- T2 blocks on T1's gap lock

-- Same scenario in PostgreSQL (REPEATABLE READ):
-- T2's INSERT succeeds immediately (no gap locks)
-- T1's re-read may see the phantom (PostgreSQL doesn't prevent phantoms at RR)
-- Must use SERIALIZABLE for phantom prevention in PostgreSQL
```

InnoDB's gap locks prevent phantoms at REPEATABLE READ, which is stronger than SQL standard requires. PostgreSQL's REPEATABLE READ allows phantoms (it only prevents at SERIALIZABLE). This is a deliberate trade-off: InnoDB chooses stronger isolation at the cost of more lock contention.

---

## 6. Key Learnings

1. **Clustered indexes are a double-edged sword.** They give incredible PK lookup and range scan performance but make secondary index lookups more expensive (double lookup) and random PK inserts costly (page splits). The choice of primary key matters far more in InnoDB than in PostgreSQL.

2. **Undo + redo is not redundant.** They serve orthogonal purposes: redo replays committed work after a crash, undo reverses uncommitted work and provides MVCC. PostgreSQL avoids needing undo by keeping all versions in the heap, but pays for it with VACUUM.

3. **Gap locks solve phantoms but cause contention.** InnoDB's gap locking gives stronger isolation guarantees at REPEATABLE READ than PostgreSQL, but it can cause unexpected blocking in range queries. Understanding when gap locks fire is essential for troubleshooting InnoDB performance.

4. **Buffer pool management matters.** InnoDB's two-sublist LRU is specifically designed to resist scan pollution. PostgreSQL's clock sweep is simpler but less resistant to sequential scan eviction (though PostgreSQL has ring buffers for large scans as a separate mitigation).

5. **There is no free lunch in MVCC.** Both InnoDB (undo log) and PostgreSQL (dead tuples) accumulate old version data that must be cleaned up. Long-running transactions are the enemy in both systems.

---

## References

- MySQL InnoDB documentation: https://dev.mysql.com/doc/refman/8.0/en/innodb-storage-engine.html
- MySQL source: `storage/innobase/`
- Tuuri, H. "InnoDB: A High-Performance Transaction Storage Engine" (MySQL Conference, 2005)
- Schwartz, B. et al. *High Performance MySQL*, 4th ed., Ch. 1 and 6
- InnoDB internals: https://blog.jcole.us/innodb/
