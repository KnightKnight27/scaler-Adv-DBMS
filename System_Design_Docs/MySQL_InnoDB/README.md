# MySQL / InnoDB Storage Engine

I studied InnoDB, MySQL's default engine. The interesting parts: tables live
inside a clustered B+tree on the primary key, MVCC uses undo logs, durability
uses redo logs, and concurrency uses row locks plus gap locks. I compare it with
PostgreSQL throughout, since they chose opposite paths.

## 1. Problem Background

Early MySQL used MyISAM: fast reads but no transactions and no crash recovery.
InnoDB was made to fix that, adding ACID transactions, crash recovery,
row-level locking, and MVCC. It became the default in MySQL 5.5.

## 2. Architecture Overview

```
clients -> MySQL SQL layer (parser, optimizer)
                 |  storage engine API
              InnoDB
   in memory: Buffer Pool (16 KB pages) + Change Buffer + Log Buffer
        |  flush pages              |  flush log
        v                           v
   Tablespace (.ibd):           Redo log
   clustered B+tree +
   secondary indexes + undo
```

The SQL layer sits on top and calls InnoDB through an engine interface. InnoDB
caches 16 KB pages in the buffer pool; each table is a tablespace file holding
the clustered index, secondary indexes, and undo; durability is the redo log.

## 3. Internal Design

- **Clustered index (the table is an index).** Rows live in the leaves of a
  B+tree ordered by the primary key, so finding the PK *is* finding the row in
  one search. Rows are stored in PK order, so PK range scans read nearby pages.
  No PK? InnoDB makes a hidden rowid.
- **Secondary indexes.** A secondary index leaf stores the PK value, not a
  physical pointer. So a secondary lookup is two searches: index -> PK, then
  clustered index -> row. A big PK therefore bloats every secondary index. (In
  PostgreSQL all indexes point to a physical TID instead.)
- **Buffer pool.** In-memory cache of 16 KB pages, like Postgres
  `shared_buffers`. Uses a young/old LRU so one big scan doesn't evict hot
  pages. A change buffer delays/merges writes to secondary index pages.
- **Undo logs + MVCC (Oracle-style).** UPDATE writes the new value *in place*
  and saves the old version to an undo log. Each row has a txn id and a roll
  pointer; an older reader follows the roll pointer back through undo to see its
  version. Undo also powers rollback. A purge thread removes old undo.
- **Redo log (durability).** InnoDB's write-ahead log: log the change before the
  data page, flush on COMMIT. After a crash it replays redo (committed changes)
  and applies undo (uncommitted ones).
- **Locking.** Row-level S/X locks let many txns write different rows. A *gap
  lock* locks the space between rows to stop inserts (prevents phantoms). A
  *next-key lock* = record + gap, the default under REPEATABLE READ (MySQL's
  default isolation level).

```
clustered row (latest): PK=7 balance=500 ROLL_PTR -+
undo chain: balance=300 -> balance=100 -> ...       (older versions)
```

## 4. Design Trade-Offs

- Clustered index: PK lookups and PK range scans are very fast, but secondary
  lookups need a second search, and a random PK (like a UUID) causes page splits
  - an auto-increment PK is much friendlier.
- Why both logs: redo rolls *forward* (durability after crash), undo rolls
  *backward* (atomic rollback) and also serves MVCC reads. You need both for
  ACID.
- InnoDB vs PostgreSQL MVCC: InnoDB updates in place and keeps old versions in
  undo (table stays compact, purge thread cleans up). PostgreSQL writes a new
  tuple version in the table (simpler, rollback is "free", but bloats and needs
  VACUUM). Two valid answers to the same problem.
- Locking: gap/next-key locks block phantoms even at REPEATABLE READ, but can
  block inserts in a range and cause more contention.

## 5. Experiments / Observations

> No MySQL server was installed here, so these are representative examples
> (illustrative, not run live).

```sql
EXPLAIN SELECT * FROM accounts WHERE id = 42345;      -- key: PRIMARY  (clustered search)
EXPLAIN SELECT * FROM accounts WHERE city = 'city_7'; -- key: NULL     (full scan)
CREATE INDEX idx_city ON accounts(city);
EXPLAIN SELECT * FROM accounts WHERE city = 'city_7'; -- key: idx_city (index + bookmark lookup)
```

Gap lock demo (REPEATABLE READ): session 1 does
`SELECT ... WHERE id BETWEEN 100 AND 200 FOR UPDATE`; then session 2's
`INSERT id=150` blocks until session 1 commits - that is the gap lock stopping a
phantom. Live tools: `SHOW ENGINE INNODB STATUS` and
`performance_schema.data_locks`. This mirrors the real SQLite plan flip
(SCAN -> SEARCH) I measured in the PostgreSQL_vs_SQLite topic.

## 6. Key Learnings

- The table is a B+tree, so PK access is one search - but secondary indexes pay
  a second hop and inherit the PK's size, so keep the PK small and increasing.
- Two logs, two jobs: redo rolls forward (durability), undo rolls backward
  (rollback) and feeds MVCC reads.
- InnoDB does MVCC without bloat by keeping old versions in undo and using a
  purge thread instead of PostgreSQL's VACUUM.
- Gap/next-key locks are the clever bit that blocks phantom inserts.
- There is no single correct MVCC: in-place+undo and new-version+vacuum are both
  reasonable, with opposite cleanup costs.

### References
MySQL Reference Manual (InnoDB engine; locking and transaction model);
cross-checked access paths against the real SQLite experiment in the
PostgreSQL_vs_SQLite topic.
