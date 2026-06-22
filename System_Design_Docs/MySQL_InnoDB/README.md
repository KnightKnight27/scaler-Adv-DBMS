# MySQL / InnoDB Storage Engine

This is my write-up for Topic 3. Unlike PostgreSQL, I hadn't used
MySQL/InnoDB in any of the labs, so this one is more "reading and
thinking" than "reading and comparing to my own code." Where it makes
sense I've contrasted with what PG does, since we already covered that
in Topic 2.

## 1. Problem Background

MySQL itself is older than InnoDB. The original MySQL storage engine
(MyISAM) was extremely fast for read-heavy workloads but didn't
support transactions, foreign keys, or row-level locking. That was
fine for early-2000s read-mostly web apps but unsuitable for anything
needing real ACID guarantees.

InnoDB was developed by a separate company (Innobase Oy, later
acquired by Oracle) specifically to give MySQL transactional storage.
Since MySQL 5.5 (2010) InnoDB has been the default engine. The design
goal: be a real OLTP storage engine — clustered storage, row-level
locking, MVCC, redo + undo logs, crash-safe — bolted onto MySQL's
existing SQL layer.

So InnoDB is the storage engine I'm writing about; "MySQL" without
qualification almost always means "MySQL with InnoDB" today.

## 2. Architecture Overview

InnoDB inside the MySQL server roughly looks like this:

```
                MySQL server (mysqld)
                +---------------------+
                |  parser / optimiser |
                |  + connection layer |
                +----------+----------+
                           |
                           v  storage engine API (handler.h)
                  +-----------------+
                  |     InnoDB      |
                  +---+---+---+---+-+
                      |   |   |   |
                      v   v   v   v
                  buffer  redo  undo  pages
                  pool    log   log   on disk
                                       (.ibd files)
```

Important InnoDB-specific pieces:

- **Buffer pool** — like PG's `shared_buffers`. Default 128 MB,
  caches 16 KB pages.
- **Redo log** (`ib_logfile*` or `#innodb_redo/`) — write-ahead log
  for crash recovery. Conceptually like PG's WAL.
- **Undo log** (in the system tablespace or per-table undo tablespaces)
  — stores the *previous* version of any row a transaction modifies,
  so that older transactions can still see it (MVCC) and so that the
  transaction itself can be rolled back.
- **Doublewrite buffer** — protects against torn writes (partial page
  writes during a crash).
- **Insert buffer / change buffer** — defers index updates to
  non-leaf pages that aren't currently in memory.
- **`.ibd` files** — one per table by default (`innodb_file_per_table
  = ON`). Each `.ibd` is a **clustered index** of the table.

## 3. Internal Design

### 3.1 Clustered indexes (the big one)

This is the most important difference vs PostgreSQL.

In PG, the **heap** is unordered. Every index is a secondary index
holding `(blockno, offset)` pointers into the heap. A lookup costs one
B-tree descent plus a heap fetch.

In InnoDB, the **primary key is the table**. The table's data lives
in the leaves of a B-tree keyed by the primary key. There's no
separate heap. So a primary key lookup is just a B-tree descent — you
arrive at the leaf and the entire row is right there.

```
PostgreSQL                       InnoDB

 heap pages                       clustered index (= the table)
 +---+---+---+---+                       (B-tree)
 | r | r | r | r |  <-- TIDs pointed         /  |  \
 +---+---+---+---+      into here by         leaves hold ENTIRE rows
   ^                    secondary indexes
   |
   (idx B-tree leaf)
```

Secondary indexes in InnoDB don't hold row pointers like PG does —
they hold the **primary key value** of the matching row. So a
secondary index lookup is *two* B-tree descents: the secondary index
finds the PK, then you descend the clustered index using that PK to
get the row. (For covering indexes that include all the columns you
need, the second descent is skipped — which is why "covering indexes"
are such a big optimisation tip in MySQL.)

This trade-off is real:

- **Primary key lookup is faster in InnoDB.** One descent, the row is
  right there.
- **Secondary index lookup is slower in InnoDB** if you need columns
  not in the index — two descents instead of one + heap fetch.
- **The primary key affects table size and write performance.** Random
  primary keys (UUIDv4) cause page splits all over the tree as inserts
  happen out of order. Sequential primary keys (auto-increment, ULID,
  UUIDv7) append cleanly. This is why MySQL guides hammer on "use a
  small monotonically-increasing primary key."

### 3.2 Buffer pool

Same idea as PG's `shared_buffers`, but with two big differences:

- InnoDB uses a **modified LRU** rather than a clock sweep. The list is
  split into "young" and "old" halves. New pages enter the old half;
  only if they're touched again after a delay do they move to the
  young half. This protects the cache from being flushed by a single
  big scan that touches everything once.
- InnoDB has an **adaptive hash index** built on top of the buffer
  pool — it watches which keys are accessed often, and builds an
  in-memory hash for those so lookups skip B-tree descent entirely.
  This doesn't exist in PG.

Lab 3's clock sweep wouldn't be a drop-in replacement for InnoDB's
LRU, but the underlying problem (which page to kick out when the pool
is full) is the same.

### 3.3 Undo logs vs PostgreSQL's tuple versioning

This is the other big architectural divergence.

PG keeps **all versions of a row inline in the heap**. The current
version + every old version still sit in heap pages until VACUUM
cleans them up. MVCC visibility is decided by reading `xmin`/`xmax`
on each tuple.

InnoDB does **in-place updates** on the row, but stashes the
*previous* version into the **undo log**. The row in the clustered
index always shows the latest data; readers who need an older snapshot
follow a pointer from the row header into the undo log and reconstruct
the version they should see.

```
   clustered index leaf
   +----------------------+
   | row id | latest data |
   |        | DB_TRX_ID   |--+
   |        | DB_ROLL_PTR |  |
   +----------------------+  |
                             v
                          undo log entry: "previous value before TRX X"
                             |
                             v (roll-pointer chain)
                          undo log entry: "previous value before TRX W"
                             ...
```

So an MVCC read in InnoDB is "fetch latest row → walk back through
undo records until the transaction id matches what your snapshot can
see."

Pros: no table bloat, no equivalent of `VACUUM`, current-version
reads are fast (no version chain to walk).

Cons: long-running transactions force the undo log to grow huge
because the data those transactions need to see has to be preserved.
Modern InnoDB has had real horror stories about an undo log filling
up because of one runaway transaction.

### 3.4 Redo log

Same role as PG's WAL — durable log of changes, written before the
dirty page hits disk. Without it, a crash between buffer modification
and page flush would lose data.

Two differences worth noting:

- The redo log is **fixed size** by default (a ring buffer). PG's WAL
  is a sequence of files that can grow. If InnoDB can't flush dirty
  pages fast enough, the redo log fills, and the server starts
  throttling new transactions. That's a unique tuning concern.
- The redo log is **only** for crash recovery. Replication in MySQL
  uses a separate stream — the **binary log** (binlog) — which records
  logical row changes. So MySQL maintains *two* logs per write. PG's
  WAL handles both jobs.

### 3.5 Locking and isolation

Row-level locking, like PG. But InnoDB also has **gap locks** and
**next-key locks**, which lock the gaps *between* index records to
prevent phantoms under `REPEATABLE READ` (the default).

```
index: ... 10 ----- 20 ----- 30 ...
            ^^^^^^^^^^^^^^^^
            a next-key lock on (10, 20] also
            locks the gap before 20, so no
            other txn can insert 15 here
```

This is how InnoDB gives serializable-feeling behaviour at the
`REPEATABLE READ` isolation level (which PG calls "snapshot
isolation" and doesn't quite give the same guarantee at the same
isolation name).

The downside: gap locks block more than seems necessary, and they're
a common source of deadlocks in write-heavy workloads.

### 3.6 Crash recovery

On startup InnoDB does the classic ARIES three-phase recovery:

1. **Analysis** — scan redo log from last checkpoint forward, figure
   out which transactions were active at the time of the crash.
2. **Redo** — replay logged changes for all transactions, including
   ones that never committed.
3. **Undo** — for transactions that were active at crash time, use
   the undo log to roll their changes back.

The undo log is what makes the third phase possible. PG can skip the
undo phase entirely because it never updated in place — uncommitted
versions are just tuples whose `xmin` is aborted, and they get
ignored on the next read (and cleaned up by VACUUM later).

## 4. Design Trade-Offs

### Clustered storage

**Wins:** PK lookups are faster (one descent), range scans on PK are
sequential, table fits more naturally into the storage engine.

**Loses:** secondary index lookups need two descents, the choice of
PK matters a lot (random PK = constant splits, terrible for write
throughput), and "altering the PK" is a major operation because the
whole table is reorganised around it.

### Undo logs vs VACUUM

InnoDB's in-place + undo design avoids table bloat. PG's append-only
+ VACUUM design is simpler but produces dead rows everyone has to
manage.

InnoDB's design fails badly under long-running transactions — the
undo log can't be truncated while old readers might need it. PG fails
differently — bloated tables and long autovacuum runs.

Both are correct; both have known failure modes.

### Redo + undo vs WAL alone

InnoDB needs both. Redo for "make committed changes durable across a
crash," undo for "make in-flight changes reversible." PG only needs
WAL because it never updates in place — rollback is just "mark this
txn aborted."

### Adaptive hash index, change buffer, doublewrite

InnoDB has a lot of micro-optimisations that aren't visible in PG.
Each of them solves a real problem (hash index → fast point lookups;
change buffer → batch secondary index updates; doublewrite → survive
torn writes). They make InnoDB feel more "engineered for OLTP," but
also more complex to operate.

### Two log streams (redo + binlog)

Replication in MySQL needs the binlog because the redo log doesn't
contain enough information to replay on a different server (it
references InnoDB page offsets). So MySQL writes a row-level binlog
*in addition* to its redo log. That's twice the write amplification
on the same change. PG avoids this by making the WAL self-describing
enough to support replication directly.

## 5. Experiments / Observations

I didn't have an InnoDB instance set up for the labs, so I'm working
from documentation and the MySQL source rather than measurements.

If I were to set up an experiment, the interesting ones would be:

- Insert 1M rows with a sequential auto-increment PK vs the same rows
  with a UUIDv4 PK. Watch how the `.ibd` file size and insert
  throughput differ — UUIDv4 should be visibly slower because of
  constant page splits.
- Run a long-running `SELECT` (`SELECT pg_sleep(...)` equivalent —
  in MySQL just a slow query) while another session does heavy
  `UPDATE`s on the same table. Watch the undo log grow.
- Compare a covering index vs a non-covering index for the same
  query. The covering one should be visibly faster because the second
  B-tree descent is avoided.

The architectural takeaway is that InnoDB is making the same big
decisions as PG (B-trees, MVCC, WAL-style logging, row-level locking)
but every detail is reversed: clustered vs heap, in-place vs
append-only, undo log vs `xmin`/`xmax`, fixed redo ring vs growing
WAL, gap locks vs no gap locks.

## 6. Key Learnings

- **Clustered indexes are a fundamentally different storage choice.**
  Everything downstream — secondary index format, PK selection, page
  split behaviour — changes once you commit to clustering rows by
  primary key. Neither model is "right." PG and InnoDB just chose
  differently.

- **You need both redo and undo if you do in-place updates.** The
  redo log makes committed changes durable; the undo log makes
  uncommitted changes reversible. PG dodges undo entirely by never
  updating in place — that's a really elegant simplification once you
  see it laid out side by side.

- **Long-running transactions are the silent killer in both designs.**
  In PG they prevent VACUUM. In InnoDB they prevent undo cleanup.
  Either way, the storage engine has to keep "old enough" data
  visible to the oldest active reader, and that has costs.

- **MySQL maintains two logs per write because of how replication was
  bolted on later.** This is the kind of thing you only really see by
  comparing systems. PG's single-WAL design feels cleaner in
  hindsight, but PG also didn't have a non-transactional engine to
  retrofit replication onto.

- **InnoDB has more clever micro-optimisations** (adaptive hash index,
  change buffer, doublewrite buffer). PG mostly leans on the OS page
  cache and a simpler buffer manager. Both work in production; the
  difference is more philosophical than performance-defining.

- Reading about InnoDB after building Lab 6 (MVCC + 2PL) made the
  visibility-via-undo-log model click much faster. The same mental
  model applies — the implementation just stores the old version in a
  different place.

## References

- MySQL reference manual, InnoDB section:
  <https://dev.mysql.com/doc/refman/8.0/en/innodb-storage-engine.html>
- "High Performance MySQL" (Schwartz, Zaitsev, Tkachenko)
- MySQL source — `storage/innobase/`
- Jeremy Cole's "InnoDB and the file format" series:
  <https://blog.jcole.us/2013/01/03/the-basics-of-innodb-space-file-layout/>
- This repo: `Lab6/` (MVCC + 2PL — the in-memory analog of the same
  visibility logic InnoDB does via undo log walks).
