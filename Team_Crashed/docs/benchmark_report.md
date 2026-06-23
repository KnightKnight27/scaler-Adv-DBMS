# MiniDB Benchmark Report

## Setup

- Build target: C++20 MinGW (`g++`) on Windows.
- Data path: local filesystem under `data/`.
- Benchmark programs: `benchmark/read_benchmark.cpp`, `benchmark/write_benchmark.cpp`, `benchmark/join_benchmark.cpp`.
- Checked-in result data: `benchmark_results/read.csv`, `benchmark_results/write.csv`, `benchmark_results/join.csv`.

## Read Benchmark

Workload: 10,000 point lookup queries over 10,000 rows.

| Mode | Queries | Seq scans | Index scans | Elapsed ms | QPS |
|---|---:|---:|---:|---:|---:|
| Table scan on non-indexed `payload` | 10,000 | 10,000 | 0 | 16969.329 | 589.30 |
| Primary-key index scan on `id` | 10,000 | 0 | 10,000 | 48.050 | 208118.28 |

Analysis: for point lookup on a primary key, the B+ tree index path is much faster than scanning the heap file. The measured local speedup was about 353x. The scan phase intentionally uses a non-indexed column so it exercises the table-scan path, while the index phase uses the primary-key index automatically created by `CREATE TABLE ... PRIMARY KEY`.

## Write Benchmark

Chosen extension track: Track B, Concurrency.

Workload: 1,000 inserts into a table with an integer primary key.

| Mode | Inserts | Elapsed ms |
|---|---:|---:|
| Strict-2PL style path with exclusive lock per insert | 1,000 | 11835.784 |
| MVCC-style no-lock write path | 1,000 | 11683.268 |

Analysis: this single-threaded write workload is dominated by SQL execution and page/index maintenance, so the lock manager overhead is small. The result is still useful as a baseline comparison because the two modes execute the same insert path with and without explicit 2PL lock acquisition.

## Join Benchmark

Workload: 100 users, 100 orders, join on `users.id = orders.user_id`, filtered with `orders.total >= 75`.

| Scale | Output rows | Elapsed ms | Rows/sec |
|---:|---:|---:|---:|
| 100 | 76 | 5.464 | 13909.22 |

Analysis: this validates the SQL join executor and qualified-column predicate handling over a modest dataset suitable for the current B+ tree/index implementation limits.

## Verification

The following direct binaries were compiled and run with `g++` because `cmake` was not on PATH in the local environment:

- storage round-trip
- index create/search/delete
- parser statements
- executor create/insert/select/delete
- transaction locking and write conflict
- WAL round-trip and startup recovery
- `miniDB.exe demo_capstone.sql`

All passed.

## Limitations

- The concurrency extension implements snapshot metadata and write-write conflict detection, but full row-version chains and garbage collection are not implemented.
- WAL and recovery are implemented and tested as standalone modules; full executor-level WAL logging of every `INSERT`/`DELETE` remains future integration work.
- `CREATE INDEX` SQL is not supported; primary-key indexes are created automatically from `PRIMARY KEY`.
- The join benchmark intentionally uses a moderate scale to stay within the current index and executor stability envelope.
