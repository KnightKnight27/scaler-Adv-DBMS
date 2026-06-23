# MySQL / InnoDB Storage Engine

**Course:** Advanced Database Management Systems  
**Student:** Vivek Anand Singh | 23BCS10172

---

## 1. Problem Background

MySQL's InnoDB storage engine was built to solve a problem that MyISAM (MySQL's original engine) couldn't: *how do you provide ACID transactions, crash recovery, and row-level locking without sacrificing the raw insert/read speed that made MySQL popular?*

MyISAM was fast but table-level locking meant a single UPDATE would block all SELECTs on that table. InnoDB, developed by Heikki Tuuri at Innobase Oy (acquired by Oracle in 2005), replaced table locks with row-level locks and added a full MVCC implementation — but chose a fundamentally different MVCC model than PostgreSQL.

The key design choice: **InnoDB stores rows in-place and uses an undo log for old versions**, while PostgreSQL stores multiple versions in the heap and uses VACUUM for cleanup. Both achieve the same MVCC semantics through opposite approaches. Understanding why InnoDB made its choice is the core lesson of this topic.

---

## 2. Architecture Overview

```
         Client → MySQL Server Layer
                        │
              ┌─────────▼──────────────┐
              │   SQL Parser           │
              │   Query Optimizer      │
              │   Query Cache          │
              └─────────┬──────────────┘
                        │ Storage Engine API (handler interface)
              ┌─────────▼──────────────────────────────────┐
              │              InnoDB Storage Engine          │
              │                                             │
              │  ┌─────────────────────────────────────┐   │
              │  │          Buffer Pool                 │   │
              │  │  (default 128 MB, typically 70-80%  │   │
              │  │   of available RAM)                  │   │
              │  └──────────┬──────────────────────────┘   │
              │             │                               │
              │  ┌──────────▼──────────┐                   │
              │  │  Tablespace Files   │  (.ibd per table) │
              │  │  Clustered B-tree   │                   │
              │  │  Secondary indexes  │                   │
              │  └─────────────────────┘                   │
              │                                             │
              │  ┌─────────────────────────────────────┐   │
              │  │  Undo Logs  (old row versions)       │   │
              │  └─────────────────────────────────────┘   │
              │                                             │
              │  ┌─────────────────────────────────────┐   │
              │  │  Redo Log (ib_logfile0, ib_logfile1) │   │
              │  └─────────────────────────────────────┘   │
              └────────────────────────────────────────────┘
```

Unlike PostgreSQL (which has one engine), MySQL has a pluggable storage engine architecture. InnoDB implements the `handler` interface that MySQL's server layer calls for every table operation. The server layer handles parsing and planning; InnoDB handles storage, locking, and transactions.

---

## 3. Internal Design

### 3.1 Clustered Index — The Core of InnoDB's Storage Model

**In InnoDB, the primary key IS the table.** There is no separate "heap file" as in PostgreSQL. The table data is stored directly in a B-tree ordered by primary key. This B-tree is called the **clustered index**.

```
Clustered B-tree (Primary Key = user_id)
                        [50 | 100]
                       /     |     \
            [10|20|30]   [60|70|80]  [110|120|130]
           /   |   |        ...
      leaf: (10, 'Alice', 25, ...)
            (20, 'Bob',   31, ...)   ← full row data here
            (30, 'Carol', 28, ...)
```

Every leaf node of the clustered index stores the **complete row** — not just a pointer to a heap file, but all columns. A primary key lookup is: traverse the B-tree → arrive at the leaf → read the row. Done. No secondary lookup.

**Consequence:** Rows with adjacent primary keys are physically stored on the same or adjacent B-tree leaf pages. A query `WHERE user_id BETWEEN 1000 AND 2000` reads 1000 sequential rows from contiguous disk pages — excellent cache and prefetch behavior.

**Danger of random primary keys:**  
If the primary key is a UUID (random), each INSERT lands at a random leaf position. Every insert potentially requires reading a cold page into the buffer pool, dirtying it, and eventually flushing it — even for sequential workloads. This is called "B-tree page thrashing." InnoDB documentation specifically recommends auto-increment integer primary keys for this reason.

### 3.2 Secondary Indexes

Secondary indexes in InnoDB do NOT store the row. They store `(index_key, primary_key_value)`. A lookup via secondary index requires:
1. Traverse secondary B-tree → find `(index_key, pk_value)`
2. Traverse clustered B-tree using `pk_value` → find the actual row

This second traversal is called a **bookmark lookup** or **back-to-table lookup**. For a query like:
```sql
SELECT name, email FROM users WHERE email = 'alice@example.com';
```
InnoDB finds the primary key from the email index, then reads the full row from the clustered index. This is two B-tree traversals per row.

**Covering index optimization:**  
If all columns requested are present in the index itself, InnoDB can skip the bookmark lookup:
```sql
CREATE INDEX idx_email_name ON users(email, name);
SELECT name FROM users WHERE email = 'alice@example.com';
-- Only needs the secondary index, no back-to-table
```

### 3.3 Buffer Pool

The buffer pool is InnoDB's page cache — analogous to PostgreSQL's shared buffers. Key differences:

- InnoDB's buffer pool uses an **LRU variant with a "midpoint insertion"** strategy. New pages are inserted at the midpoint (⅜ from the tail by default), not the head. This protects frequently accessed "hot" pages from being evicted by a full table scan (which inserts at midpoint, not head).
- The buffer pool is divided into **pages (16 KB default)** — larger than PostgreSQL's 8 KB. Larger pages reduce tree depth for indexes but increase I/O for point lookups.
- **Adaptive Hash Index:** InnoDB monitors frequently accessed B-tree paths. If a specific key is looked up repeatedly, InnoDB builds an in-memory hash index for it automatically. This turns O(log n) B-tree lookups into O(1) for hot keys — transparent to the application.

### 3.4 MVCC via Undo Logs

This is where InnoDB differs most fundamentally from PostgreSQL.

**PostgreSQL's approach:** Keep multiple versions of a row in the heap. Old versions become dead tuples; VACUUM removes them.

**InnoDB's approach:** Keep one "current" version of a row in the clustered index. When a transaction needs to see an older version, InnoDB constructs it by **applying undo log records in reverse.**

```
Clustered index (current version):
  Row: (id=1, balance=1500, trx_id=200, roll_ptr=→)
                                                   │
                                                   ▼
Undo log segment:
  Undo record: (id=1, balance=1000, trx_id=100)  ← previous version
```

When transaction T (with snapshot before trx_id 200) reads row id=1:
1. Reads current version from clustered index: balance=1500, trx_id=200
2. 200 > T's snapshot → not visible
3. Follows `roll_ptr` to undo log → reads balance=1000, trx_id=100
4. 100 ≤ T's snapshot → visible → returns 1000

**The critical difference:** PostgreSQL's old versions live in the heap alongside new versions and must be vacuumed. InnoDB's old versions live in a separate undo log and are purged by the **purge thread** once no active transaction needs them. The clustered index always has exactly one version per row.

### 3.5 Redo Log

The redo log (`ib_logfile0`, `ib_logfile1`) is InnoDB's WAL — changes are written here before the buffer pool pages are flushed to disk.

**Why both undo and redo?**
- **Redo log** = durability. If the server crashes after a committed transaction but before the dirty buffer pool pages were written to disk, the redo log lets InnoDB replay those changes on restart.
- **Undo log** = atomicity + MVCC. If a transaction is aborted (crash or explicit ROLLBACK), InnoDB uses the undo log to reverse all changes. Also used to reconstruct old versions for MVCC reads.

PostgreSQL doesn't need a separate undo log because its MVCC model stores old versions in the heap — rollback just means the new tuple's `t_xmin` is marked aborted in CLOG, so it becomes invisible. The "undo" is implicit in the visibility rules.

### 3.6 Locking: Row Locks and Gap Locks

InnoDB locks are set at the **index entry** level, not the row level. This distinction matters.

**Record lock:** Locks a specific index entry. `SELECT ... WHERE id = 5 FOR UPDATE` locks the index entry for id=5.

**Gap lock:** Locks the gap between two index entries. Prevents insertions into the gap. `WHERE id BETWEEN 5 AND 10` locks not just the rows but the gap, preventing phantom reads.

**Next-key lock:** Record lock + gap lock on the preceding gap. Default in REPEATABLE READ isolation.

**Why gap locks?** The REPEATABLE READ isolation level must prevent phantom reads (a second execution of the same range query returning more rows). Gap locks block concurrent INSERT operations in the locked range, preventing phantoms without serializing the entire table.

**Trade-off:** Gap locks can cause surprising deadlocks. Two transactions, each trying to insert different keys in the same gap, will deadlock — each holds a gap lock and waits for the other to release it. This is a common source of production deadlocks in InnoDB applications.

---

## 4. Design Trade-Offs

### Clustered Index: Advantages and Costs

| Advantage | Cost |
|---|---|
| Primary key lookup = single B-tree traversal | Random PK inserts cause page thrashing |
| Range scans on PK are sequential I/O | Secondary index lookups need two traversals |
| Row and its PK are co-located | Schema must have a PK (InnoDB creates a hidden one if missing) |
| INSERT order matches disk order for auto-increment PKs | UUID primary keys are an anti-pattern |

### InnoDB MVCC vs PostgreSQL MVCC

| Property | InnoDB (undo log) | PostgreSQL (heap versions) |
|---|---|---|
| Current version location | Clustered index (fixed) | Anywhere in heap |
| Old version location | Undo log segments | Same heap, as dead tuples |
| Rollback cost | Read + apply undo records | Mark xmin as aborted in CLOG |
| Long transaction impact | Undo log grows, can't be purged | Dead tuples accumulate in heap |
| Cleanup mechanism | Purge thread | VACUUM |
| Read path for old snapshots | Traverse undo chain | Find old tuple version in heap |

PostgreSQL's model is simpler for reads: old versions are right in the heap. InnoDB's model keeps the heap clean but adds complexity to reads of old snapshots (must follow the undo chain).

### Why InnoDB needs both undo and redo logs but PostgreSQL only needs WAL

PostgreSQL's WAL records are logical enough to replay forward for crash recovery. Rollbacks are handled implicitly by marking transactions as aborted in CLOG. There are no "undo" operations — the heap just contains a version the aborted transaction inserted, which becomes invisible once CLOG marks the transaction aborted.

InnoDB's in-place update model requires physical undo to reverse changes. If a transaction inserts a row and crashes before commit, the row is physically in the clustered index — InnoDB must physically remove it using the undo log.

---

## 5. Experiments / Observations

### 5.1 Clustered Index Lookup vs Secondary Index Lookup

```sql
CREATE TABLE orders (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    amount DECIMAL(10,2),
    INDEX idx_user (user_id)
);

INSERT INTO orders (user_id, amount)
    SELECT FLOOR(RAND()*1000), RAND()*1000
    FROM information_schema.columns
    LIMIT 100000;

-- Clustered index (PK) lookup:
EXPLAIN SELECT * FROM orders WHERE id = 50000;
-- type: const  | rows: 1 | Extra: (none)

-- Secondary index lookup (requires back-to-table):
EXPLAIN SELECT * FROM orders WHERE user_id = 500;
-- type: ref | rows: ~100 | Extra: (none)
-- Uses idx_user, then fetches full rows from clustered index

-- Covering index (no back-to-table):
EXPLAIN SELECT user_id, amount FROM orders WHERE user_id = 500;
-- Extra: Using index  ← no back-to-table needed
```

### 5.2 Observing Undo Log Growth During Long Transaction

```sql
-- Terminal 1: Start a transaction and hold it open
START TRANSACTION;
SELECT * FROM orders WHERE id = 1;   -- just read, hold open

-- Terminal 2: Run 10,000 updates
UPDATE orders SET amount = amount * 1.01;

-- Terminal 3: Check undo log size
SELECT NAME, SUBSYSTEM, COMMENT
FROM information_schema.INNODB_METRICS
WHERE NAME LIKE '%undo%';
-- undo_log_pages grows as Terminal 1's open snapshot
-- prevents the purge thread from reclaiming undo pages
```

The undo log grows proportionally to the amount of DML committed while Terminal 1's snapshot remains open. This is the InnoDB equivalent of PostgreSQL's dead tuple bloat — different storage location, same root cause: a long-running transaction holding back version cleanup.

### 5.3 EXPLAIN Output — Hash Join vs Nested Loop

```sql
EXPLAIN FORMAT=JSON
SELECT u.name, SUM(o.amount)
FROM orders o JOIN users u ON o.user_id = u.id
GROUP BY u.name;
```

**MySQL 8.0 output (hash join added in 8.0.18):**
```json
{
  "query_block": {
    "grouping_operation": {
      "table": "u", "access_type": "ALL",
      "join_type": "hash",
      "attached_condition": "...",
      "used_tables": ["o", "u"]
    }
  }
}
```

MySQL's optimizer chooses hash join when the join column has no useful index or when the optimizer estimates it's cheaper. Prior to 8.0.18, MySQL only supported nested-loop joins — hash join was a significant addition.

---

## 6. Key Learnings

**1. The clustered index is the most impactful structural decision in InnoDB.**  
Choosing a poor primary key (UUID, random hash) causes every insert to touch a random B-tree page — defeating the sequential I/O benefits of the clustered index. Auto-increment integer PKs are not just convention; they are a performance requirement for write-heavy InnoDB tables.

**2. InnoDB and PostgreSQL have opposite MVCC storage strategies, but identical semantics.**  
Both deliver snapshot isolation. PostgreSQL puts old versions in the heap next to new versions. InnoDB puts old versions in a separate undo log and keeps the clustered index clean. Neither approach is universally better — InnoDB's undo approach keeps the primary data structure clean but requires undo log I/O for historical reads.

**3. The purge thread is InnoDB's equivalent of VACUUM.**  
Both must reclaim space used by transactions no longer visible to any active snapshot. Both are blocked by long-running transactions. The failure mode is different (undo log growth vs heap table bloat), but the root cause — retaining old versions longer than necessary — is identical.

**4. Gap locks are the hidden cost of REPEATABLE READ.**  
Most application developers are surprised when two non-conflicting INSERTs deadlock in InnoDB. Gap locks on index ranges are the cause. Switching to READ COMMITTED eliminates gap locks but allows phantom reads. This is a genuine trade-off between isolation and concurrency that every InnoDB-based application must understand.

**5. The covering index is the most underused InnoDB optimization.**  
If a query's WHERE, JOIN, and SELECT columns are all present in one index, InnoDB never touches the clustered index. For read-heavy reporting queries on large tables, the difference between a covering index and a back-to-table lookup can be an order of magnitude in query time.
