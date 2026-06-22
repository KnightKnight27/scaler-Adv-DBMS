# System Design Docs — Advanced DBMS

Architecture deep-dives into four production database systems. Every topic folder is self-contained: a standalone `README.md` alongside the scripts and raw experiment logs it was written from — each query plan, lock dump, and amplification figure was measured on a local instance rather than lifted from the manuals.

| Topic | Engine(s) | What it focuses on |
|---|---|---|
| [PostgreSQL_vs_SQLite](./PostgreSQL_vs_SQLite/) | PostgreSQL 18.3, SQLite 3.51 | client-server vs embedded; how one architectural choice cascades |
| [PostgreSQL_Internals](./PostgreSQL_Internals/) | PostgreSQL 18.3 | buffer manager, B-tree, MVCC (`xmin`/`xmax`/`ctid`), WAL, planner & statistics |
| [MySQL_InnoDB](./MySQL_InnoDB/) | MySQL 9.6 / InnoDB | clustered index, secondary back-reference, undo+redo logs, gap locks |
| [RocksDB](./RocksDB/) | RocksDB 11.1.1 | LSM-tree, compaction, bloom filters, write/read/space amplification |

**Method.** The live-measurement tooling, engine by engine: `EXPLAIN (ANALYZE, BUFFERS)`, `pageinspect`, `pg_buffercache`, `pg_stats` (Postgres); `EXPLAIN QUERY PLAN`, `dbstat` (SQLite); `performance_schema.data_locks`, `innodb_index_stats`, `SHOW ENGINE INNODB STATUS` (InnoDB); and for RocksDB, a hand-written C++ benchmark linked against `librocksdb` that reads its `Statistics` counters. Each folder's `*.sql` / `run.sh` scripts let you reproduce the numbers.

One idea keeps surfacing across all four engines: a database is fundamentally a bundle of compromises. Append-vs-update, heap-vs-clustered, lock-vs-version, sequential-vs-random — every engine commits to one side and pays the bill on the other.
