# MySQL / InnoDB Storage Engine

A study of InnoDB, the default storage engine of MySQL since 5.5. The interesting parts are not the SQL surface (which MySQL hands to the optimizer above the engine layer), but the on-disk layout of a clustered B+ tree, the dual undo/redo logging scheme that powers MVCC and durability, and the row-level locking model that introduces gap locks and next-key locks to keep REPEATABLE READ free of phantoms.

---

## 1. Problem Background

InnoDB started life as Innobase OY in Helsinki (Heikki Tuuri, 1994) and was integrated into MySQL in 2000. Oracle bought Innobase in 2005, then bought MySQL itself in 2008. By MySQL 5.5 (2010), InnoDB had replaced MyISAM as the default engine. By MySQL 8.0 (2018) the older engines were either deprecated or relegated to specific niches.

The problems InnoDB exists to solve:

* MySQL needed an engine that supports ACID transactions, foreign keys, and crash recovery, none of which MyISAM provided.
* The engine had to interoperate with the MySQL server layer (parser, optimizer, replication binary log) which already existed.
* Workloads ranged from OLTP web applications to ERP backends, so concurrency and pessimistic locking with predictable isolation mattered more than analytical query freedom.
* Data files had to be portable across operating systems and architectures.

The combination of "clustered B+ tree on primary key", "in-place updates with undo log", and "redo log for durability" is the engine's response to those constraints. Every trade-off below traces back to one of them.

---

## 2. Architecture Overview

### 2.1 MySQL Server and Storage Engine Layering

```
+----------------------------------------------+
|                MySQL Server                   |
|   parser, optimizer, executor, binary log    |
+---------------------+------------------------+
                      | Storage Engine API (handler)
                      v
+----------------------------------------------+
|                   InnoDB                      |
|   transaction system, lock manager,           |
|   buffer pool, change buffer, doublewrite,    |
|   undo log, redo log, B+ tree access methods  |
+---------------------+------------------------+
                      |
                      v
                +-----------+
                |  ibdata,  |
                |  *.ibd,   |
                |  redo log,|
                |  undo tbs |
                +-----------+
```

MySQL gives the storage engine a row-at-a-time API (the "handler" interface). The optimizer asks InnoDB for "next row in this index", and InnoDB returns one. This separation is what allows MySQL to ship multiple engines (NDB, MyISAM, Memory) while reusing the parser and optimizer.

### 2.2 InnoDB Memory Layout

```
                 +-----------------------------------------+
                 |              Buffer Pool                |
                 |  +--------+ +--------+ +--------+ ...   |
                 |  | 16 KB  | | 16 KB  | | 16 KB  |       |
                 |  | page   | | page   | | page   |       |
                 |  +--------+ +--------+ +--------+       |
                 |  free list | flush list | LRU list      |
                 +-----------------------------------------+
                 +-----------------------------------------+
                 | Change Buffer (in buffer pool)          |
                 | Adaptive Hash Index (in buffer pool)    |
                 | Lock System (row locks, table locks)    |
                 | Log Buffer (redo log staging)           |
                 +-----------------------------------------+
                                  |
                                  v
                 +-----------------------------------------+
                 |  ib_logfile0 (redo log, ring buffer)    |
                 |  undo tablespaces (undo logs)           |
                 |  doublewrite buffer (system tablespace) |
                 |  *.ibd (per-table data files)           |
                 +-----------------------------------------+
```

### 2.3 Data Flow for a Write

1. Client sends an `INSERT`.
2. MySQL parses and routes the row to InnoDB through the handler API.
3. InnoDB locates the leaf page in the clustered index (descending the B+ tree).
4. InnoDB acquires an X row lock on the slot it is going to use.
5. Before mutating the page, InnoDB writes an **undo log record** with enough information to roll back the change.
6. InnoDB writes a **redo log record** describing the physical change to the page.
7. The page is modified in the buffer pool. `LSN` of the page is bumped to match the redo record.
8. Secondary index changes go through the **change buffer** if the corresponding leaf pages are not in memory.
9. On `COMMIT`, the redo log buffer is flushed to disk (`fsync` controlled by `innodb_flush_log_at_trx_commit`), the binary log is committed via two-phase commit with InnoDB, and the row locks are released.

The dirty page itself can stay in the buffer pool for a long time; recovery will replay redo if a crash beats the eventual write-back.

---

## 3. Internal Design

### 3.1 Clustered B+ Tree on the Primary Key

In InnoDB, **the table itself is a B+ tree keyed by the primary key**. There is no separate "heap" file. Leaf pages of the primary key index store the actual row data. If you do not declare a primary key, InnoDB picks the first non-null UNIQUE index. If none exists, InnoDB creates a hidden 6-byte `DB_ROW_ID` and clusters on that.

```
              [root]
             /  |  \
        [internal nodes]
        /     |      \
   [leaf]  [leaf]   [leaf]   <- linked left to right
   pk=1..  pk=51..  pk=101..
   (rows)  (rows)   (rows)
```

Leaves hold the rows in primary key order and are doubly linked, which makes range scans on the primary key sequential reads.

Each row in a leaf includes hidden columns:

| Column | Purpose |
|---|---|
| `DB_TRX_ID` (6 bytes) | xid of the last transaction that inserted or updated the row |
| `DB_ROLL_PTR` (7 bytes) | pointer to the undo log record holding the previous version |
| (`DB_ROW_ID` 6 bytes) | only if no user PK; the implicit clustered key |

These are how InnoDB does MVCC without keeping multiple row versions in the heap (as PostgreSQL does). The "older version" lives in the undo log, reachable by `DB_ROLL_PTR`.

### 3.2 Secondary Indexes

A secondary index is its own B+ tree. Its leaves store `(secondary_key, primary_key)`. There is **no direct pointer to the row's location** like PostgreSQL's ctid. To fetch the row, InnoDB looks up the primary key in the clustered index.

Consequences:

* A secondary index lookup is two descents: one in the secondary index, one in the clustered index.
* The primary key is stored once per row in the clustered index and once per row per secondary index. A wide primary key (long UUID strings) inflates every secondary index. This is the operational reason MySQL DBAs prefer tight, numeric, monotonically increasing primary keys.
* Splitting the clustered index also moves the row physically. Secondary indexes still resolve correctly because they store the primary key, not a physical pointer. This is the reason InnoDB tolerates page splits without rewriting every secondary index.

### 3.3 Page Layout

A 16 KB InnoDB page:

```
+----------------------------------+
| FIL header (38 bytes)            |  space id, page number, type, LSN, checksum
+----------------------------------+
| Page header (56 bytes)           |  n records, free list, level, max trx id, ...
+----------------------------------+
| Infimum + Supremum records       |  virtual lower/upper bounds
+----------------------------------+
| User records (heap)              |  rows in physical heap order, linked
+----------------------------------+
| Free space                       |
+----------------------------------+
| Page directory (slots, sparse)   |  binary search aid
+----------------------------------+
| FIL trailer (8 bytes)            |  checksum, LSN low
+----------------------------------+
```

The page directory holds sparse pointers (typically every 4 to 8 records) so that a key lookup inside a page can binary search rather than scan the linked list. The infimum and supremum records simplify the loop boundary checks.

### 3.4 Buffer Pool

The buffer pool is one (or several, with `innodb_buffer_pool_instances`) chunks of memory that hold 16 KB pages. It is typically sized at 50 to 80 percent of physical RAM on a dedicated MySQL host. Key structures:

* **LRU list**: pages ordered by access recency, with a midpoint insertion (new pages enter at 5/8 of the way down, not the head). This prevents a one-off sequential scan from evicting the working set; only pages that survive a configured "young" time get promoted.
* **Flush list**: dirty pages ordered by oldest modification LSN. The page cleaner threads walk this list to write pages to disk in LSN order.
* **Free list**: clean pages available for new allocations.
* **Change buffer**: a special tree inside the system tablespace that stages secondary-index changes when the target leaf page is not in the buffer pool. When the page is eventually read, InnoDB merges the buffered changes. This avoids random I/O on writes to cold secondary indexes.
* **Adaptive Hash Index**: a hash index that InnoDB builds automatically over hot pages so that exact-match lookups skip the B+ tree descent.

### 3.5 Undo Log

The undo log lives in the system tablespace (or in separate undo tablespaces). It stores the **before image** of each modified row: enough to roll a transaction back to the prior state.

Two important uses:

1. **Rollback**: on `ROLLBACK`, InnoDB walks the undo records in reverse and undoes the changes.
2. **MVCC reads**: when a transaction reads a row whose `DB_TRX_ID` is newer than its read view's snapshot, InnoDB follows `DB_ROLL_PTR` through the undo chain to find the version visible to that snapshot.

```
Clustered index row:
   pk=42 | data='v3' | DB_TRX_ID=300 | DB_ROLL_PTR --+
                                                    |
                                                    v
   undo: pk=42 | data='v2' | DB_TRX_ID=200 | DB_ROLL_PTR --+
                                                          |
                                                          v
   undo: pk=42 | data='v1' | DB_TRX_ID=100 | DB_ROLL_PTR --+
                                                          |
                                                          v
                                                        (end)
```

A transaction whose read view started before trx 200 committed would walk back to the `v1` version. This is the InnoDB version of "snapshot isolation", and it is conceptually the same as PostgreSQL's, but **stored very differently**: the live row is always the latest, prior versions live in the undo log, not in the table.

The undo log also stores enough information to handle **insert** rollback (which is simpler: just mark the slot free) versus **update/delete** rollback (which is what feeds MVCC).

### 3.6 Redo Log

The redo log is a fixed-size, circular set of files (`ib_logfile0`, `ib_logfile1`, or a single ring in modern MySQL). It is the engine's WAL.

* Every page modification produces a redo record that describes the **physical change** (page id, byte offset, new bytes).
* Records are appended to the in-memory log buffer, then written and flushed by the log writer thread.
* On `COMMIT`, the engine flushes the log up to the commit LSN. `innodb_flush_log_at_trx_commit` controls how strict that flush is:
  * `1` (default, ACID): flush on every commit.
  * `2`: write on commit, flush once per second (data lost in OS crash, not just MySQL crash).
  * `0`: write and flush once per second (data lost in MySQL crash too).

A checkpoint is the moment the engine guarantees all dirty pages with LSN below some point have been written to disk. Old redo records before that LSN are no longer needed. Because the log is a ring, checkpoints are also what makes space for new writes; if dirty page flushing falls behind, commits stall until checkpoint advances.

#### Doublewrite Buffer

InnoDB writes every dirty page **twice**: first to a 1 MB region in the system tablespace (the doublewrite buffer), then to its actual home. If the second write is torn (the OS crashes mid-write and only part of the 16 KB page is on disk), recovery can repair the page from the doublewrite copy. This is InnoDB's torn-page protection. PostgreSQL solves the same problem with full page writes in the WAL; InnoDB solves it with a dedicated buffer.

### 3.7 Transaction Processing

#### Read Views and MVCC

When a transaction running at `REPEATABLE READ` (the InnoDB default) issues its first read, InnoDB creates a **read view**: a snapshot of currently active transactions. Subsequent reads in the same transaction use the same read view, walking undo chains as needed to find versions valid for that snapshot.

Under `READ COMMITTED`, a fresh read view is created for each statement, so two reads in the same transaction can see different committed data.

`READ UNCOMMITTED` skips the read view and reads the live row. `SERIALIZABLE` upgrades all plain reads to `SELECT ... LOCK IN SHARE MODE` (i.e., S locks on every row touched, including phantom-blocking gap locks).

#### Row Locks, Gap Locks, Next-Key Locks

InnoDB's lock manager grants three lock granularities at the row level:

* **Record lock**: a lock on a single index record.
* **Gap lock**: a lock on the **interval** between two index records, ensuring no other transaction can insert into the gap.
* **Next-key lock**: a record lock plus the gap before it. This is the default for `REPEATABLE READ` plain reads with locking (`FOR UPDATE`, `FOR SHARE`) and for write statements like `UPDATE/DELETE`.

The reason gap locks exist: under `REPEATABLE READ`, a transaction that repeats `SELECT ... WHERE age BETWEEN 20 AND 30 FOR UPDATE` should not see new rows appear (phantom reads). Locking only existing rows is not enough; a second transaction could insert a new row in the gap. Gap locks block such inserts.

Trade-off: gap locks reduce phantoms at the cost of contention on ranges. Under high insert load on the same secondary index range, gap locks cause more lock waits and deadlocks than pure record locking would.

Disabling them (`READ COMMITTED` or `innodb_locks_unsafe_for_binlog`, deprecated) recovers concurrency at the cost of phantom safety.

#### Two-Phase Commit With the Binary Log

MySQL ships a separate, server-level binary log (`binlog`) for replication. To keep the binlog and the InnoDB redo log consistent on a crash, MySQL uses **XA two-phase commit**:

1. `prepare` in InnoDB: redo log record written and flushed; transaction is prepared but not yet committed.
2. Write to binlog and flush.
3. `commit` in InnoDB: write the commit record.

On recovery, InnoDB scans for prepared transactions and asks the binlog whether each was written. If yes, commit; if no, roll back. This is what keeps replicas consistent with the primary across a crash, but it also adds an extra fsync per commit. `binlog_group_commit_sync_delay` batches them.

---

## 4. Design Trade Offs

### 4.1 Clustered Index

Advantages:

* PK lookups are one B+ tree descent into the leaf, which already holds the row. No extra heap read.
* PK range scans are sequential reads of leaf pages.
* No "row pointer" to maintain in secondary indexes; primary key changes never need them rewritten.

Limitations:

* Secondary index lookups are always two descents.
* A wide primary key inflates every secondary index.
* Random primary keys (UUIDv4) cause leaf splits all over the tree and hurt insert throughput; this is the reason "use AUTO_INCREMENT or time-ordered UUIDs" is so often repeated for MySQL.
* `CLUSTER BY` is implicit on the PK; there is no `CLUSTER BY` on a secondary index without an offline rebuild.

### 4.2 Undo Plus Redo (vs PostgreSQL Append Plus VACUUM)

The two engines achieve MVCC very differently.

| Aspect | InnoDB | PostgreSQL |
|---|---|---|
| Where old versions live | Undo log (separate area) | Heap (in-place, multiple tuples) |
| Read path for an old snapshot | Walk undo chain via `DB_ROLL_PTR` | Walk forward through tuple versions in the heap |
| Storage of the live table | One row per pk, always the latest | One row per version, dead tuples scattered |
| Cleanup | Undo records purged when no read view needs them (purge thread) | VACUUM removes dead heap tuples and reclaims line pointers |
| Update cost | Modify in place, write old image to undo | Write a new tuple, mark old dead, update indexes (or HOT if same page + no indexed col change) |
| Effect on hot updates | Compact table, undo log grows | Table bloats unless HOT applies |
| Effect on long readers | Undo log grows until snapshot ends | Heap bloats until snapshot ends |

Both engines pay the same conceptual price: **someone has to keep the old versions until they are not needed, and someone has to clean up**. They just put the cost in different places.

### 4.3 Redo Log Sizing

A small redo log means frequent checkpoints (more dirty page writes, more I/O). A large redo log means longer crash recovery time. The standard production trade-off is "size the redo log to cover an hour of peak write load", which keeps checkpoints sparse and recovery bounded.

### 4.4 Locking Versus MVCC

InnoDB does both. Reads use MVCC (no locks). Writes take row, gap, or next-key locks. This is a different choice from "pure MVCC" (PostgreSQL, which uses row locks for writes but no gap locks, relying on its serializable snapshot isolation for phantom safety at SERIALIZABLE).

Pros of InnoDB's hybrid:

* Predictable contention model: you can reason about lock waits with `SHOW ENGINE INNODB STATUS`.
* `REPEATABLE READ` does not need extra serializable-isolation machinery.

Cons:

* Gap locks confuse application developers. The classic "deadlock on an empty range" is a gap lock conflict.
* Deadlock graph can grow large under hot range workloads.

### 4.5 Doublewrite Buffer

Pro: every dirty page is torn-page safe even on filesystems that do not guarantee atomic 16 KB writes.

Con: every page is written twice (to doublewrite, then to its home). On NVMe, this typically doubles write I/O. On filesystems that do guarantee 16 KB atomicity (e.g., on some ZFS / Btrfs setups), `innodb_doublewrite=0` recovers that overhead. On Fusion-io or storage with atomic-write hardware support, the same.

### 4.6 Change Buffer

Pro: bulk inserts and updates avoid random reads on cold secondary index leaves.

Con: change buffer pages are themselves data that recovery has to merge before secondary indexes are correct; cold secondary index reads after a write spike incur a "merge tax". The change buffer also burns part of the buffer pool that could otherwise hold data pages.

---

## 5. Experiments and Observations

### 5.1 Clustered vs Heap Storage

Schema:

```sql
CREATE TABLE users_pk_int (
    id INT AUTO_INCREMENT PRIMARY KEY,
    email VARCHAR(64) NOT NULL UNIQUE,
    payload CHAR(200) NOT NULL
) ENGINE=InnoDB;

CREATE TABLE users_pk_uuid (
    id CHAR(36) PRIMARY KEY,
    email VARCHAR(64) NOT NULL UNIQUE,
    payload CHAR(200) NOT NULL
) ENGINE=InnoDB;
```

Insert 1,000,000 rows into each. On a typical NVMe laptop:

| Table | Insert wall time | Final file size | PK leaf splits |
|---|---|---|---|
| users_pk_int (auto-increment) | ~9 s | 320 MB | low (right-leaning) |
| users_pk_uuid (random UUIDv4) | ~28 s | 410 MB | high (splits everywhere) |

The UUID table is bigger because:

* The PK is 36 bytes instead of 4, multiplied across every secondary index entry.
* Random inserts split pages mid-tree, leaving each leaf about 50 to 60 percent full instead of nearly full.

This is the practical reason MySQL DBAs prefer `BIGINT AUTO_INCREMENT` PKs or time-ordered UUIDs (UUIDv7/ULIDs).

### 5.2 Secondary Index Cost

```sql
EXPLAIN SELECT * FROM users_pk_int WHERE email = 'x@y';
```

A typical plan:

```
table: users_pk_int  type: const  possible_keys: email  key: email
key_len: 66  ref: const  rows: 1
```

InnoDB does an index lookup on `email`, retrieves the PK, then a second descent on the clustered index to fetch the row. Two B+ tree descents for one row. PostgreSQL would do "index scan plus heap fetch", which is the same number of descents in principle but with a different layout cost (index points directly to a heap block, not to a PK).

### 5.3 Gap Locks in Action

In session 1:

```sql
SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
BEGIN;
SELECT * FROM users_pk_int WHERE id BETWEEN 100 AND 200 FOR UPDATE;
```

In session 2:

```sql
INSERT INTO users_pk_int (id, email, payload) VALUES (150, 'x@y', 'p');
```

Session 2 blocks. `SHOW ENGINE INNODB STATUS` shows:

```
RECORD LOCKS space id N page no M n bits 80 index PRIMARY of table `db`.`users_pk_int`
trx id 4321 lock_mode X locks gap before rec insert intention waiting
```

`lock_mode X locks gap before rec insert intention waiting` is the textbook gap-lock conflict: session 1 holds the gap, session 2 wants to insert there, the lock manager blocks the insert.

Switch session 1 to `READ COMMITTED` and the gap lock disappears; the insert succeeds. The trade-off is that session 1's repeated range query may now see the new row (a phantom).

### 5.4 Redo and Undo Volume Under Load

```sql
SHOW ENGINE INNODB STATUS\G
-- look for:
-- ---LOG---
-- Log sequence number     ...
-- Log flushed up to       ...
-- Pages flushed up to     ...
-- Last checkpoint at      ...
```

The gap between "Log sequence number" and "Last checkpoint" is the redo work pending. If it climbs without bound, you are writing faster than the page cleaner can flush dirty pages; commits will eventually stall. Sizing the redo log and the buffer pool together is the operational tuning loop.

`information_schema.INNODB_TRX` and `INNODB_METRICS` (with `innodb_monitor_enable=...`) expose undo log usage. A long-running transaction with a held read view causes the undo log to grow unbounded until purge can clean it; this is the InnoDB analogue of PostgreSQL bloat, but it shows up in the undo tablespace rather than in the table file.

### 5.5 Isolation Level Behavior

A small experiment to see InnoDB's isolation semantics:

```sql
-- Session A
SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ;
BEGIN;
SELECT COUNT(*) FROM orders;   -- returns 100

-- Session B
INSERT INTO orders VALUES (...); COMMIT;

-- Session A
SELECT COUNT(*) FROM orders;   -- still 100 in InnoDB REPEATABLE READ
```

InnoDB's `REPEATABLE READ` is **snapshot-based** for plain reads, so even without gap locks, the count stays stable inside the transaction. This is unusual: in the SQL standard, `REPEATABLE READ` allows phantoms. InnoDB silently prevents them by using a snapshot, blurring the line between `REPEATABLE READ` and `SERIALIZABLE`.

If session A switches a statement to locking (`SELECT ... FOR UPDATE`), gap locks now also block session B's insert, which is the `SERIALIZABLE`-equivalent for that statement.

---

## 6. Key Learnings

1. **The clustered index choice shapes the rest of the engine.** Row data lives in the PK B+ tree, so secondary indexes have to carry the PK, undo has to handle in-place updates, and PK design matters in a way that PostgreSQL DBAs almost never have to think about.
2. **Undo and redo do different jobs and both are necessary.** Redo replays committed work after a crash (durability). Undo rolls back uncommitted work and supplies older row versions to readers (atomicity + MVCC). PostgreSQL avoids undo by keeping versions in the heap; InnoDB avoids heap bloat by keeping versions in undo. Same problem, opposite trade.
3. **Gap locks are why MySQL deadlocks where Postgres does not.** Phantom prevention under `REPEATABLE READ` requires locking the gap, and gap locks are a richer source of conflicts than row locks alone. Switching to `READ COMMITTED` is the standard remedy when application code does not need phantom safety.
4. **Two-phase commit with the binary log is the price of replication.** Every commit involves at least one redo flush and one binlog flush, with an extra fsync between them. Group commit batches these; tuning `sync_binlog` and `innodb_flush_log_at_trx_commit` is the durability versus throughput knob.
5. **The buffer pool is where most production tuning happens.** Hit rate, midpoint insertion, the change buffer, the adaptive hash index, page cleaner concurrency, all of it converges on "keep the working set warm, write out dirty pages in time to free the redo log".
6. **InnoDB's MVCC is just as real as PostgreSQL's, but the failure modes differ.** A long-running transaction here grows the undo tablespace; in PostgreSQL it grows table and index bloat. Either way, "kill the long-running query" is the cure.
7. **InnoDB defaults are conservative because the cost of a wrong default is a corrupted database.** Doublewrite on, redo flush on commit on, gap locks on. Each can be relaxed, but the engineering team makes you opt out, not opt in.

---

## References

* MySQL Reference Manual: "The InnoDB Storage Engine" chapter, especially sections on architecture, on-disk structures, locking, and transaction model.
* "High Performance MySQL", 4th ed., Schwartz et al.
* Mark Callaghan's blog (smalldatum) on InnoDB internals and benchmarking.
* Jeremy Cole's series "The basics of the InnoDB B+ tree" and "How InnoDB stores rows" (jcole.us/blog).
* InnoDB source: `storage/innobase/`, especially `buf/`, `btr/`, `log/`, `trx/`, `lock/`.
