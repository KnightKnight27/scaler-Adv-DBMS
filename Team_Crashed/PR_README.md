# TEAM_Crashed - MiniDB Submission

## Team Information

Team Name: **Crashed**

| Full Name | Roll Number | Scaler Email ID |
|---|---|---|
| Harshit Tiwari | 24BCS10277 | harshit.24bcs10277@sst.scaler.com |
| Swaim Sahay | 24BCS10335 | swaim.24bcs10335@sst.scaler.com |
| Archit Kulkarni | 24BCS10194 | archit.24bcs10194@sst.scaler.com |
| Piyush Kumar Mahato | 24BCS10233 | piyush.24bcs10233@sst.scaler.com |

## Submission Directory

`Team_Crashed/`

## Summary

MiniDB is a single-process C++ relational database engine implementing the Advanced DBMS capstone stack: page-based storage, disk manager, LRU buffer pool, slotted heap files, B+ tree indexing, SQL parsing, logical and physical planning, optimizer rewrites, Volcano-style executors, transaction management, and WAL/recovery modules.

## Track

**Track B - Concurrency**

The project keeps strict 2PL as the serializable baseline and adds an MVCC-style extension path with snapshot metadata and write-write conflict detection. The lock manager remains available for baseline comparison and deadlock demonstrations.

## Guideline Coverage

- Page manager, slotted pages, heap files, disk manager, and buffer pool.
- B+ tree search, insert, delete, metadata-backed primary-key index creation, and range-scan path.
- SQL parser and execution for `CREATE TABLE`, `DROP TABLE`, `INSERT`, `SELECT`, `WHERE`, `JOIN`, `DELETE`, `BEGIN`, `COMMIT`, and `ROLLBACK`.
- `SHOW TABLES;` and interactive `.tables` for catalog inspection.
- Cost-based optimizer with selectivity estimates, scan choice, predicate pushdown, and join ordering.
- Transaction manager with strict 2PL lock manager, conflict/deadlock smoke tests, and MVCC-style write-write conflict detection.
- WAL append/read and startup recovery flow.
- Benchmark report and runnable read/write/join benchmark programs.

## Build And Run

From `Team_Crashed/`:

```powershell
g++ -std=c++20 -Iinclude src/cli/main.cpp (Get-ChildItem -Recurse src -Filter *.cpp | Where-Object { $_.FullName -notmatch '\\src\\cli\\' }).FullName -o miniDB.exe
.\miniDB.exe demo_capstone.sql
```

The submitted folder also includes a prebuilt `miniDB.exe`.

## Demo Queries

`demo_capstone.sql` demonstrates:

- `SHOW TABLES;`
- `CREATE TABLE`
- `INSERT`
- `SELECT *`
- `SELECT ... WHERE`
- `JOIN ... ON ... WHERE`
- `DELETE`
- `BEGIN` / `COMMIT`

## Verification

Latest local verification before PR update:

- `miniDB.exe demo_capstone.sql`: passed.
- `.tables` interactive catalog command: passed.
- Storage test: passed.
- Index create/search/delete test: passed.
- Parser statement test: passed.
- Executor create/insert/select/delete test: passed.
- Transaction locking and write-conflict test: passed.
- WAL round-trip and startup recovery test: passed.
- Read, write, and join benchmark executables compiled and ran.

## Benchmark Snapshot

Read benchmark at 10,000 rows and 10,000 point queries:

| Mode | Queries | Seq scans | Index scans | Elapsed ms | QPS |
|---|---:|---:|---:|---:|---:|
| Table scan on non-indexed payload | 10,000 | 10,000 | 0 | 16969.329 | 589.30 |
| Primary-key index scan | 10,000 | 0 | 10,000 | 48.050 | 208118.28 |

Observed index speedup: about **353x**.

Write benchmark:

| Mode | Inserts | Elapsed ms |
|---|---:|---:|
| Strict-2PL style path with exclusive lock per insert | 1,000 | 11835.784 |
| MVCC-style no-lock write path | 1,000 | 11683.268 |

Join benchmark:

| Scale | Output rows | Elapsed ms | Rows/sec |
|---:|---:|---:|---:|
| 100 | 76 | 5.464 | 13909.22 |

## Known Limitations

- Full executor-level WAL logging for every `INSERT`/`DELETE` is not yet integrated.
- MVCC has snapshot/write-conflict metadata, but not full physical row-version chains or garbage collection.
- `CREATE INDEX` SQL is not supported; primary-key indexes are created automatically from `PRIMARY KEY`.
- Join benchmark uses a moderate scale to stay within the current index and executor stability envelope.
