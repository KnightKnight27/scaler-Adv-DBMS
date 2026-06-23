# MySQL / InnoDB Storage Engine

InnoDB is the default storage engine behind most of the world's MySQL deployments,
and it makes a very different set of architectural bets than PostgreSQL. Where
PostgreSQL stores tables as unordered heaps and keeps every row version, InnoDB
stores every table physically sorted inside its primary key (a clustered index) and
updates rows in place, using undo logs to reconstruct old versions on demand. This
document explains those choices (clustered indexes, the buffer pool, undo and redo
logging, and row-level locking) and demonstrates each with real output from a live
MySQL 9.6 / InnoDB instance.

InnoDB is a storage engine, not a database server in its own right: you get it
through MySQL, which handles SQL, connections, and the rest. Every `EXPLAIN`, lock
dump, and transaction result below was captured from an actual running instance.

---

## 1. Problem Background

InnoDB was created by Heikki Tuuri (Innobase Oy, around 1995) to give MySQL
something it originally lacked: transactions, crash recovery, and row-level
locking. Before InnoDB, MySQL's default engine (MyISAM) used table-level locks and
had no real durability, which was fine for read-heavy websites but unacceptable for
anything handling money.

InnoDB follows the classic Oracle/IBM lineage of transactional engines, and its
goals are:

- ACID transactions with proper isolation levels.
- High concurrency for OLTP, meaning many short transactions hitting different
  rows.
- Fast primary-key access, since most OLTP queries look rows up by key.
- Crash recovery that restores a consistent state without manual repair.

These goals explain its two defining features: clustered storage, which optimizes
primary-key access, and a dual-logging scheme (undo plus redo) that supports both
MVCC and crash recovery.

---

## 2. Architecture Overview

InnoDB is organized around an in-memory buffer pool and two distinct logs. The
buffer pool caches data and index pages, the redo log guarantees durability, and
the undo log preserves old row versions for MVCC and rollback.

```
        SQL ──►  Transaction / Lock manager
                        │
        ┌───────────────┼───────────────────────────────┐
        │                 BUFFER POOL (in RAM)            │
        │   ┌──────────────┐   ┌──────────────────────┐   │
        │   │ data + index │   │  change buffer,       │   │
        │   │ pages (16KB) │   │  adaptive hash index  │   │
        │   └──────┬───────┘   └──────────────────────┘   │
        └──────────┼──────────────────────────────────────┘
        dirty pages │              │ redo records      │ undo records
        (flushed     ▼              ▼                   ▼
         lazily) ┌─────────┐   ┌──────────┐      ┌───────────────┐
                 │ .ibd    │   │ redo log │      │ undo logs     │
                 │ (B+tree │   │ (ib_log) │      │ (rollback seg)│
                 │ tables) │   │ WAL→disk │      │ old versions  │
                 └─────────┘   └──────────┘      └───────────────┘
```

Data flow on an UPDATE:
1. Lock the target row with a row-level exclusive lock.
2. Copy the old row image into an undo log record (used for rollback and for MVCC).
3. Modify the row in place in the buffer-pool page and mark the page dirty.
4. Write a redo log record and flush it at commit (this is the WAL part, for
   durability).
5. The dirty data page is flushed to the `.ibd` file later by background threads.

The contrast with PostgreSQL is already visible. PostgreSQL writes a new tuple and
vacuums the old one; InnoDB overwrites the row and keeps the old image in the undo
log instead.

---

## 3. Internal Design

### 3.1 Clustered index — the table is a B+tree

In InnoDB, a table is not a heap. The rows are stored inside the leaf pages of a
B+tree keyed by the primary key. There is no separate "table" and "PK index"; they
are the same structure. That is what clustered index means: rows adjacent in
primary-key order are adjacent on disk.

The payoff is that a primary-key range scan reads sequential leaf pages:

```sql
EXPLAIN SELECT id, amount FROM orders WHERE id BETWEEN 100 AND 110;
```
```
-> Index range scan on orders using PRIMARY over (100 <= id <= 110)
   (cost=2.47 rows=11)
```

The scan walks the clustered (`PRIMARY`) index directly. The data is right there in
the leaves, with no separate heap fetch.

### 3.2 Secondary indexes — and the "double lookup"

Because the table is clustered on the primary key, a secondary index cannot store a
physical row pointer, since rows move as pages split and merge. Instead, secondary
index leaves store the primary-key value of the row. Looking a row up by a
secondary key therefore takes two B+tree descents: one in the secondary index to
get the PK, then one in the clustered index to fetch the row.

```sql
EXPLAIN SELECT * FROM orders WHERE user_id = 42;
```
```
-> Index lookup on orders using idx_user (user_id = 42)  (cost=1.4 rows=4)
```

This `idx_user` lookup yields the matching PKs, and InnoDB then walks the clustered
index to retrieve the full rows. The practical lesson is that a long primary key
makes every secondary index bigger and every secondary lookup more expensive,
because the PK is duplicated into every secondary index.

### 3.3 Buffer pool — InnoDB's working memory

All reads and writes go through the buffer pool, sized by
`innodb_buffer_pool_size` (64 MB in this demo, often many gigabytes in production).
It caches 16 KB pages and manages them with a midpoint-insertion LRU that resists a
single large scan flushing out the hot working set. It also hosts the change buffer
(which batches secondary-index updates for pages not currently in memory) and the
adaptive hash index (an automatic in-memory hash over hot pages).

### 3.4 Undo logs — MVCC without dead tuples

When InnoDB modifies a row, it first writes the old version into an undo log. That
undo record serves two purposes: rolling back if the transaction aborts, and
reconstructing the row as it looked to an older transaction's snapshot. This is
Oracle-style MVCC, where old versions live in the undo log rather than interleaved
in the table the way PostgreSQL does it.

I demonstrated this with two concurrent sessions under the default REPEATABLE READ:

| Step | Session A (open transaction) | Session B (separate) |
|---|---|---|
| 1 | `START TRANSACTION;` read order #1 → 1.50 | |
| 2 | | `UPDATE ... SET amount=999.99 WHERE id=1; COMMIT;` |
| 3 | re-read order #1 → still 1.50 | sees 999.99 |
| 4 | `COMMIT;` re-read → 999.99 | |

The captured output:
```
A_first_read           = 1.50      ← snapshot taken at txn start
B_sees_committed       = 999.99    ← B committed the change
A_second_read_same_txn = 1.50      ← A reconstructs old row from UNDO log
A_after_commit         = 999.99    ← new transaction sees committed value
```

Session A kept seeing `1.50` even after B committed `999.99`, because InnoDB served
A the old version from the undo log, consistent with the snapshot A took when its
transaction began. No dead tuples accumulate in the table; the undo log is purged
once no transaction needs the old version.

### 3.5 Redo log — durability and crash recovery

The redo log (`ib_logfile` / `innodb_redo_log_capacity`, here 100 MB) is InnoDB's
write-ahead log. Before a committed change is guaranteed durable, its redo record
is flushed to disk. InnoDB flushes dirty data pages lazily, so on a crash the
`.ibd` files may be missing recently committed changes. Recovery then replays the
redo log forward to reapply them, and rolls back any in-flight uncommitted
transactions using the undo log. Two logs, two jobs:

- Redo answers "redo committed work the data files haven't caught up on yet."
- Undo answers "undo uncommitted work, and show old snapshots to readers."

This is why InnoDB needs both: redo for durability (roll forward), undo for
atomicity and isolation (roll back, and time-travel for reads).

### 3.6 Row-level locking, gap locks, and next-key locks

InnoDB locks index records, not whole tables. Under REPEATABLE READ it also takes
gap locks (locking the space between index values) and next-key locks (a record
plus the preceding gap) to prevent phantom reads, where new rows get inserted into
a range another transaction is scanning.

I captured a real lock set. Session A ran
`SELECT id FROM orders WHERE user_id=42 FOR UPDATE`, and a third connection read
`performance_schema.data_locks`:

```
tbl     idx        LOCK_TYPE  LOCK_MODE        LOCK_DATA
orders  NULL       TABLE      IX                 NULL        ← intention lock on table
orders  PRIMARY    RECORD     X,REC_NOT_GAP      15041       ← exclusive lock on the actual row
orders  idx_user   RECORD     X,GAP              43, 42      ← GAP lock (no row, just the gap)
orders  idx_user   RECORD     X                  42, 41      ← NEXT-KEY lock (record + gap)
orders  PRIMARY    RECORD     X,REC_NOT_GAP      41          ← clustered-index row lock
```

Three lock concepts show up at once:
- `IX` (intention exclusive) on the table is a coarse flag saying "I hold row locks
  below," so table-level operations know to wait.
- `X,REC_NOT_GAP` on `PRIMARY` is a pure record lock on the matched clustered rows.
- `X,GAP` and `X` (next-key) on `idx_user` lock the gaps around `user_id=42` so no
  other transaction can insert a phantom `user_id=42` row into the range. This is
  how InnoDB delivers repeatable-read semantics that PostgreSQL achieves purely
  through snapshots.

---

## 4. Design Trade-Offs

Clustered storage
- Primary-key lookups and range scans are extremely fast, since the row is in the
  index leaf and adjacent keys are physically adjacent.
- No separate heap means no `ctid`-style indirection for PK access.
- Secondary lookups pay a double descent (secondary → PK → clustered).
- A fat or random primary key (a UUID, say) hurts twice: it bloats every secondary
  index and causes page-split churn on insert. This is why auto-increment PKs are
  the InnoDB norm.

In-place updates plus undo logs (versus PostgreSQL's versioned heap)
- The table doesn't bloat with dead rows, so no `VACUUM` is needed for table
  cleanup.
- Old versions are compactly stored and purged automatically.
- MVCC reads of frequently-updated rows must walk undo records to rebuild old
  versions. Long-running transactions can make undo chains long and reads slower,
  and can prevent undo purge (the "history list length" problem).

Locking model
- Gap and next-key locks give phantom protection at REPEATABLE READ without
  serializing everything.
- Those same gap locks can block inserts that seem unrelated and are a classic
  source of deadlocks, so understanding them is essential for high-concurrency
  tuning.

### Why did PostgreSQL choose differently?
PostgreSQL keeps all versions in the table and vacuums them; InnoDB keeps the
current row in the table and old versions in undo. InnoDB's model keeps tables
compact and PK-clustered, which is great for OLTP point access, at the cost of
undo-chain maintenance. PostgreSQL's model avoids undo chains and keeps updates
uniform, at the cost of table bloat and mandatory vacuuming. Neither is universally
better; they optimize different points of the same trade-off space.

---

## 5. Experiments / Observations

Dataset: `users` (5,000 rows) and `orders` (20,000 rows), both `ENGINE=InnoDB`, on
MySQL 9.6, default isolation REPEATABLE READ, buffer pool 64 MB.

1. Clustered PK range scan. `WHERE id BETWEEN 100 AND 110` produced an
   `Index range scan ... using PRIMARY`, reading sequential clustered-index leaves
   (§3.1).
2. Secondary index lookup. `WHERE user_id=42` used `idx_user` then resolved rows
   via the clustered index, confirming the two-step secondary → PK access (§3.2).
3. MVCC via undo log. A transaction kept reading `amount=1.50` even after a
   concurrent session committed `999.99`, proving consistent-read reconstruction
   from undo (§3.4).
4. Gap and next-key locks. `SELECT ... FOR UPDATE` produced an intention table
   lock, clustered record locks, and gap plus next-key locks on the secondary
   index, visible in `performance_schema.data_locks` (§3.6).
5. Defaults observed. `transaction_isolation = REPEATABLE-READ` and
   `innodb_redo_log_capacity = 100 MB`, confirming InnoDB's stricter default
   isolation than PostgreSQL's READ COMMITTED.

---

## 6. Key Learnings

"The table is an index" turned out to change everything. Once the rows live inside
the primary-key B+tree, fast PK access, expensive fat PKs, and the secondary-index
double lookup all follow logically. Choosing a small, monotonic primary key isn't a
micro-optimization in InnoDB; it is a structural decision that shows up in every
secondary index.

The two-log scheme finally clicked when I traced recovery. Redo rolls committed
work forward after a crash; undo rolls uncommitted work back and serves old
snapshots to readers. They aren't redundant, because they cover opposite directions
in time, and you genuinely need both.

What struck me most was how InnoDB and PostgreSQL reach the same MVCC guarantee by
opposite routes. I saw the identical "reader sees the old value" behavior in both
engines, but InnoDB rebuilt it from undo while PostgreSQL kept a second tuple in the
heap. The guarantee is the same; the bookkeeping, and therefore the maintenance
cost, is completely different.

Two smaller takeaways:
- Locks are richer than "row vs table." Seeing real gap and next-key locks in
  `data_locks` made it concrete why InnoDB can prevent phantoms at REPEATABLE READ,
  and why those same locks are a frequent and surprising source of deadlocks.
- Compactness versus simplicity is the core trade-off. InnoDB keeps tables tidy
  (no vacuum) but pays with undo-chain maintenance and lock complexity; PostgreSQL
  keeps updates simple but pays with bloat and vacuum. Knowing which cost you are
  signing up for is the real point of comparing the two.

---

### References
- MySQL Reference Manual — *InnoDB Storage Engine*: architecture, clustered and
  secondary indexes, buffer pool, redo and undo logs, locking:
  <https://dev.mysql.com/doc/refman/8.0/en/innodb-storage-engine.html>
- *MySQL InnoDB Locking and Transaction Model*:
  <https://dev.mysql.com/doc/refman/8.0/en/innodb-locking.html>
- Jeremy Cole, *"InnoDB: A journey to the core"* blog series:
  <https://blog.jcole.us/innodb/>

*Experiments performed locally on MySQL 9.6 / InnoDB; all `EXPLAIN`, lock, and
transaction output is copied from actual runs.*
