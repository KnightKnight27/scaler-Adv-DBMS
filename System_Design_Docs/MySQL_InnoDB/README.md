# MySQL / InnoDB Storage Engine

**Name:** Harshita Hirawat  
**Roll number:** 24BCS10044

## 1. Problem Background

InnoDB is MySQL's transactional storage engine. It exists to provide durable
ACID transactions, row-level concurrency, crash recovery, and efficient indexed
access while still fitting MySQL's pluggable storage-engine interface.

Its central decision is to store table rows in the leaf pages of the primary-key
B-tree. This clustered organization is very effective for primary-key lookups
and ranges. It also shapes every secondary index, update, and page split.

InnoDB and PostgreSQL both implement MVCC, but they keep old row state in
different places. InnoDB generally keeps the current record in the clustered
index and reconstructs older versions through undo records. PostgreSQL writes a
new heap tuple version and later removes dead versions with VACUUM.

## 2. Architecture Overview

```text
MySQL client
    |
MySQL SQL layer: parser, optimizer, executor
    |
InnoDB storage engine
    +-- buffer pool
    +-- clustered primary-key B-tree
    +-- secondary B-trees -> primary key
    +-- lock manager
    +-- undo logs -> rollback and old versions
    +-- redo log  -> crash recovery
    |
tablespaces / data files / redo and undo storage
```

### Write path

```text
UPDATE row
  -> acquire record/next-key locks
  -> write old information to undo
  -> change page in buffer pool
  -> append redo describing page changes
  -> commit makes required redo durable
  -> dirty data page can be flushed later
```

Undo answers “how do I go backward logically?” Redo answers “how do I repeat
completed physical changes after a crash?” One cannot replace the other.

## 3. Internal Design

### 3.1 Clustered and secondary indexes

The primary-key index is clustered: its leaf record contains the row's columns.
Therefore, `WHERE primary_key = ?` can reach the row in one B-tree search, and a
primary-key range reads nearby leaf records.

A secondary-index leaf stores the secondary key plus the row's primary key, not
a physical heap address. A secondary lookup normally follows two trees:

```text
secondary key -> primary key -> clustered leaf record
```

This avoids storing unstable physical addresses, but wide primary keys make
every secondary index larger. Tables without a suitable primary key require an
engine-generated clustered key. The behavior is documented under
[InnoDB clustered and secondary indexes](https://dev.mysql.com/doc/refman/8.0/en/innodb-index-types.html).

### 3.2 Buffer pool

The buffer pool caches data and index pages, tracks dirty pages, and uses LRU-like
lists with special handling to reduce pollution from large scans. A buffer-pool
hit avoids a disk read; a miss requires a victim and page load. Dirty pages can
be flushed in the background because redo protects committed changes first.

Unlike PostgreSQL's heap-plus-separate-index model, an InnoDB table's clustered
index is both data storage and an index. Caching its upper B-tree levels makes
primary-key navigation inexpensive. See the official
[buffer-pool documentation](https://dev.mysql.com/doc/refman/8.0/en/innodb-buffer-pool.html).

### 3.3 Undo logs and MVCC

Before modifying a record, InnoDB stores enough information in undo to reverse
the change. Undo serves two related purposes:

- roll back an active or aborted transaction,
- reconstruct an older row version for a consistent read.

Read views decide which transaction versions are visible. Long-running read
views keep old undo history necessary, so purge cannot immediately remove it.
This is InnoDB's version-retention pressure, similar in effect to how an old
PostgreSQL snapshot can prevent dead-tuple cleanup. See
[undo logs](https://dev.mysql.com/doc/refman/8.0/en/innodb-undo-logs.html) and
[multi-versioning](https://dev.mysql.com/doc/refman/8.0/en/innodb-multi-versioning.html).

### 3.4 Redo log and recovery

Redo records changes to pages so committed work can be replayed after a crash.
At commit, the durability policy determines when redo is flushed. Data pages do
not need to be written at every commit, which converts many random page writes
into sequential log activity plus later background flushing.

Redo improves throughput and recovery, but log capacity and checkpoint pressure
still matter. If dirty pages cannot be flushed fast enough, the engine may have
to slow writers. See the [InnoDB redo log](https://dev.mysql.com/doc/refman/8.0/en/innodb-redo-log.html).

### 3.5 Locks and isolation

InnoDB supports shared/exclusive record locks, intention locks at table level,
gap locks, and next-key locks (record plus preceding gap). Gap-related locks stop
another transaction from inserting a row into a protected key range, which is
important for preventing phantoms under `REPEATABLE READ`.

This protection has a cost: two transactions touching overlapping ranges can
block or deadlock even when no existing row is identical. Consistent nonlocking
reads use MVCC; locking reads and writes participate in the lock manager. The
details are in [InnoDB locking](https://dev.mysql.com/doc/refman/8.0/en/innodb-locking.html).

Isolation level changes which versions and locks a transaction uses:

| Isolation level | InnoDB behavior and trade-off |
|---|---|
| `READ UNCOMMITTED` | May expose uncommitted versions; weakest isolation and rarely appropriate for transactional application logic |
| `READ COMMITTED` | Each consistent read receives a fresh read view; nonmatching record locks are generally released earlier and gap locking is reduced, improving concurrency but permitting nonrepeatable reads |
| `REPEATABLE READ` | Default InnoDB level; consistent reads reuse a transaction read view, while locking range operations use next-key locks to prevent phantoms |
| `SERIALIZABLE` | Converts ordinary reads into locking reads when autocommit is disabled, providing the strongest isolation at the cost of more blocking and deadlocks |

MVCC alone answers which committed version a consistent read may see. Locks are
still required for writes and explicitly locking reads because those operations
must protect future changes, not only reconstruct a past snapshot.

## 4. Design Trade-Offs

### InnoDB choices

| Choice | Advantage | Limitation |
|---|---|---|
| Cluster rows by primary key | Fast primary-key lookup/range; good locality | Random primary keys cause splits; secondary indexes carry the PK |
| Secondary leaf stores primary key | Stable logical reference | Often requires a second tree lookup |
| Undo-based old versions | Current clustered record stays central | Long snapshots retain undo and delay purge |
| Redo before page flush | Durable commits without synchronous data-page flush | Redo/checkpoint pressure must be managed |
| Next-key/gap locks | Prevent phantoms for locking statements | More blocking and deadlock possibilities |

### Compared with PostgreSQL

| Area | InnoDB | PostgreSQL |
|---|---|---|
| Base table | Clustered primary-key B-tree | Unordered heap relation |
| Update history | Current record plus undo chain | Multiple heap tuple versions |
| Cleanup | Purge old undo/history | VACUUM dead tuples and indexes |
| Secondary reference | Primary-key value | Heap tuple identifier |
| Phantom protection | Range/gap/next-key locking | Serializable Snapshot Isolation detects dangerous dependencies |

PostgreSQL's heap permits several independent indexes without choosing one as
the physical row order. InnoDB gains primary-key locality but makes primary-key
design part of physical storage design. Neither decision is universally better.

## 5. Experiments / Observations

### Setup

An isolated MySQL 8.0.42 instance was initialized locally with InnoDB. A table
of 100,000 orders was created:

```sql
CREATE TABLE orders(
  id INT PRIMARY KEY,
  customer_id INT NOT NULL,
  amount DECIMAL(10,2),
  note VARCHAR(40)
) ENGINE=InnoDB;
```

The point query was measured with `EXPLAIN ANALYZE` before and after adding a
secondary index:

```sql
SELECT SUM(amount) FROM orders WHERE customer_id=7777;
CREATE INDEX idx_orders_customer ON orders(customer_id);
```

### Index result

| Observation | Before index | After index |
|---|---:|---:|
| Access method | Table scan | Index lookup on `idx_orders_customer` |
| Rows examined/returned | 100,000 scanned / 10 matched | 10 index matches |
| Actual time | 23.6 ms | 0.054 ms |

The primary-key query `WHERE id=77777` was resolved as a constant row fetched
before normal execution, illustrating the direct clustered-key path.

`SHOW INDEX` reported a `PRIMARY` B-tree and the new secondary B-tree. InnoDB's
statistics estimated 78,016 table rows even though the exact count was 100,000,
and estimated secondary cardinality at 9,655 instead of 10,000. This is a useful
reminder that optimizer metadata is sampled/approximate. Observed storage was:

| Data length | Secondary index length |
|---:|---:|
| 23,642,112 bytes | 1,589,248 bytes |

### Undo and redo result

A transaction updated the first 10,000 rows and was rolled back:

```sql
START TRANSACTION;
UPDATE orders SET amount=amount+1 WHERE id<=10000;
ROLLBACK;
```

The redo counter was sampled immediately before and after the transaction with:

```sql
SHOW GLOBAL STATUS LIKE 'Innodb_os_log_written';
```

This is a cumulative server counter, so the isolated instance avoided unrelated
application traffic between the two samples.

| Measurement | Value |
|---|---:|
| Sum before transaction | 2,499,500.00 |
| Sum inside transaction | 2,509,500.00 |
| Sum after rollback | 2,499,500.00 |
| Redo bytes before | 11,021,824 |
| Redo bytes after | 12,605,440 |

Undo restored the logical values exactly. Meanwhile, redo output increased by
1,583,616 bytes because the engine still had to log page and undo-related
changes made during the transaction and its rollback. This observation explains
why both logs exist: rollback correctness and crash durability are separate jobs.

Times are local warm-cache measurements, not universal performance claims.

## 6. Key Learnings

- InnoDB's primary key is also the table's physical organization.
- Secondary-index design cannot be separated from primary-key width.
- Undo supports rollback and snapshot reads; redo supports crash recovery.
- MVCC reduces read/write blocking but retains history until old read views end.
- Gap and next-key locks trade concurrency for phantom protection.
- The optimizer's row/cardinality values are estimates; `EXPLAIN ANALYZE` makes
  the difference between estimated and actual behavior visible.

## Sources Consulted

- [Clustered and secondary indexes](https://dev.mysql.com/doc/refman/8.0/en/innodb-index-types.html)
- [InnoDB buffer pool](https://dev.mysql.com/doc/refman/8.0/en/innodb-buffer-pool.html)
- [Undo logs](https://dev.mysql.com/doc/refman/8.0/en/innodb-undo-logs.html)
- [Redo log](https://dev.mysql.com/doc/refman/8.0/en/innodb-redo-log.html)
- [InnoDB locking](https://dev.mysql.com/doc/refman/8.0/en/innodb-locking.html)
- [InnoDB multi-versioning](https://dev.mysql.com/doc/refman/8.0/en/innodb-multi-versioning.html)
