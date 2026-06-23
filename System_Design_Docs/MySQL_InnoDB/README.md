# MySQL / InnoDB Storage Engine

## 1. Problem Background
InnoDB is MySQL's default engine, made to give MySQL real transactions, crash recovery, and row-level concurrency. The older MyISAM was fast but locked whole tables and wasn't crash-safe, so a power cut could break tables. InnoDB fixes that with ACID transactions, undo and redo logs, and fine-grained locking. It borrows a lot from Oracle, including its MVCC style. Its key idea is that row data lives inside the primary key index (a clustered index).

## 2. Architecture Overview
```
   SQL -> MySQL server (parser, optimizer)
                 |
          InnoDB engine
          +---------------------+
          |  Buffer Pool (RAM)  |
          |  data + index pages |
          +----------+----------+
                     |
  Redo Log <-- changes --> Undo Log (old versions)
                     |
        Clustered index B+tree (data in leaves)
                     |
                .ibd data files
```
Rows live in the PK B+tree. The buffer pool caches pages, redo log makes changes durable, undo log keeps old versions for MVCC and rollback.

## 3. Internal Design
In InnoDB the table *is* its primary key B+tree, and the actual row data sits in the leaves. That's a clustered index. Secondary indexes don't store row data, they store the PK value, so a secondary lookup finds the PK then walks the clustered index again. Updates happen in place and the old version goes to the undo log so other transactions can still read it (that's the MVCC part). Redo log records page changes for durability. Locking is row-level, plus gap locks to block phantom rows.

## 4. Design Trade-Offs
Clustered storage makes PK lookups and range scans fast because related rows sit together. The cost: secondary lookups need a second hop through the clustered index, and a big PK bloats every secondary index. In-place updates with undo keep the table compact (no dead-tuple bloat like Postgres) but add undo and purge work. Row + gap locks give strong isolation but can deadlock. Versus Postgres, InnoDB trades VACUUM cleanup for undo-log overhead.

## 5. Experiments / Observations
With `EXPLAIN`, a primary key lookup shows very cheap clustered access, while filtering a non-indexed column shows a full scan of the clustered tree. A secondary index lookup uses the index but you can reason it then does PK lookups behind the scenes. If a query only needs columns in the secondary index plus the PK, it's a "covering" index and skips the second hop, which is cheaper. `SHOW ENGINE INNODB STATUS` during concurrent updates also reveals row locks and deadlocks.

## Suggested Questions

**Why does InnoDB need both undo and redo logs?**
They solve opposite problems. The redo log is for durability and going forward: it records committed page changes so after a crash InnoDB can replay them and not lose work, even if the data files weren't updated yet. The undo log is for rolling back and for MVCC: it keeps the old version of each row so a transaction can be undone and other transactions can still read the old committed value. So redo means "don't lose committed data", undo means "revert and give readers a consistent snapshot".

**What advantages do clustered indexes provide?**
Because row data lives in the leaves of the PK B+tree, fetching a row by primary key is one traversal with no extra lookup, the data is right there. Range scans on the PK are fast too since rows are physically in order and stored next to each other, so reads are sequential and cache-friendly. There's no separate heap to maintain. It works great when you mostly access data by primary key or in PK order, like ID-ordered or time-ordered records.

**Why did PostgreSQL choose a different MVCC model?**
Postgres keeps all row versions in the heap (add a new version, mark the old one) instead of pushing old versions to undo like InnoDB. The upside is cheap rollback and reads never rebuild an old version from undo, they're just sitting there. The downside is dead tuples that need VACUUM. InnoDB went the Oracle route: update in place, old versions in undo, which keeps tables compact but adds undo and purge work. Both reach lock-free reads, they just decided where old versions live.

## 6. Key Learnings
The clustered index is the idea everything hangs off. Once data lives in the PK tree, fast PK access, the secondary-index second hop, and covering indexes all follow. The other lesson is undo vs VACUUM: InnoDB and Postgres both give MVCC but pay for it in different places, undo-log maintenance vs dead-tuple cleanup. Neither is free. Seeing two mature engines make opposite calls and both work showed me these are trade-offs about where the cost lands.
