# System Design Lab — Storage Engine Internals

- **Role Number:** 24BCS10345
- **Name:** Ansh Mahajan
- **Date:** 2026-06-22

## Aim

Study how four widely-used storage engines actually store and retrieve data,
and write up — in my own words, with my own diagrams — their architecture,
internal data structures, the trade-offs they deliberately make, and how they
compare. This lab is the "design" counterpart to the earlier coding labs: where
Lab 3–8 *built* individual mechanisms (buffer cache, B-tree, transactions),
this lab steps back and looks at how real engines assemble those mechanisms
into a whole.

## Why these four

| Engine | Storage model | Why it is here |
| --- | --- | --- |
| **MySQL / InnoDB** | Update-in-place B+tree (clustered index) | The classic OLTP storage engine; index-organized tables. |
| **PostgreSQL** | Heap tables + MVCC + WAL | A different MVCC strategy (old row versions live in the heap). |
| **PostgreSQL vs SQLite** | Client-server vs single-file embedded | Same SQL, opposite deployment philosophies. |
| **RocksDB** | Log-Structured Merge tree (LSM) | The write-optimized alternative to B-trees. |

The recurring theme is the **read/write/space amplification triangle**: no
engine wins on all three, and each document calls out which corner its engine
sacrifices.

## Contents

| Document | Focus |
| --- | --- |
| [MySQL_InnoDB/](MySQL_InnoDB/README.md) | Clustered B+tree, buffer pool, redo/undo, MVCC, doublewrite |
| [PostgreSQL_Internals/](PostgreSQL_Internals/README.md) | Heap pages, tuple visibility (xmin/xmax), WAL, VACUUM, TOAST |
| [PostgreSQL_vs_SQLite/](PostgreSQL_vs_SQLite/README.md) | Architecture, concurrency, file layout, when to pick which |
| [RocksDB/](RocksDB/README.md) | MemTable, SSTable, WAL, compaction, Bloom filters, LSM trade-offs |

## Connection to earlier labs

- **Lab 2** already benchmarked SQLite vs PostgreSQL page sizes and timings;
  the [PostgreSQL_vs_SQLite](PostgreSQL_vs_SQLite/README.md) document builds on
  that with the architectural *why*.
- **Lab 3** implemented a CLOCK-sweep buffer cache — the same idea InnoDB's
  buffer pool and PostgreSQL's shared buffers use, described here in context.
- **Lab 4** dissected the SQLite file format byte-by-byte; that on-disk view is
  referenced when contrasting it with InnoDB pages and PostgreSQL heap pages.
- **Lab 6** implemented a B-tree; InnoDB's clustered index is a production B+tree
  variant of exactly that structure.
- **Lab 8** implemented MVCC + 2PL; both InnoDB and PostgreSQL are studied here
  through the lens of how *they* do MVCC differently.

## Key takeaway

Storage-engine design is the art of choosing which cost to pay. B-tree engines
(InnoDB, SQLite, PostgreSQL) keep reads cheap and pay on random writes; LSM
engines (RocksDB) make writes sequential and pay on reads and background
compaction. Understanding *where* each engine spends its budget is what lets you
pick the right one for a workload.
