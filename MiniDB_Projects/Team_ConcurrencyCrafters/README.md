# Team ConcurrencyCrafters MiniDB

This folder contains the baseline MiniDB engine for the Track B MVCC capstone. The implementation stays fully isolated inside `MiniDB_Projects/Team_ConcurrencyCrafters` and provides the baseline relational engine, strict 2PL concurrency layer, and extension hooks for WAL, recovery, and MVCC work.

## What Is Included

- Page-based heap storage with persisted table files and `RecordID(page_id, slot_id)` addressing.
- Buffer pool with LRU eviction, pin counts, dirty tracking, and debug logging.
- Integer B+ Tree indexing for primary keys and `CREATE INDEX` support on `INT` columns.
- SQL parser and executor for `CREATE TABLE`, `CREATE INDEX`, `INSERT`, `SELECT`, `DELETE`, `JOIN`, `EXPLAIN`, `BEGIN`, `COMMIT`, `ROLLBACK`, and `SET MODE`.
- Cost-based optimizer with row/page statistics, index-vs-scan selection, and join-order reasoning.
- Strict 2PL transaction manager with shared/exclusive locks, waits-for graph deadlock detection, and rollback undo hooks.
- Benchmarks, tests, and demo scripts.

## Project Layout

```text
Team_ConcurrencyCrafters/
├── README.md
├── benchmarks/
├── docs/
├── scripts/
├── src/minidb/
└── tests/
```

## Architecture

See [docs/architecture.md](docs/architecture.md) for the detailed architecture diagram, page format, buffer pool design, optimizer strategy, 2PL protocol, deadlock detection approach, and teammate extension points.

## Build, Test, Benchmark

Run these commands from `MiniDB_Projects/Team_ConcurrencyCrafters`:

```bash
python -m compileall src
python -m unittest discover -s tests
python benchmarks/run_baseline.py
```

## Demo Commands

```bash
bash scripts/demo_core.sh
bash scripts/demo_2pl_deadlock.sh
bash scripts/run_baseline_benchmarks.sh
```

## SQL Examples

```sql
CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT);
INSERT INTO users VALUES (1, 'Ada', 31);
SELECT * FROM users WHERE id = 1;
EXPLAIN SELECT * FROM users WHERE id = 1;
SELECT * FROM users JOIN orders ON users.id = orders.user_id;
BEGIN;
DELETE FROM users WHERE id = 1;
ROLLBACK;
SET MODE MVCC;
```

## Notes For Person 1

- `TransactionManager`, `LockManager`, and executor hooks already separate read/write access from storage details.
- `WALManager`, `RecoveryManager`, `MVCCManager`, and `VersionStore` are present as drop-in extension hooks.
- The current rollback path uses logical undo actions so recovery work can later swap in WAL-backed behavior without changing the parser or optimizer surfaces.

## Limitations

- This baseline intentionally stops short of full MVCC version storage and WAL recovery.
- The parser supports the capstone SQL subset rather than a full SQL grammar.
- B+ Tree secondary indexes are limited to integer columns in this version.
