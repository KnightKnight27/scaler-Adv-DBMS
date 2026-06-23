# System Design Docs

**Name:** Tirth Shah
**Roll Number:** 24BCS10347
**Course:** Advanced DBMS — System Design Discussion

This directory contains architectural deep-dives into four real database systems.
Each document follows the required structure: *Problem Background → Architecture
Overview → Internal Design → Design Trade-Offs → Experiments / Observations →
Key Learnings*.

| # | Topic | Document | Focus |
|---|-------|----------|-------|
| 1 | PostgreSQL vs SQLite | [PostgreSQL_vs_SQLite/README.md](PostgreSQL_vs_SQLite/README.md) | Client-server vs embedded; process model; storage; concurrency |
| 2 | PostgreSQL Internals | [PostgreSQL_Internals/README.md](PostgreSQL_Internals/README.md) | Buffer manager, nbtree, MVCC, WAL |
| 3 | MySQL / InnoDB | [MySQL_InnoDB/README.md](MySQL_InnoDB/README.md) | Clustered indexes, undo/redo logs, row/gap/next-key locking |
| 4 | RocksDB | [RocksDB/README.md](RocksDB/README.md) | LSM-tree, MemTable/SSTables, compaction, bloom filters |

### Notes on experiments

- **PostgreSQL** and **SQLite** sections use **real measurements** captured locally
  (PostgreSQL 16.13 and SQLite 3.51.0 on macOS) — `EXPLAIN ANALYZE` plans, `pageinspect`
  / `pgstattuple` MVCC inspection, B-tree metapage stats, WAL sizing, and `dbstat` page
  layout, over a shared dataset of `users` (200k rows) and `orders` (500k rows).
- **MySQL/InnoDB** and **RocksDB** experiment sections present **representative,
  documented behavior** from the official manuals and published `db_bench` benchmarks
  (clearly labeled as illustrative, not a local run).
