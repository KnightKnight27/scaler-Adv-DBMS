# Team klode MiniDB

MiniDB is a compact relational database engine for the Advanced DBMS capstone. It is intentionally small, but it implements the main internal database components: page-based storage, a buffer pool, primary-key B+ tree indexing, SQL execution, cost-inspired optimization, transactions with two-phase locking, WAL recovery, and one performance extension track.

Chosen extension track: **Track A - Performance**.

PR title: `TEAM_klode`

## Team Information

Team name: `klode`

| Full Name | Scaler Email ID | Roll Number |
| --- | --- | --- |
| Jangam Rohan | jangam.24bcs10212@sst.scaler.com | 24BCS10212 |
| Praneeth Budati | praneeth.24bcs10081@sst.scaler.com | 24BCS10081 |
| Viraj Vaibhav Bhanage | viraj.24bcs10274@sst.scaler.com | 24BCS10274 |
| Pratham Jain | pratham.24bcs10083@sst.scaler.com | 24BCS10083 |

## Project Structure

```text
MiniDB_Projects/
└── Team_klode/
    ├── CMakeLists.txt
    ├── README.md
    ├── build.ps1
    ├── benchmarks/
    │   ├── BENCHMARK_REPORT.md
    │   ├── demo.sql
    │   └── run_benchmark.ps1
    ├── docs/
    │   └── DEMO_SCRIPT.md
    ├── src/
    │   ├── main.cpp
    │   ├── minidb.cpp
    │   └── minidb.hpp
    └── tests/
        └── smoke_test.cpp
```

## Architecture

```text
                 +----------------+
                 |    SQL CLI     |
                 +-------+--------+
                         |
                         v
                 +----------------+
                 | MiniDB parser  |
                 | / dispatcher   |
                 +-------+--------+
                         |
                         v
          +--------------+---------------+
          |                              |
          v                              v
+-------------------+          +------------------+
| Optimizer / plans |          | Transaction mgr  |
| scans, joins      |          | 2PL locks        |
+---------+---------+          +--------+---------+
          |                             |
          v                             v
+-------------------+          +------------------+
| Executor          |----------| WAL              |
| rows / joins      |          | logical redo log |
+---------+---------+          +------------------+
          |
          v
+-------------------+          +------------------+
| B+ tree index     |<-------->| Page manager     |
| primary key       |          | heap pages       |
+-------------------+          +--------+---------+
                                       |
                                       v
                              +------------------+
                              | Buffer pool      |
                              | CLOCK cache      |
                              +--------+---------+
                                       |
                                       v
                              +------------------+
                              | .heap files      |
                              +------------------+
```

## Main Components

- `MiniDB`: SQL dispatcher and execution engine.
- `PageManager`: stores table records in page-based heap files.
- `BufferPool`: small CLOCK-style page cache.
- `BPlusTree`: primary-key index with search, insert, and delete.
- `LockManager`: shared/exclusive table locks for 2PL.
- `Wal`: append-only logical transaction log.

## Implemented Features

### Storage Engine

- Table data is stored in `.heap` files.
- Each table heap starts with a schema record.
- Rows include a tombstone flag so `DELETE` can mark rows deleted.
- `PageManager` packs records into 4096-byte pages.
- `BufferPool` caches pages and flushes dirty frames.
- `STORAGE_DEMO` shows heap path, page size, page count, active rows, and deleted rows.

### B+ Tree Indexing

- The first column is treated as the primary key.
- The primary index maps primary key to heap row id.
- Supports `search`, `insert`, and `remove`.
- Primary-key equality queries use index scan.
- `INDEX_DEMO users` shows sorted B+ tree leaf keys and row ids.

### SQL Execution

Supported commands:

- `CREATE TABLE users (id, name, age)`
- `INSERT INTO users VALUES (1, Ada, 31)`
- `SELECT * FROM users`
- `SELECT name FROM users WHERE id = 1`
- `SELECT name,amount FROM users JOIN orders ON users.id = orders.user_id WHERE amount > 100`
- `DELETE FROM users WHERE id = 1`
- `BEGIN`
- `COMMIT`
- `ROLLBACK`
- `INDEX_DEMO users`
- `STORAGE_DEMO`
- `LOCK_DEMO`
- `PERF_DEMO`

The SQL grammar is deliberately narrow and focused on the required capstone surface.

### Optimizer

The optimizer reports its selected plan with query output.

- Primary-key equality uses B+ tree index scan.
- Non-index predicates use table scan.
- Predicate selectivity is estimated from active rows.
- Two-table joins choose the smaller estimated input as the outer side.
- Index nested-loop join is used when the inner side join key is a primary key.

### Transactions And Concurrency

- `SELECT` takes shared table locks.
- `INSERT` and `DELETE` take exclusive table locks.
- Locks are released at `COMMIT` or `ROLLBACK`.
- Primary-key uniqueness is checked against committed rows and pending inserts in the active transaction.
- `LOCK_DEMO` creates a deterministic two-transaction wait-for cycle and reports deadlock detection.

### WAL Recovery

- WAL records `CREATE`, `BEGIN`, `INSERT`, `DELETE`, `COMMIT`, and `ROLLBACK`.
- On startup, MiniDB loads heap files and replays committed WAL operations.
- Uncommitted and rolled-back transactions are ignored.
- B+ tree indexes are rebuilt after recovery.
- The smoke test includes a WAL-only crash recovery scenario.

### Track A - Performance Extension

`PERF_DEMO` compares:

- row-at-a-time filtering
- 128-row batch filtering

Both paths evaluate the same predicate over the same synthetic rows and report timing in microseconds.

## How To Build

From this directory:

```powershell
cd C:\Users\lostdecimal27\Downloads\scaler-Adv-DBMS\MiniDB_Projects\Team_klode
.\build.ps1
```

This builds:

```text
build\minidb_cli.exe
build\minidb_smoke.exe
```

CMake files are also included:

```powershell
cmake -S . -B build
cmake --build build
```

## How To Run

Built-in demo:

```powershell
.\build\minidb_cli.exe demo_data --demo
```

Interactive mode:

```powershell
.\build\minidb_cli.exe demo_data
```

Example interactive commands:

```sql
CREATE TABLE users (id, name, age);
INSERT INTO users VALUES (1, Ada, 31);
SELECT name FROM users WHERE id = 1;
INDEX_DEMO users;
STORAGE_DEMO;
LOCK_DEMO;
PERF_DEMO;
```

## How To Test

Run the smoke test:

```powershell
.\build\minidb_smoke.exe
```

Expected output:

```text
smoke test passed
```

The smoke test covers:

- create table
- insert
- select with index scan
- join
- index demo
- storage demo
- lock/deadlock demo
- batch performance demo
- delete
- rollback
- duplicate primary-key rejection
- WAL recovery for committed and uncommitted transactions

## Benchmarks

Run:

```powershell
.\benchmarks\run_benchmark.ps1
```

The benchmark runner prints:

- command count
- total latency
- commands/sec throughput
- heap bytes
- WAL bytes
- transcript path

Benchmark details are in:

```text
benchmarks\BENCHMARK_REPORT.md
```

The repeatable command sequence is in:

```text
benchmarks\demo.sql
```

## Demo Flow

The live demonstration flow is documented in:

```text
docs\DEMO_SCRIPT.md
```

## Limitations

- SQL grammar is intentionally small.
- B+ tree delete removes leaf entries but does not rebalance underfull nodes.
- Locks are table-level, not row-level.
- WAL recovery is logical redo, not full ARIES-style physiological logging.
- Secondary index is not implemented because it is optional in the guidelines.
