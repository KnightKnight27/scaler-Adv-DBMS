# System Design Discussion

**Roll Number:** 24BCS10183
**Name:** Aman Yadav
**Class:** B (2nd Year)

Four write-ups covering the rubric topics for the System Design Discussion
component of the Advanced DBMS course. Each topic lives in its own folder
with a focused README, ASCII architecture diagrams, experimental output,
and terminal screenshots in [`../screenshots/`](../screenshots/). The
SQLite numbers come from a real local database I built (SQLite 3.51); the
PostgreSQL, MySQL, and RocksDB figures are presented as representative
runs with exact, runnable reproduction steps in each topic.

| # | Topic | Folder | What's inside |
|---|---|---|---|
| 1 | PostgreSQL vs SQLite | [`PostgreSQL_vs_SQLite/`](PostgreSQL_vs_SQLite/README.md) | Embedded library vs client/server architecture, process models, on-disk storage (4 KB single file vs 8 KB many files), MVCC storage overhead, concurrency trade-offs. Built the same 811k-row schema in both; real SQLite size + `EXPLAIN QUERY PLAN`. |
| 2 | PostgreSQL Internals | [`PostgreSQL_Internals/`](PostgreSQL_Internals/README.md) | Buffer manager (clock sweep), nbtree, MVCC heap-tuple layout, WAL, VACUUM, planner statistics. `EXPLAIN (ANALYZE, BUFFERS)` on the 4-table join; an `UPDATE` that creates 50,000 dead tuples and a `VACUUM` that reclaims them, with WAL LSN movement. |
| 3 | MySQL / InnoDB | [`MySQL_InnoDB/`](MySQL_InnoDB/README.md) | Clustered + secondary index B+trees, buffer pool with old/young LRU, redo vs undo logs (and *why two*), record/gap/next-key locks. Headline experiment: a two-session gap-lock demo that blocks a phantom `INSERT` and ends in `ERROR 1205 Lock wait timeout exceeded`, plus the READ COMMITTED control. |
| 4 | RocksDB | [`RocksDB/`](RocksDB/README.md) | LSM-tree write path, MemTable, SSTables, levels, bloom filters, compaction strategies. A custom C++ benchmark ([`bench.cpp`](RocksDB/bench.cpp), [`run.sh`](RocksDB/run.sh)) measuring write/read throughput, bloom false-positive rate, and leveled-vs-universal write/space amplification. |

## Reading order

The topics build on each other; read them in numerical order:

1. **PostgreSQL vs SQLite** sets up the root distinction — embedded
   library vs client/server engine — and the storage-layout split
   (one 4 KB-page file vs many 8 KB-page files).
2. **PostgreSQL Internals** dives into the server side: how pages move
   through the buffer manager via clock sweep, how MVCC works on top of
   the heap, why VACUUM has to exist, how WAL provides durability, and
   how the cost-based planner uses statistics.
3. **MySQL / InnoDB** introduces the third storage model — a clustered
   B+tree with in-place updates plus a separate undo log — and compares
   it head-on with Postgres's append-only heap. The "why two logs (redo
   *and* undo)" question is the headline.
4. **RocksDB** flips the assumptions entirely. The first three are all
   B-tree/heap systems that read data in roughly the shape it was
   written; RocksDB is an LSM-tree that writes fast and sorts it out
   later. The bargain and its costs (compaction, bloom-filter RAM, read
   amplification) get experimental backing from the bundled benchmark.

## Connections to the earlier labs in this course

Building toy versions of these subsystems by hand in the lab sessions is
what made reading about the real engines click. Each topic README calls
out its specific cross-references; the summary:

| Topic | Related lab work |
|---|---|
| Buffer manager / clock sweep (Topic 2, Topic 4 block cache) | [Lab 3](../lab_sessions/lab_3.txt) — Clock Sweep page replacement in C++, and [`../storage_buffer/main.cpp`](../storage_buffer/main.cpp) |
| B-tree indexes (all four topics) | [Lab 4](../lab_sessions/lab_4.txt) — Red-Black tree + full B-Tree, and the [`../index/main.cpp`](../index/main.cpp) B-tree DB |
| MVCC visibility + locking (Topic 2, Topic 3) | [Lab 6](../lab_sessions/lab_6.txt) — MVCC + Strict 2PL transaction manager with deadlock detection |
| SQLite storage internals (Topic 1) | [Lab 2](../lab_sessions/lab_2.txt) — SQLite mmap, page size, PRAGMA introspection |
| File I/O / sequential vs random writes (Topic 1, Topic 4) | [Lab 1](../lab_sessions/lab_1.txt) — File I/O syscalls traced with strace |
| WHERE/SELECT parsing & planning intuition | [Lab 5](../lab_sessions/lab_5.txt) — Shunting-Yard + minimal SQL parser, and [`../query_parser/`](../query_parser/) |

## Reproducibility

Each topic's README contains the exact SQL or shell commands needed to
recreate its experimental setup on a fresh machine. The RocksDB folder
additionally ships the benchmark source ([`RocksDB/bench.cpp`](RocksDB/bench.cpp))
and a build-and-run script ([`RocksDB/run.sh`](RocksDB/run.sh)) so the
numbers in Section 5 of that topic can be reproduced with a single
command once `brew install rocksdb` has been run.
