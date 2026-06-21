# Advanced DBMS — System Design Discussion

**Name:** Pratyush Mohanty
**Roll No.:** 24BCS10238

This repository is my submission for the Advanced DBMS System Design Discussion. Each topic studies how a real database stores data, manages memory, runs transactions, and recovers from crashes. Every number in these docs comes from experiments run on the system.

The write-ups live under [`System_Design_Docs/`](System_Design_Docs/).

---

## Topics

| Topic | What it covers | System studied |
|-------|----------------|----------------|
| [PostgreSQL vs SQLite](System_Design_Docs/PostgreSQL_vs_SQLite/README.md) | Storage layout, page sizes, mmap vs shared_buffers, process models, query timing | SQLite 3, PostgreSQL 17 |
| [PostgreSQL Internals](System_Design_Docs/PostgreSQL_Internals/README.md) | Buffer manager, B-tree (page splits via `pageinspect`), MVCC, WAL, the query planner and `pg_statistic` | PostgreSQL 17.9 |
| [MySQL / InnoDB](System_Design_Docs/MySQL_InnoDB/README.md) | Clustered indexes, secondary indexes, buffer pool, undo/redo logs, row and gap locking, MVCC via undo | MySQL 8.0.42 |
| [RocksDB](System_Design_Docs/RocksDB/README.md) | LSM-tree write path, SSTables and levels, compaction (leveled vs universal), write/read/space amplification, bloom filters | RocksDB 11.1.1 |

---

## Approach

All four topics use the same workload where it makes sense so the engines can be compared directly: a `users` table of 500,000 rows and an `orders` table of 1,000,000 rows, joined on `user_id`.

- For PostgreSQL and InnoDB I ran the experiments against the running servers and used their introspection tools (`EXPLAIN ANALYZE`, `pageinspect`, `pg_buffercache`, `performance_schema.data_locks`, `information_schema`).
- For RocksDB, which is a library rather than a server, I wrote a small C++ harness against its API and inspected the files on disk with `ldb` and `sst_dump`.

Each doc follows the same shape: a short setup, then one section per subsystem with the command, the real output, and a plain observation explaining what it means.

---

## A few findings that connect the topics

Running the same workload on more than one engine made the design differences concrete:

- **Page sizes climb with concurrency ambitions.** SQLite uses 4 KB pages, PostgreSQL 8 KB, InnoDB 16 KB. Bigger pages mean fewer of them and shallower trees, at the cost of more wasted space per page.
- **The same 50,000-row update logged very differently.** PostgreSQL wrote 15 MB of WAL, InnoDB only 4.4 MB of redo. PostgreSQL puts full-page images in the WAL to survive torn writes; InnoDB handles torn writes with a separate doublewrite buffer and keeps its redo log small.
- **The same join produced opposite plans.** PostgreSQL used a parallel hash join (~88 ms); InnoDB used a single-threaded nested loop with clustered-index PK lookups (~732 ms). Neither is simply faster, they're tuned for different query shapes.
- **Three different ways to do MVCC.** PostgreSQL writes a new row version and cleans up later with VACUUM. InnoDB updates in place and rebuilds old versions from the undo log. RocksDB never updates in place at all and reclaims space through compaction. All three reach the same isolation guarantees by very different routes.

---

## Environment

| Component | Version |
|-----------|---------|
| Platform | macOS (Darwin 25.0.0), Apple Silicon |
| SQLite | 3.x (system) |
| PostgreSQL | 17.9 (Homebrew) |
| MySQL / InnoDB | 8.0.42 |
| RocksDB | 11.1.1 (Homebrew) |
