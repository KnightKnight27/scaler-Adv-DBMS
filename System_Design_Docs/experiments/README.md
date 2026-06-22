# Experiments

All numbers quoted in the four topic write-ups come from these scripts, run locally.
Raw captured output lives in [`output/`](output/).

| Script | Engine | Produces |
|---|---|---|
| [`pg_experiments.sql`](pg_experiments.sql) | PostgreSQL 17.9 | query plans, MVCC version chain, buffer cache, B-tree metapage, WAL volume |
| [`sqlite_experiments.sql`](sqlite_experiments.sql) | SQLite 3.51 | page storage, query plans, clustered rowid, WAL mode |
| [`mysql_experiments.sql`](mysql_experiments.sql) | MySQL 9.6 (InnoDB) | clustered vs secondary index, isolation, next-key locks |
| [`lsm_sim.py`](lsm_sim.py) | Python 3.12 | LSM write / read / space amplification under compaction |

## How to reproduce

```bash
# PostgreSQL
createdb adbms_exp && psql -d adbms_exp -f pg_experiments.sql

# SQLite
sqlite3 shop.db < sqlite_experiments.sql

# MySQL
mysql -uroot < mysql_experiments.sql

# LSM simulator
python3 lsm_sim.py
```

Dataset is the same shape across the SQL engines: `customers` (10k rows) and
`orders` (200k rows) with secondary indexes, so plans and sizes are comparable.

## Captured output

- [`output/pg_plans.txt`](output/pg_plans.txt), [`output/pg_mvcc.txt`](output/pg_mvcc.txt), [`output/pg_internals.txt`](output/pg_internals.txt), [`output/pg_wal.txt`](output/pg_wal.txt)
- [`output/sqlite.txt`](output/sqlite.txt)
- [`output/mysql.txt`](output/mysql.txt), [`output/mysql_locks.txt`](output/mysql_locks.txt)
- [`output/rocksdb_lsm.txt`](output/rocksdb_lsm.txt)
