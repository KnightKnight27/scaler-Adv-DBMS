# Advanced DBMS — System Design Discussion

This directory contains four architectural deep-dives into real database systems,
written for the Advanced DBMS *System Design Discussion* component. The emphasis
is on **architectural reasoning and trade-offs**, not copied documentation: each
README explains *why* a system is built the way it is, *what alternatives existed*,
and *what trade-offs were accepted* — and backs every claim with output captured
from the actual engine running locally.

## Topics

| # | Topic | What it covers |
|---|-------|----------------|
| 1 | [PostgreSQL vs SQLite](./PostgreSQL_vs_SQLite/README.md) | Client-server vs embedded; process model; single-file vs cluster storage; page/index layout; single-writer (WAL) vs MVCC concurrency; scalability trade-offs |
| 2 | [PostgreSQL Internals](./PostgreSQL_Internals/README.md) | Buffer manager (clock-sweep); B-link tree; MVCC (xmin/xmax/ctid); WAL & crash recovery; why VACUUM exists; cost-based planner & `pg_statistic` |
| 3 | [MySQL / InnoDB](./MySQL_InnoDB/README.md) | Clustered index storage; secondary-index → PK lookups; buffer pool (young/old LRU); undo + redo logs; doublewrite; row/gap/next-key locking; contrast with PostgreSQL MVCC |
| 4 | [RocksDB](./RocksDB/README.md) | LSM-tree write/read paths; MemTable → SST flush; leveled vs universal compaction; write/read/space amplification (measured); Bloom filters |

## Everything is backed by real, reproducible experiments

Rather than describe behavior abstractly, each topic stands up the actual engine,
runs internal-inspection queries/benchmarks, and interprets the **real** output.
The harnesses and the captured raw output live in [`_experiments/`](./_experiments/):

| Script | Engine | Captures |
|--------|--------|----------|
| `run_sqlite.sh` | SQLite 3.51 | page size, per-btree rootpages, `EXPLAIN QUERY PLAN`, VDBE bytecode, WAL `-wal`/`-shm` sidecars, `sqlite_stat1` |
| `run_postgres.sh` | PostgreSQL 17.10 | `EXPLAIN (ANALYZE, BUFFERS)`, `pg_buffercache` clock-sweep state, `pageinspect` B-tree/heap, MVCC ctid across an UPDATE, `VACUUM VERBOSE`, WAL LSN, planner stats |
| `run_mysql.sh` | MySQL/InnoDB 9.5 | clustered vs covering index plans, `INNODB_TABLES`/`INNODB_INDEXES`, buffer-pool stats, `SHOW ENGINE INNODB STATUS` (undo/redo/LRU), gap/next-key locks via `performance_schema.data_locks`, on-disk tablespaces |
| `rocks_demo.cpp` | RocksDB 11.1 (`librocksdb`) | leveled vs universal compaction stats, write/read/space amplification, Bloom-filter effectiveness; plus `rocksdb_ldb`/`rocksdb_sst_dump` for on-disk LSM structure |

Captured output: `sqlite_experiments.txt`, `postgres_experiments.txt`,
`mysql_experiments.txt`, `rocksdb_experiments.txt`, `lsm_structure.txt`.

In addition, three **live two-session** demos capture concurrency behavior that a
single-session snapshot cannot show, plus a `db_bench` reference sheet:

| File(s) | Demonstrates |
|---------|--------------|
| `postgres_concurrency_demo.{sql,txt}` + `sqlite_concurrency_demo.{sql,txt}` | A conflicting writer blocking under PostgreSQL row locks vs. SQLite's whole-file `SQLITE_BUSY` — the single-writer-vs-MVCC contrast, live (PostgreSQL_vs_SQLite §5.8) |
| `postgres_internals_lab.{sql,txt}` | VACUUM **reclaiming** 6000 dead tuples (the contrast to the bypass case), via `pgstattuple` (PostgreSQL_Internals §5.7) |
| `innodb_locking_demo.{sql,txt}` | A gap lock physically stalling an `INSERT` (`ERROR 1205`), plus the primary-key-width / UUID-v1 measurements (MySQL_InnoDB §5.7–5.8) |
| `db_bench_commands.md` | Equivalent canonical `db_bench` invocations for the RocksDB measurements (for a source build where `db_bench` is available) |

### Reproducing

```bash
# SQLite and the harness scripts assume macOS + Homebrew tooling.
cd _experiments

./run_sqlite.sh                      # needs: sqlite3
./run_postgres.sh                    # needs: postgresql@17  (brew install postgresql@17)
./run_mysql.sh                       # needs: mysql          (brew install mysql)

# RocksDB: compile the benchmark against librocksdb (brew install rocksdb), then run
RDIR=/opt/homebrew/opt/rocksdb
clang++ -std=c++20 -O2 -w rocks_demo.cpp -I"$RDIR/include" -L"$RDIR/lib" -lrocksdb -o rocks_demo
DYLD_LIBRARY_PATH="$RDIR/lib" ./rocks_demo
```

Each script spins up a throwaway instance in `_experiments/`, writes its `*_experiments.txt`,
and tears the instance down. The committed `.txt` files are the exact runs the READMEs quote.

## A few headline findings

- **PostgreSQL planner accuracy:** estimated **36,000** rows for `year > 2010`; the executor
  returned **36,000** — exactly matching after full lab-table statistics, which is *why* it confidently chose a Bitmap Index Scan + Hash Join.
- **InnoDB covering index:** the same `author_id` lookup costs **857** when it must hop back to the
  clustered index for the row, but **401** when the secondary index already covers the query.
- **RocksDB compaction trade-off (measured):** *leveled* gave write-amp **4.63×** / space-amp **1.14×**;
  *universal* gave write-amp **3.86×** / space-amp **1.38×** — the canonical "write vs space" trade-off, live.
- **InnoDB next-key locks:** a `BETWEEN 100 AND 105 FOR UPDATE` under REPEATABLE READ took record+gap
  locks on 101–105 and a record-only lock on 100 — the mechanism that makes InnoDB's RR phantom-free.
