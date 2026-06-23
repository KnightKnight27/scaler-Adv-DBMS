# Advanced DBMS — System Design Documentation

> **Name:** Abdullah Danish &nbsp;|&nbsp; **Roll Number:** 24BCS10054 &nbsp;|&nbsp; **Course:** Advanced DBMS

This repository documents an architectural study of four production database systems. The focus is **not** on copying documentation, but on understanding *why* each system was designed the way it was, *what alternatives existed*, and *what trade-offs were accepted* — backed by hands-on experiments (query plans, benchmarks, and engine statistics).

---

## Topics

| # | Topic | What it covers | Document |
|---|-------|----------------|----------|
| 1 | **PostgreSQL vs SQLite** | Client-server vs embedded design, process model, storage layout, locking vs MVCC, durability | [PostgreSQL_vs_SQLite/README.md](PostgreSQL_vs_SQLite/README.md) |
| 2 | **PostgreSQL Internals** | Buffer manager, nbtree, MVCC + HOT, WAL, crash recovery, planner & `pg_statistic` | [PostgreSQL_Internals/README.md](PostgreSQL_Internals/README.md) |
| 3 | **MySQL / InnoDB** | Clustered + secondary indexes, buffer pool, undo/redo logs, row & gap locks, isolation | [MySQL_InnoDB/README.md](MySQL_InnoDB/README.md) |
| 4 | **RocksDB** | LSM tree, MemTable, SSTables, Bloom filters, compaction, tombstones, read/write paths | [RocksDB/README.md](RocksDB/README.md) |

Each document follows the prescribed structure: **Problem Background → Architecture Overview → Internal Design → Design Trade-Offs → Experiments / Observations → Directly Answering the Study Questions → Key Learnings.**

---

## A Connecting Theme

The four systems are best understood as **four different answers to the same question**: *how do you keep data correct and durable while staying fast under concurrency?*

```
                         Where does the engine pay the cost of MVCC / durability?

  SQLite        →  One writer at a time; durability via single-file atomic commit.
                   Cost paid in: concurrency (deliberately given up for embeddability).

  PostgreSQL    →  Versions live in the heap (append-only); WAL for durability.
                   Cost paid in: dead-tuple bloat → VACUUM. Mitigated by HOT updates.

  InnoDB        →  In-place updates; old versions in undo logs; redo for durability.
                   Cost paid in: undo-segment growth + purge lag.

  RocksDB       →  All writes sequential (WAL + MemTable); deletes are tombstones.
                   Cost paid in: write amplification + background compaction.
```

Every system uses **write-ahead logging** for durability and **multi-versioning** for concurrency — they simply differ in *where the cleanup cost is deferred to*. That single lens explains VACUUM, undo purge, and LSM compaction as the same idea wearing three different costumes.

---

## Experiment Environments

| Topic | Environment |
|-------|-------------|
| PostgreSQL vs SQLite | SQLite 3.46, PostgreSQL 17, Python benchmarks |
| PostgreSQL Internals | PostgreSQL 17 (`pgstattuple`, `pg_statio_user_tables`, `EXPLAIN ANALYZE`) |
| MySQL / InnoDB | MariaDB 11.8 (InnoDB, MySQL-8 compatible) — `EXPLAIN`, `SHOW ENGINE INNODB STATUS` |
| RocksDB | RocksDB 9.10.0, C++ benchmark (`g++ -O2`), `db_bench` |

All diagrams are original ASCII; external sources are credited in each document's References footer.
