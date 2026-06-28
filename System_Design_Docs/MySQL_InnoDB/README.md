# MySQL / InnoDB Storage Engine

**Course:** Advanced Database Management Systems
**Student:** Indrajeet Yadav | 23BCS10199

---

## 1. Problem Background

MySQL's InnoDB storage engine was built to solve a problem that MyISAM (MySQL's original engine) couldn't: *how do you provide ACID transactions, crash recovery, and row-level locking without sacrificing the raw insert/read speed that made MySQL popular?*

MyISAM was fast but table-level locking meant a single UPDATE would block all SELECTs on that table. InnoDB, developed by Heikki Tuuri at Innobase Oy (acquired by Oracle in 2005), replaced table locks with row-level locks and added a full MVCC implementation — but chose a fundamentally different MVCC model than PostgreSQL.

The key design choice: **InnoDB stores rows in-place and uses an undo log for old versions**, while PostgreSQL stores multiple versions in the heap and uses VACUUM for cleanup. Both achieve the same MVCC semantics through opposite approaches.

---

## 2. Architecture Overview

```
         Client -> MySQL Server Layer
                        |
              +---------v--------------+
              |   SQL Parser           |
              |   Query Optimizer      |
              |   Query Cache          |
              +---------+--------------+
                        | Storage Engine API (handler interface)
              +---------v------------------------------------------+
              |              InnoDB Storage Engine                  |
              |                                                     |
              |  +---------------------------------------------+   |
              |  |          Buffer Pool                         |   |
              |  |  (default 128 MB, typically 70-80% of RAM)  |   |
              |  +------------------+--------------------------+   |
              |                     |                               |
              |  +------------------v------------------+           |
              |  |  Tablespace Files   (.ibd per table) |           |
              |  |  Clustered B-tree                    |           |
              |  |  Secondary indexes                   |           |
              |  +--------------------------------------+           |
              |                                                     |
              |  +---------------------------------------------+   |
              |  |  Undo Logs  (old row versions)               |   |
              |  +---------------------------------------------+   |
              |                                                     |
              |  +---------------------------------------------+   |
              |  |  Redo Log (ib_logfile0, ib_logfile1)         |   |
              |  +---------------------------------------------+   |
              +-----------------------------------------------------+
```

Unlike PostgreSQL (which has one engine), MySQL has a pluggable storage engine architecture. InnoDB implements the `handler` interface that MySQL's server layer calls for every table operation.

---

## 3. Internal Design

### 3.1 Clustered Index — The Core of InnoDB's Storage Model

**In InnoDB, the primary key IS the table.** The table data is stored directly in a B-tree ordered by primary key — the **clustered index**.

```
Clustered B-tree (Primary Key = user_id)
                        [50 | 100]
                       /     |     \
            [10|20|30]   [60|70|80]  [110|120|130]
           /   |   |        ...
      leaf: (10, 'Alice', 25, ...)
            (20, 'Bob',   31, ...)   <- full row data here
            (30, 'Carol', 28, ...)
```

Every leaf node stores the **complete row** — not just a pointer to a heap file. A primary key lookup is: traverse the B-tree → arrive at the leaf → read the row. Done.

**Danger of random primary keys:** UUID primary keys cause each INSERT to land at a random leaf position, requiring a cold page read on every insert — "B-tree page thrashing." Auto-increment integer PKs are strongly recommended.

### 3.2 Secondary Indexes

Secondary indexes in InnoDB store `(index_key, primary_key_value)`. A lookup via secondary index requires two B-tree traversals: one on the secondary index, one on the clustered index (**bookmark lookup**).

**Covering index optimization:** If all requested columns exist in the index itself, InnoDB skips the bookmark lookup entirely (`Using index` in EXPLAIN).

### 3.3 Buffer Pool

InnoDB's buffer pool uses an **LRU variant with midpoint insertion**: new pages go to the midpoint (3/8 from tail), protecting hot pages from full table scan eviction.

Key features:
- Pages are 16 KB (vs PostgreSQL's 8 KB)
- **Adaptive Hash Index**: automatically builds in-memory hash for frequently accessed B-tree paths, turning O(log n) lookups into O(1)

### 3.4 MVCC via Undo Logs

**PostgreSQL's approach:** Multiple versions in the heap; VACUUM removes dead ones.

**InnoDB's approach:** One current version in the clustered index; old versions reconstructed from the **undo log** on demand.

```
Clustered index (current version):
  Row: (id=1, balance=1500, trx_id=200, roll_ptr=->)
                                                    |
                                                    v
Undo log segment:
  Undo record: (id=1, balance=1000, trx_id=100)  <- previous version
```

When a transaction with snapshot < trx_id 200 reads row id=1: it reads 1500, sees trx_id=200 > snapshot, follows `roll_ptr` to undo log, reads 1000 (trx_id=100 <= snapshot) → returns 1000.

### 3.5 Redo Log

The redo log (`ib_logfile0`, `ib_logfile1`) is InnoDB's WAL.

- **Redo log** = durability: replays committed changes after crash
- **Undo log** = atomicity + MVCC: reverses aborted changes and serves old versions

PostgreSQL doesn't need a separate undo log because rollback is implicit: mark the tuple's xmin as aborted in CLOG → it becomes invisible.

### 3.6 Locking: Record Locks and Gap Locks

**Record lock:** Locks a specific index entry.
**Gap lock:** Locks the gap between two index entries — prevents phantom inserts.
**Next-key lock:** Record lock + gap on preceding gap. Default in REPEATABLE READ.

Gap locks prevent phantom reads but can cause surprising deadlocks: two transactions inserting different keys into the same gap will deadlock.

---

## 4. Design Trade-Offs

### Clustered Index: Advantages and Costs

| Advantage | Cost |
|---|---|
| PK lookup = single B-tree traversal | Random PK inserts cause page thrashing |
| Range scans on PK are sequential I/O | Secondary index needs two traversals |
| Row and PK co-located | Schema must have a PK |

### InnoDB MVCC vs PostgreSQL MVCC

| Property | InnoDB (undo log) | PostgreSQL (heap versions) |
|---|---|---|
| Current version location | Clustered index | Anywhere in heap |
| Old version location | Undo log segments | Same heap, as dead tuples |
| Rollback cost | Apply undo records | Mark xmin as aborted in CLOG |
| Cleanup mechanism | Purge thread | VACUUM |
| Long transaction impact | Undo log grows | Dead tuples accumulate |

---

## 5. Experiments / Observations

### 5.1 Clustered vs Secondary Index Lookup

```sql
CREATE TABLE orders (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    amount DECIMAL(10,2),
    INDEX idx_user (user_id)
);

-- Clustered index (PK) lookup:
EXPLAIN SELECT * FROM orders WHERE id = 50000;
-- type: const  | rows: 1

-- Secondary index lookup (back-to-table required):
EXPLAIN SELECT * FROM orders WHERE user_id = 500;
-- type: ref | rows: ~100

-- Covering index (no back-to-table):
EXPLAIN SELECT user_id, amount FROM orders WHERE user_id = 500;
-- Extra: Using index
```

### 5.2 Undo Log Growth During Long Transaction

```sql
-- Terminal 1: hold a long transaction open
START TRANSACTION;
SELECT * FROM orders WHERE id = 1;

-- Terminal 2: run 10,000 updates
UPDATE orders SET amount = amount * 1.01;

-- Terminal 3: observe undo log growth
SELECT NAME, SUBSYSTEM, COMMENT
FROM information_schema.INNODB_METRICS
WHERE NAME LIKE '%undo%';
-- undo_log_pages grows as T1 prevents purge
```

### 5.3 EXPLAIN — Hash Join vs Nested Loop (MySQL 8.0+)

```sql
EXPLAIN FORMAT=JSON
SELECT u.name, SUM(o.amount)
FROM orders o JOIN users u ON o.user_id = u.id
GROUP BY u.name;
-- MySQL 8.0.18+ may choose hash join over nested-loop
-- when join column has no useful index
```

---

## 6. Key Learnings

1. **The clustered index is InnoDB's most impactful architectural choice.** Auto-increment PKs enable sequential I/O; UUID PKs cause random page thrashing.

2. **InnoDB and PostgreSQL have opposite MVCC strategies but identical semantics.** PostgreSQL keeps old versions in the heap; InnoDB keeps them in a separate undo log. Neither is universally better.

3. **The purge thread is InnoDB's equivalent of VACUUM.** Both are blocked by long-running transactions. The failure mode differs (undo log growth vs heap bloat) but the root cause is identical.

4. **Gap locks are the hidden cost of REPEATABLE READ.** Switching to READ COMMITTED eliminates gap locks but allows phantom reads — a genuine isolation-vs-concurrency trade-off.

5. **Covering indexes are the most underused InnoDB optimization.** Avoiding the bookmark lookup can reduce query time by an order of magnitude for read-heavy workloads.
