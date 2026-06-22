# System Design Docs — Advanced DBMS

**Author:** Mohammed Abdur Rahman · Roll `24BCS10130` · `mohammed.24bcs10130@sst.scaler.com`

Architectural study of four real database systems. Each write-up follows the
required structure — Problem Background, Architecture Overview, Internal Design,
Design Trade-Offs, Experiments/Observations, Key Learnings — and every claim is
backed by an experiment I ran locally (PostgreSQL 17.9, SQLite 3.51, MySQL 9.6,
Python 3.12).

| Topic | Focus |
|---|---|
| [PostgreSQL vs SQLite](PostgreSQL_vs_SQLite/) | Client–server vs embedded; process model, storage, concurrency |
| [PostgreSQL Internals](PostgreSQL_Internals/) | Buffer manager, B-tree, MVCC (xmin/xmax), WAL, cost-based planner |
| [MySQL / InnoDB](MySQL_InnoDB/) | Clustered index, undo/redo logs, next-key locking, isolation |
| [RocksDB](RocksDB/) | LSM-tree, SSTables, compaction, Bloom filters, the three amplifications |

Reproducible scripts and captured output: [`experiments/`](experiments/).
