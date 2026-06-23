# System Design Docs

**Author:** Gauri Shukla
**Roll number:** 24BCS10115
**Course:** Advanced DBMS, System Design Discussion

This folder contains my system-design analyses of four real database systems. Each one is a study of *why* the system is built the way it is and what trade-offs its designers accepted, backed by experiments I ran on my own machine rather than copied numbers.

## Topics

| # | Topic | Folder | What it focuses on |
|---|---|---|---|
| 1 | PostgreSQL vs SQLite | [`PostgreSQL_vs_SQLite/`](./PostgreSQL_vs_SQLite/) | Client-server vs embedded, process model, concurrency, storage |
| 2 | PostgreSQL Internals | [`PostgreSQL_Internals/`](./PostgreSQL_Internals/) | Buffer manager, B-tree, MVCC, WAL, VACUUM, the planner |
| 3 | MySQL / InnoDB | [`MySQL_InnoDB/`](./MySQL_InnoDB/) | Clustered indexes, undo/redo logs, row and gap locking |
| 4 | RocksDB | [`RocksDB/`](./RocksDB/) | LSM trees, compaction, Bloom filters, the amplification trilemma |

## How I approached this

I used one consistent e-commerce dataset (50,000 customers, 200,000 orders, 200,000 order items) across PostgreSQL, SQLite, and MySQL so the comparisons are apples to apples, and a 2,000,000-key benchmark for RocksDB. For each topic I installed the real engine, loaded data, and captured actual output: query plans, page dumps, lock traces, MVCC version chains, and benchmark numbers. The raw captures and the exact SQL or source code live in each topic folder next to its README, so everything is reproducible.

## Software versions used

- PostgreSQL 16.14 (Homebrew)
- SQLite 3.51.0
- MySQL 9.6.0 with InnoDB
- RocksDB 11.1.1 (compiled against `librocksdb`)

## A thread that runs through all four

The same question, "how do you handle concurrency and durability without losing performance," gets four different answers:

- **SQLite** keeps it simple with one writer and a file lock, because it is embedded in a single app.
- **PostgreSQL** uses multi-version concurrency control and cleans up later with VACUUM, so readers never block writers.
- **InnoDB** updates in place, parks old versions in an undo log, and locks at the row (and gap) level.
- **RocksDB** never updates in place at all, appending everything and reorganizing in the background with compaction.

Seeing the same workload behave so differently across the four is the real lesson: a database is a pile of trade-offs, and which pile you want depends entirely on your workload.
