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

MiniDB is a single-process C++ relational database engine implementing the Advanced DBMS capstone stack: page-based storage, disk manager, LRU buffer pool, slotted heap files, B+ tree indexing, SQL parsing, logical and physical planning, optimizer rewrites, Volcano-style executors, transaction management, and WAL/recovery modules. Both concurrency-control protocols — strict 2PL and MVCC snapshot isolation — are wired into the execution path, not just the manager API.

## Track

**Track B - Concurrency**

The project implements **both** concurrency-control protocols end-to-end and
compares them on a contended read/write workload:

- **Strict 2PL** is the serializable baseline. In `TWO_PL` mode, `SeqScan` /
  `IndexScan` acquire an S lock on each row they read and `Insert` / `Delete`
  acquire an X lock on each row they write; all locks are held until commit
  (`releaseAll`). The lock manager does **real blocking** with a **wait-for-graph
  deadlock detector** (replacing the earlier report-`DEADLOCK`-on-any-conflict
  stub).
- **MVCC snapshot isolation** is the Track B extension. Each row carries an
  `(created_txn, deleted_txn)` version trailer; `InsertExecutor` stamps new
  rows, `SeqScan` / `IndexScan` filter rows through `TransactionManager::isVisible`
  against the reader's snapshot, and `SELECT` runs inside an implicit transaction
  so it observes a consistent snapshot. Writers call `recordWrite` for
  first-updater-wins write/write conflict detection at commit.

Both are selectable via `IsoMode { AUTOCOMMIT, TWO_PL, MVCC }` on the executor
context; the default autocommit path stays lock-free and snapshot-aware, so the
demo and existing tests are unchanged.

## Guideline Coverage

- Page manager, slotted pages, heap files, disk manager, and buffer pool.
- B+ tree search, insert, delete, metadata-backed primary-key index creation, and range-scan path.
- SQL parser and execution for `CREATE TABLE`, `DROP TABLE`, `INSERT`, `SELECT`, `WHERE`, `JOIN`, `DELETE`, `BEGIN`, `COMMIT`, and `ROLLBACK`.
- `SHOW TABLES;` and interactive `.tables` for catalog inspection.
- Cost-based optimizer with selectivity estimates, scan choice, predicate pushdown, and join ordering.
- Transaction manager with a **strict 2PL lock manager (real blocking + wait-for-graph deadlock detection)** and **MVCC snapshot isolation**, **both wired into the executor path**; deadlock/snapshot-isolation tests.
- WAL append/read and startup recovery flow.
- Benchmark report and runnable read/write/join benchmark programs.

## Build And Run

From `Team_Crashed/`:

```powershell
g++ -std=c++20 -Iinclude src/cli/main.cpp (Get-ChildItem -Recurse src -Filter *.cpp | Where-Object { $_.FullName -notmatch '\\src\\cli\\' }).FullName -o miniDB.exe
.\miniDB.exe demo_capstone.sql
```

The submitted folder also includes a prebuilt `miniDB.exe`.

With CMake (MinGW, C++20, Release) the full test suite builds and runs under
`ctest --output-on-failure`; the concurrency benchmark builds as
`bench_write_benchmark`.

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

Latest local verification before PR update (CMake + MinGW g++ 15.2.0, C++20 Release):

- `miniDB.exe demo_capstone.sql`: passed (SHOW TABLES, CREATE, INSERT, SELECT *,
  SELECT … WHERE, JOIN, DELETE → 0 rows, BEGIN/COMMIT).
- `.tables` interactive catalog command: passed.
- All seven `ctest` test binaries pass: storage, index, parser, executor,
  optimizer, transaction, recovery.
- The executor test additionally verifies, with explicit checks that run under
  `NDEBUG`:
  - **MVCC snapshot isolation through the executor path** — an uncommitted
    insert is invisible to a concurrent reader's scan, and becomes visible
    after the writer commits.
  - **2PL lock wiring** — a `TWO_PL` insert acquires an X lock on its row
    (asserted via `LockManager::holdsLock`) and releases it at commit.
- Read, write, and join benchmark executables compiled and ran; the write
  benchmark is the concurrent 2PL-vs-MVCC comparison below.

## Benchmark Snapshot

Read benchmark at 10,000 rows and 10,000 point queries:

| Mode | Queries | Seq scans | Index scans | Elapsed ms | QPS |
|---|---:|---:|---:|---:|---:|
| Table scan on non-indexed payload | 10,000 | 10,000 | 0 | 16969.329 | 589.30 |
| Primary-key index scan | 10,000 | 0 | 10,000 | 48.050 | 208118.28 |

Observed index speedup: about **353x**.

Write benchmark — **concurrent 2PL vs MVCC** (`N=500` rows, `R=4` reader threads,
`W=2` writer threads, 1.0 s window; readers scan every row, writers update a
partitioned row, both driving the real `TransactionManager` + `LockManager`):

| Protocol | reads/s | writes/s | ops/s | deadlocks/aborts |
|---|---:|---:|---:|---:|
| 2PL (strict two-phase locking) | 3,716,708 | 247 | 3,716,955 | 0 |
| MVCC (snapshot isolation) | 11,790,361 | 592,143 | 12,382,504 | 0 |
| **MVCC vs 2PL** | **3.17x** | **≈2400x** | **3.33x** | — |

Analysis: under a read-heavy, read/write-contended workload, strict 2PL starves
writers to a handful of operations per second — readers hold S locks on every
row until COMMIT and the (unfair) lock manager grants incoming S requests
without honouring a waiting X, so a writer's `acquireExclusive` only slips
through in the brief gaps. MVCC readers take no locks and read a consistent
snapshot, so writers proceed concurrently and reader throughput is higher too.
Writer throughput is roughly three-to-four orders of magnitude above 2PL.
Throughput varies run-to-run with thread scheduling; CSV: `benchmark_results/write.csv`.

Join benchmark:

| Scale | Output rows | Elapsed ms | Rows/sec |
|---:|---:|---:|---:|
| 100 | 76 | 5.464 | 13909.22 |

## Known Limitations

- **MVCC is wired into the execution path** (row versioning, `isVisible`
  snapshot filtering, implicit snapshot transaction for `SELECT`), but
  **full multi-version chains for in-place updates and a background vacuum/GC
  are not implemented**: deletes are still physical (tombstoned slots), so an
  old snapshot will not observe a row a later txn deleted, and dead versions
  are never reclaimed. Garbage collection is a stub, as the README anticipated.
- **2PL is wired into executors** (`TWO_PL` mode takes S/X locks on touched
  rids, released at commit). Safe *concurrent* execution of multiple executors
  is still gated by the non-thread-safe storage layer (single `BufferPool`);
  the concurrency-control benchmark therefore drives the thread-safe
  `TransactionManager` / `LockManager` + a synchronised store rather than the
  executor stack directly. The lock manager is not fair (no queue honouring a
  waiting X), so 2PL writers starve under continuous readers — a real,
  measurable property reflected in the benchmark above.
- Full executor-level WAL logging of every `INSERT` / `DELETE` is not yet
  integrated (WAL and recovery are implemented and tested as standalone modules).
- `CREATE INDEX` SQL is not supported; primary-key indexes are created
  automatically from `PRIMARY KEY`.
- The join benchmark uses a moderate scale to stay within the current index and
  executor stability envelope.