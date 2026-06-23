# System Design Discussion

**Roll Number:** 24BCS10406
**Name:** Manasvi Sabbarwal

Four write-ups covering the rubric topics for the System Design Discussion
component. Each lives in its own folder with a focused README, ASCII
diagrams, real experimental output captured on my local machine, and
terminal screenshots in `../screenshots/`.

| Topic | Folder | What's inside |
|---|---|---|
| 1. PostgreSQL vs SQLite | [`PostgreSQL_vs_SQLite/`](PostgreSQL_vs_SQLite/README.md) | Architecture comparison, process model, on-disk storage, MVCC overhead, concurrency trade-offs. Experiments on a 50k-row `orders` table built in both engines. |
| 2. PostgreSQL Internals | [`PostgreSQL_Internals/`](PostgreSQL_Internals/README.md) | Buffer manager (clock sweep), nbtree, MVCC heap tuple layout, WAL, VACUUM, planner statistics. EXPLAIN ANALYZE on a 4-table 812k-row join. WAL LSN movement and `pg_stat_user_tables` dead-tuple counts. |
| 3. MySQL / InnoDB | [`MySQL_InnoDB/`](MySQL_InnoDB/README.md) | Clustered indexes, secondary indexes, buffer pool with old/young LRU, redo and undo logs, row-level and gap locks. Same 4-table schema rebuilt in InnoDB. Gap-lock blocking demo with two sessions and a measured `ERROR 1205 Lock wait timeout`. |
| 4. RocksDB | [`RocksDB/`](RocksDB/README.md) | LSM-tree write path, MemTable, SSTables, levels, bloom filters, compaction strategies. Custom C++ benchmark (`bench.cpp`, `run.sh`) measuring write throughput, read throughput, bloom-filter false-positive rate, and leveled-vs-universal write amplification. |

## Reading order

The topics build on each other. If you read them in numerical order you get:

1. SQLite vs Postgres sets up the embedded-vs-server distinction and
   the basic storage-layout difference (4 KB single-file vs 8 KB
   many-files).
2. PostgreSQL Internals dives into the Postgres side: how pages move
   through the buffer manager, how MVCC works on top of the heap,
   why VACUUM exists, how WAL gives durability, how the planner uses
   `pg_statistic`.
3. MySQL / InnoDB takes the third storage model (B-tree heap, but
   in-place updates with separate undo) and compares it head-on with
   Postgres's append-only heap. The "why two logs (redo + undo)"
   question is the headline.
4. RocksDB flips assumptions entirely. The other three use B-trees;
   RocksDB uses an LSM-tree. The bargain ("write fast, sort it out
   later") and its costs (compaction, bloom filter RAM, read
   amplification) get experimental backing.

## Connections to the earlier labs in this course

| Topic | Related lab |
|---|---|
| Buffer manager / clock sweep (Topic 2) | Lab 3 (Clock Sweep buffer cache in C++) |
| nbtree (Topic 2) | Lab 6 (B-Tree from scratch with audit + stress test) |
| MVCC visibility, snapshot isolation (Topic 2, Topic 3) | Lab 8 (MVCC + Strict 2PL transaction manager in C++) |
| Storage layout pragmas (Topic 1) | Lab 2 (SQLite vs PostgreSQL exploration), Lab 4 (SQLite hex walkthrough) |

Building toy versions of these systems in the labs made reading the
real source much easier. I have called out the cross-references in
each topic's README.

## Reproducibility

Each topic's README contains the exact SQL or shell commands needed to
recreate the experimental setup on a fresh machine. The RocksDB folder
additionally ships the C++ benchmark source (`bench.cpp`) and a
build-and-run script (`run.sh`) so the numbers in section 5 of that
topic can be reproduced with one command.
