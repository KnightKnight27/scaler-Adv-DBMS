# System Design Discussion — Advanced DBMS

Architectural deep-dives into four real database systems, each documented with **live
experimental output** (no invented numbers — every query plan, page dump, and file size
below was captured on the versions listed).

| # | Topic | Focus | Tooling used for experiments |
|---|---|---|---|
| 1 | [PostgreSQL vs SQLite](./PostgreSQL_vs_SQLite/README.md) | client-server vs embedded; storage, concurrency, durability | SQLite 3.51 · PostgreSQL 16.13 |
| 2 | [PostgreSQL Internals](./PostgreSQL_Internals/README.md) | buffer manager, nbtree, MVCC, WAL, planner & `pg_statistic` | PostgreSQL 16.13 (`pageinspect`, `pg_buffercache`) |
| 3 | [MySQL / InnoDB](./MySQL_InnoDB/README.md) | clustered index, redo/undo logs, buffer pool, next-key locks | MySQL 9.3.0 / InnoDB |
| 4 | [RocksDB](./RocksDB/README.md) | LSM-tree, MemTable→SST, compaction, Bloom filters, amplification | RocksDB 11.1.1 (`ldb`) |

## The thread connecting all four

These systems are four points in the same design space — **how to store rows durably and
let many transactions touch them correctly and fast**:

- **Storage shape:** SQLite = B-tree-as-table · PostgreSQL = heap + side indexes · InnoDB = clustered B-tree (table *is* the PK index) · RocksDB = LSM tree (never update in place).
- **MVCC, two ways:** PostgreSQL keeps every row version *in the heap* and cleans up with **VACUUM**; InnoDB updates in place and reconstructs old versions from the **undo log** on read. Same guarantee, mirror-image cost.
- **Durability, one way:** *every* system writes a **write-ahead log** before touching its main structure — SQLite's `-wal`, PostgreSQL's WAL, InnoDB's redo log, RocksDB's WAL.
- **Garbage collection:** PostgreSQL's VACUUM and RocksDB's compaction are the *same job* — reclaiming superseded versions — at different structural granularity.

> Database systems are collections of engineering trade-offs. The goal across these four
> documents is to show **what alternative each team had, which they chose, and what they
> paid for it.**

*Submitted for Advanced DBMS — System Design Discussion · Roll No. 24BCS10123*
