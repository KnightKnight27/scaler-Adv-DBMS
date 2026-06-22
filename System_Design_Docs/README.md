# System Design Docs — Advanced DBMS

Architecture studies of four real database systems. Each topic folder has a self-contained `README.md` plus the scripts and raw experiment output used to write it — every plan, lock dump, and amplification number is measured locally, not copied from documentation.

| Topic | Engine(s) | What it focuses on |
|---|---|---|
| [PostgreSQL_vs_SQLite](./PostgreSQL_vs_SQLite/) | PostgreSQL 18.3, SQLite 3.51 | client-server vs embedded; how one architectural choice cascades |
| [PostgreSQL_Internals](./PostgreSQL_Internals/) | PostgreSQL 18.3 | buffer manager, B-tree, MVCC (`xmin`/`xmax`/`ctid`), WAL, planner & statistics |
| [MySQL_InnoDB](./MySQL_InnoDB/) | MySQL 9.6 / InnoDB | clustered index, secondary back-reference, undo+redo logs, gap locks |
| [RocksDB](./RocksDB/) | RocksDB 11.1.1 | LSM-tree, compaction, bloom filters, write/read/space amplification |

**Method.** Tools used for live measurement: `EXPLAIN (ANALYZE, BUFFERS)`, `pageinspect`, `pg_buffercache`, `pg_stats` (Postgres); `EXPLAIN QUERY PLAN`, `dbstat` (SQLite); `performance_schema.data_locks`, `innodb_index_stats`, `SHOW ENGINE INNODB STATUS` (InnoDB); a custom C++ benchmark against `librocksdb` using RocksDB's `Statistics` counters (RocksDB). To reproduce, see the `*.sql` / `run.sh` scripts in each folder.

A recurring theme across all four: a database is a pile of trade-offs. Append-vs-update, heap-vs-clustered, lock-vs-version, sequential-vs-random — each engine picks a corner and pays for it somewhere else.
