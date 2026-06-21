# Advanced DBMS — System Design Assignment

> **Name:** Krritin Keshan
> **Roll Number:** 24BCS10122
> **Course:** Advanced DBMS

This is my system design assignment for the Advanced DBMS course. I picked four database systems and tried to figure out how each one is built, and why the people who built it made the choices they did.

I didn't want to just copy what the docs say. I wanted to understand the trade-offs each system makes — where it's fast, where it's slow, and what it gives up to be good at what it's good at. To back that up, I ran some real experiments along the way: query plans, small benchmarks, and looking at engine stats.

---

## The Four Topics

| # | Topic | What it's about | Read |
|---|-------|-----------------|------|
| 1 | **PostgreSQL vs SQLite** | Same SQL, very different designs. | [Topic_1_PostgreSQL_vs_SQLite/](Topic_1_PostgreSQL_vs_SQLite/README.md) |
| 2 | **PostgreSQL Internals** | How PostgreSQL actually runs a query — buffer manager, MVCC, WAL, and the planner. | [Topic_2_PostgreSQL_Internals/](Topic_2_PostgreSQL_Internals/README.md) |
| 3 | **MySQL / InnoDB** | Clustered indexes, undo and redo logs, row and gap locking, isolation levels. | [Topic_3_MySQL_InnoDB/](Topic_3_MySQL_InnoDB/README.md) |
| 4 | **RocksDB** | What an LSM tree is, and what changes when you design a database around "writes are cheap". | [Topic_4_RocksDB/](Topic_4_RocksDB/README.md) |

Each one follows the same outline: **Problem Background → Architecture Overview → Internal Design → Trade-Offs → Experiments → Key Learnings.**

---

## What All Four Have In Common

Reading them side by side, I noticed that all four are basically answering the same question in different ways:

> How do you keep data correct and durable, while still being fast when many people use the database at the same time?

And each one pays the cost in a different place:

```
  SQLite      →  Only one writer at a time. Durability comes from an atomic file swap.
                 The cost: it gives up concurrency. Fine for an app or a phone, not a busy server.

  PostgreSQL  →  Keeps old row versions sitting in the heap. WAL handles durability.
                 The cost: dead rows pile up, so VACUUM has to run regularly. HOT updates help.

  InnoDB      →  Updates rows in place. Old versions go into undo logs. Redo log for durability.
                 The cost: undo logs grow, and the purge thread has to keep up.

  RocksDB     →  Every write goes to a log + memory first. Deletes become tombstones.
                 The cost: writes get amplified, and background compaction can be heavy.
```

So all of them use **write-ahead logging** and **multi-versioning**. The real difference is where they push the cleanup cost. Once I saw that, things like `VACUUM`, undo purge, and LSM compaction stopped looking like three separate problems — they're the same idea, just handled differently.

---

## How I Tested Things

| Topic | Tools I used |
|-------|--------------|
| PostgreSQL vs SQLite | SQLite 3.46, PostgreSQL 17, a few small Python benchmarks |
| PostgreSQL Internals | PostgreSQL 17 — `pgstattuple`, `pg_statio_user_tables`, `EXPLAIN ANALYZE` |
| MySQL / InnoDB | MariaDB 11.8 (InnoDB, MySQL-8 compatible) — `EXPLAIN`, `SHOW ENGINE INNODB STATUS` |
| RocksDB | RocksDB 9.10.0 with a small C++ benchmark (`g++ -O2`), plus `db_bench` |

The ASCII diagrams in each topic are my own. Any external sources I leaned on are credited at the bottom of that topic's README.

---

> Submitted as the **System Design Assignment** for Advanced DBMS.
> — Krritin Keshan (24BCS10122)
