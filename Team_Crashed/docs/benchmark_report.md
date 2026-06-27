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

## Write Benchmark (2PL vs MVCC concurrency control)

Chosen extension track: Track B, Concurrency.

This benchmark compares the two concurrency-control protocols the engine
implements — **strict 2PL** (the serializable baseline) and **MVCC snapshot
isolation** (the Track B extension) — on a **concurrent read/write workload**,
which is the regime where the two actually differ.

### What it measures (and why)

The earlier version measured single-threaded insert throughput with and
without a per-insert lock. With one thread there is no contention, so MVCC's
whole advantage (non-blocking reads under concurrent writers) never appeared.
The rewritten `benchmark/write_benchmark.cpp` spawns **R reader threads and W
writer threads** concurrently for a fixed window and measures throughput.

- **Readers** do `BEGIN; scan every row; COMMIT`.
  - 2PL: acquire an S lock on every row, held until COMMIT.
  - MVCC: take **no locks**; read each row's newest version visible to the
    reader's snapshot (`TransactionManager::isVisible`), so a row an in-flight
    writer is mutating is seen at its old committed value.
- **Writers** do `BEGIN; update one row; COMMIT`. Writers are partitioned
  (writer `w` touches rows `w, w+W, w+2W, …`) so no two writers collide on the
  same row concurrently — this isolates the measurement to **read/write**
  contention (the write/write conflict path is exercised by
  `tests/transaction/transaction_test.cpp`).
  - 2PL: acquire an X lock on the row, bump it in place, release at COMMIT.
  - MVCC: `recordWrite` + append a new version stamped with the txn, commit
    (first-updater-wins conflict check at commit).

Both phases drive the **real** `TransactionManager` + `LockManager` (the same
code the executor path uses). The shared row store is guarded by a single
mutex — only physical map access is serialised; whether a thread *blocks* is
decided by the CC protocol, and that blocking happens inside the lock manager,
not under the store mutex.

### Results

Configuration: `N` rows pre-populated, `R` reader threads, `W` writer threads,
1.0 s window. Numbers are from a local Windows/MinGW Release build
(g++ 15.2.0); throughput varies run-to-run with thread scheduling, so ranges
are given. Representative single run (`N=500 R=4 W=2 T=1.0`):

| Protocol | reads/s | writes/s | ops/s | deadlocks/aborts |
|---|---:|---:|---:|---:|
| 2PL (strict two-phase locking) | 3,561,313 | 248 | 3,561,561 | 0 |
| MVCC (snapshot isolation) | 11,545,239 | 551,365 | 12,096,603 | 0 |
| **MVCC speedup vs 2PL** | **3.24×** | **≈2220×** | **3.40×** | — |

Observed ranges across configs (`N=500–2000`, `R=4–8`, `W=2–4`):

| Protocol | reads/s | writes/s |
|---|---:|---:|
| 2PL | ~3.5M – 4.3M | **2 – 250** (starved) |
| MVCC | ~4M – 12M | ~280K – 570K |

### Analysis

- **Writer starvation under 2PL.** Under a read-heavy, read/write-contended
  workload, strict 2PL collapses writer throughput to a handful of operations
  per second (often single digits). Readers hold S locks on every row until
  COMMIT and the lock manager grants incoming S requests without honouring a
  waiting X, so a writer's `acquireExclusive` can wait behind an unbroken
  stream of readers and only slips through in the brief gaps. This is the
  classic 2PL read/write-blocking pathology made measurable.
- **MVCC never starves writers and never blocks readers.** Because readers
  take no locks, writers proceed concurrently; because readers read a
  consistent snapshot, a writer mid-update is simply invisible to them.
  Writer throughput stays in the hundreds of thousands per second (≈3–5
  orders of magnitude above 2PL), and reader throughput is consistently ≥
  2PL reader throughput (no per-row lock acquisition or blocking).
- **No deadlocks/aborts** in either protocol on this workload: readers/writers
  are partitioned so no wait-for cycle forms, and MVCC writers don't conflict
  (distinct rows). Deadlock detection and write/write first-updater-wins are
  exercised by the transaction test instead.

Conclusion: on contended read/write workloads, MVCC snapshot isolation
delivers materially higher and far more stable throughput than strict 2PL —
the gap is largest and most decisive for writers, which 2PL starves under
continuous readers. CSV: `benchmark_results/write.csv`.

## Join Benchmark

Workload: 100 users, 100 orders, join on `users.id = orders.user_id`, filtered with `orders.total >= 75`.

| Scale | Output rows | Elapsed ms | Rows/sec |
|---:|---:|---:|---:|
| 100 | 76 | 5.464 | 13909.22 |

Analysis: this validates the SQL join executor and qualified-column predicate handling over a modest dataset suitable for the current B+ tree/index implementation limits.

## Verification

Built with CMake + MinGW g++ 15.2.0 (C++20, Release). All seven test binaries
pass via `ctest --output-on-failure` (storage, index, parser, executor,
optimizer, transaction, recovery). The executor test now additionally verifies,
with explicit checks that run under `NDEBUG`:

- **MVCC snapshot isolation through the executor path** — an uncommitted
  insert is invisible to a concurrent reader's scan, and becomes visible
  after the writer commits.
- **2PL lock wiring** — a `TWO_PL` insert acquires an X lock on its row
  (asserted via `LockManager::holdsLock`) and releases it at commit.

The demo script runs end-to-end through the CLI:
`./build/minidb.exe demo_capstone.sql` (SHOW TABLES, CREATE, INSERT, SELECT *,
SELECT … WHERE, JOIN, DELETE → 0 rows, BEGIN/COMMIT).

## Limitations

- **MVCC is now wired into the execution path**, not just the manager API:
  `InsertExecutor` stamps each row with an MVCC `(created_txn, deleted_txn)`
  version trailer; `SeqScan`/`IndexScan` apply `isVisible` snapshot filtering;
  `Insert`/`Delete` call `recordWrite` for write/write conflict detection; and
  `SELECT` runs inside an implicit transaction so it gets a snapshot. What
  remains is **full multi-version chains for in-place updates and a background
  vacuum/GC**: deletes are still physical (tombstoned slots), so an old snapshot
  will not observe a row a later txn deleted, and dead versions are never
  reclaimed. Garbage collection is a stub, as the README anticipated.
- **2PL is wired into executors** (`TWO_PL` mode takes S/X locks on touched
  rids, released at commit). The default autocommit path is lock-free and
  snapshot-aware, so the demo and existing tests are unchanged. Safe *concurrent*
  execution of multiple executors is still gated by the non-thread-safe storage
  layer (single `BufferPool`); the concurrency-control benchmark therefore drives
  the thread-safe `TransactionManager`/`LockManager` + a synchronised store rather
  than the executor stack directly.
- The `2PL writer starvation` observed above is a real property of the current
  lock manager (S requests are granted without honouring a waiting X, i.e. no
  fair queueing). A fair lock manager would narrow — but not eliminate — the
  gap, since 2PL would still block readers on rows an active writer holds.
- WAL and recovery are implemented and tested as standalone modules; full
  executor-level WAL logging of every `INSERT`/`DELETE` remains future work.
- `CREATE INDEX` SQL is not supported; primary-key indexes are created
  automatically from `PRIMARY KEY`.
- The join benchmark intentionally uses a moderate scale to stay within the
  current index and executor stability envelope.
