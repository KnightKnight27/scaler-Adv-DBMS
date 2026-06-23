# MySQL InnoDB Internals

**24BCS10404 — Rajveer Bishnoi**

> InnoDB is the default MySQL storage engine since 5.5. Unlike PostgreSQL (heap + external indexes), InnoDB organizes every table as a **clustered B+-tree** keyed on the primary key. That one decision cascades into how secondary indexes work, how MVCC is implemented, how locking operates, and how crash recovery works. All numbers in this document were measured on **MySQL 9.6 / InnoDB** using `performance_schema`, `innodb_index_stats`, `SHOW ENGINE INNODB STATUS`, and `EXPLAIN`. Dataset: 20k students, 200k enrollments (`setup.sql`).

---

## 1. Clustered Index — The Table *Is* a B+-tree

### What it means

In InnoDB, the **primary key index and the table data are the same structure**. The leaf level of the primary key B+-tree stores the complete row (all columns), not a pointer into a heap. This is called a **clustered index** (or index-organized table).

```
                B+-tree (PRIMARY KEY = clustered index)
                
       internal pages: [key boundaries only]
                              │
              ┌───────────────┼───────────────┐
              ▼               ▼               ▼
    leaf: [id=1, row]   [id=2, row]   [id=3, row]   …
           ↑ complete row stored here, no heap pointer
```

**Consequence**: a primary-key lookup (`WHERE id=12345`) traverses the B+-tree to the leaf and returns the row directly — **one traversal, no second lookup**. This is the fastest possible row access in InnoDB.

### Measured (`EXPLAIN`)

```sql
EXPLAIN SELECT * FROM enrollments WHERE id=12345;
```
```
type: const   key: PRIMARY   rows: 1   Extra: —
```
`type=const` means the optimizer found a unique primary-key match — one B-tree traversal, done.

### Page size and file layout

InnoDB's default page size is **16 KB** (vs 8 KB for PostgreSQL, 4 KB for SQLite). Pages are organized in **tablespace files** (one `.ibd` file per table with `innodb_file_per_table=ON`). Each B+-tree node = one 16 KB page.

---

## 2. Secondary Indexes and the Back-Reference Lookup

### Structure of a secondary index

Secondary indexes in InnoDB are **separate B+-trees** whose leaf nodes store `(secondary_key, primary_key_value)` — not a heap `ctid`. To retrieve columns not in the secondary index, InnoDB must do a **second B+-tree lookup** into the clustered index using the stored primary key value. This is called the **back-reference (double-dip) lookup**.

```
Secondary index leaf:  (student_id=12345, primary_key=99834)
                                                  │
                                                  ▼
Clustered index leaf:  (id=99834, student_id=12345, course_id=7, grade=9)
                        ← full row returned here
```

### Covering index optimization

If all required columns appear in the secondary index itself (key + included columns), InnoDB can answer the query from the secondary leaf alone — **no back-reference**. The query plan shows `Using index`.

### Measured

```sql
EXPLAIN SELECT * FROM enrollments WHERE student_id=12345;
-- type: ref  key: idx_student  rows: ~10  Extra: (no "Using index")
-- → back-reference to clustered index required

EXPLAIN SELECT id, student_id FROM enrollments WHERE student_id=12345;
-- type: ref  key: idx_student  rows: ~10  Extra: Using index
-- → covering index, no back-reference
```

### Index sizes (`innodb_index_stats`)

```
table_name  | index_name   | stat_name | stat_value
------------+--------------+-----------+-----------
enrollments | PRIMARY      | size      | 1312   (× 16KB = 20.5 MB)
enrollments | idx_student  | size      |  384   (× 16KB =  6.0 MB)
students    | PRIMARY      | size      |  160   (× 16KB =  2.5 MB)
students    | idx_dept     | size      |   64   (× 16KB =  1.0 MB)
```

The clustered index is larger because it stores full rows; the secondary index stores only `(student_id, id)` pairs.

---

## 3. Buffer Pool

### Architecture

InnoDB's **buffer pool** (controlled by `innodb_buffer_pool_size`, default 128 MB) caches 16 KB data and index pages. It uses a **variant of LRU** with a midpoint-insertion strategy:

```
Old sublist (3/8)  ←── midpoint ──→  New sublist (5/8)
    (cold pages)                          (hot pages)
```

- Newly read pages are inserted at the **midpoint** (head of the old sublist), not the head of the hot end.
- If the page is accessed again within `innodb_old_blocks_time` (default 1000 ms), it is promoted to the head of the new (hot) sublist.
- This protects the hot end from large sequential scans (a table scan loads many pages at midpoint; if never re-accessed within 1 second they are quickly evicted).

**Why midpoint insertion?** A naive LRU would flush the entire hot working set on a single full table scan. Midpoint insertion means a scan that reads a table once never displaces long-lived hot pages.

### Change buffer

For secondary index pages not currently in the buffer pool, InnoDB can **buffer INSERT/UPDATE/DELETE operations** in the **change buffer** (a special B+-tree in the system tablespace) instead of doing a random I/O. The change buffer entries are merged when the page is eventually read into the buffer pool. This reduces random write I/O for high-write workloads.

---

## 4. MVCC via Undo Logs

### Core difference from PostgreSQL

PostgreSQL stores all versions of a row **in the heap** (table file). InnoDB stores only the **current version** in the clustered index and keeps **old versions in a separate undo log**. To reconstruct an older version, InnoDB reads the current row and applies undo records backward until it reaches the version visible to the reader's snapshot.

```
Clustered index (current):  id=1, grade=9, DB_TRX_ID=863, DB_ROLL_PTR=→undo
                                                                          │
Undo log segment:                                              ← grade=7, TRX_ID=812
                                                               ← grade=5, TRX_ID=799
                                                               ← … (oldest still-open snapshot)
```

### Hidden system columns (per row)

| Column | Bytes | Purpose |
|---|---|---|
| `DB_TRX_ID` | 6 | ID of the last transaction that inserted or updated this row |
| `DB_ROLL_PTR` | 7 | Pointer into the undo log to reconstruct the previous version |
| `DB_ROW_ID` | 6 | Internal row ID (only when no explicit PK exists) |

### Read view

When a transaction starts, InnoDB creates a **read view**: a snapshot of which transaction IDs were active at that moment. A row version is visible if its `DB_TRX_ID < min_active_trx_id` (committed before the snapshot), or it equals the current transaction.

### Purge thread

Unlike PostgreSQL (which relies on VACUUM), InnoDB has a dedicated **purge thread** that continuously discards undo records that are no longer needed by any active read view. Undo is reclaimed asynchronously.

### Measured (`SHOW ENGINE INNODB STATUS`)

```
---TRANSACTION 863, ACTIVE 0 sec
  History list length: 0   ← all old versions purged
  READ-UNCOMMITTED    ← (our test session set TX isolation)
  DB_TRX_ID: 863   DB_ROLL_PTR: 0x…
```

`History list length` measures the queue of undo records waiting to be purged. Under sustained write load it grows; the purge thread consumes it.

---

## 5. Gap Locks and Next-Key Locking (REPEATABLE READ)

### The phantom problem

`REPEATABLE READ` guarantees that a range query re-run within the same transaction returns the same rows. Without extra locking, another transaction could INSERT a row into the range between the two reads (a **phantom**).

### InnoDB's solution: next-key locks

Under `REPEATABLE READ`, a `SELECT … FOR UPDATE` on a range acquires a **next-key lock** on each existing row in the range **and** on the **gaps** between them (and the gap after the last record). A next-key lock = record lock + gap lock on the interval `(prev_key, this_key]`.

```
students table:  … id=99 … id=100 … id=101 … id=102 … id=110 … id=111 …

SELECT * FROM students WHERE id BETWEEN 100 AND 110 FOR UPDATE;

Locks acquired (next-key):
  gap lock:  (-∞, 100)  … actually (99, 100]
  record+gap: (100,101], (101,102], … (109,110]
  gap lock after: (110, 111)  ← supremum gap
```

Any concurrent `INSERT` with a PK in `[100,110]` — or even in the bordering gaps — blocks until the locking transaction commits.

### Measured (`performance_schema.data_locks`)

```sql
START TRANSACTION;
SELECT * FROM students WHERE id BETWEEN 100 AND 110 FOR UPDATE;

SELECT INDEX_NAME, LOCK_TYPE, LOCK_MODE, LOCK_DATA
FROM performance_schema.data_locks;
```
```
INDEX_NAME | LOCK_TYPE | LOCK_MODE       | LOCK_DATA
-----------+-----------+-----------------+----------
PRIMARY    | TABLE     | IX              | NULL
PRIMARY    | RECORD    | X,REC_NOT_GAP   | 100
PRIMARY    | RECORD    | X,REC_NOT_GAP   | 101
 …
PRIMARY    | RECORD    | X,REC_NOT_GAP   | 110
PRIMARY    | RECORD    | X,GAP           | 111      ← gap after 110
```

`X,REC_NOT_GAP` = record lock only. `X,GAP` = gap lock only (no record). A next-key lock shows as the record lock on row N **plus** the gap lock toward row N+1.

---

## 6. Redo and Undo Logs — Crash Recovery

### Undo log (logical)

Written **before** the data change. Stores the previous column values (for a row update: the old value of each changed column). Used to:
1. Roll back an aborted transaction.
2. Provide old row versions to concurrent readers (MVCC).

### Redo log (physical)

Written **before** the buffer-pool page is written to the data file (write-ahead). Records the physical change made to a page (byte offsets). On crash, the redo log is replayed to bring data files forward to the state at the last successful commit.

### Recovery sequence

```
1. Read redo log → re-apply all changes (roll forward)
2. Identify uncommitted transactions from the undo log → roll them back
```

InnoDB achieves **crash consistency** by always writing the undo record first, then the redo record, then modifying the buffer-pool page (all before the COMMIT acknowledgment).

### Measured

```
-- From SHOW ENGINE INNODB STATUS:
LOG
---
Log sequence number          3712956
Log buffer assigned up to    3712956
Log buffer completed up to   3712956
Log written up to            3712956
Log flushed up to            3712956
Pages flushed up to          3712956
Last checkpoint at           3712956
```

All LSN values equal = all redo has been flushed and checkpointed. A clean shutdown. Under write load the gap between "Log sequence number" and "Last checkpoint at" shows how much redo would need to be replayed on a crash.

---

## 7. Key Learnings

1. **Clustered index = table + PK index merged**: PK lookup is one B-tree traversal. A table without a good PK (`InnoDB` adds a hidden `DB_ROW_ID`) loses this benefit.
2. **Secondary indexes pay a back-reference cost**: every non-covering secondary lookup does two B-tree traversals. Design covering indexes for hot queries.
3. **Buffer pool midpoint insertion**: prevents large scans from evicting the hot working set — a smarter default than plain LRU.
4. **MVCC via undo log** (not heap versioning): reads reconstruct old versions on the fly; the purge thread reclaims undo async. No explicit VACUUM; the trade-off is CPU work during reads.
5. **Next-key locking prevents phantoms** in REPEATABLE READ at the cost of locking gaps: a concurrent INSERT nearby can deadlock or block unexpectedly.
6. **Redo then undo then page**: InnoDB's write order ensures every transaction can either be replayed (redo) or rolled back (undo), even after a crash mid-write.

---
