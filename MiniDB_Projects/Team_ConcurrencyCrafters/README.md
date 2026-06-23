# Team_ConcurrencyCrafters - MiniDB Track B

MiniDB is a relational database engine built from scratch with page-based
storage, buffer pool, B+ Tree indexing, SQL execution, optimizer, strict 2PL
concurrency, WAL recovery, and a Track B MVCC extension benchmarked against
2PL. The project preserves the original baseline engine and extends it through
the existing storage, transaction, optimizer, and executor hooks so that both
the required locking baseline and the MVCC extension can be demonstrated from
the same codebase.

## Team

| Member | Roll Number | Scaler Email | Contribution Area |
| --- | --- | --- | --- |
| Lekhana Dinesh | 24bcs10108 | lekhana.24bcs10108@sst.scaler.com | Baseline MiniDB, storage, page manager, buffer pool, B+ Tree, parser/executor, optimizer, strict 2PL, deadlock detection, baseline tests/benchmarks/docs |
| Aparna Singha | 24bcs10353 | aparna.24bcs10353@sst.scaler.com | WAL, recovery, MVCC version store, snapshot visibility, MVCC SQL integration, 2PL-vs-MVCC benchmarks, recovery/MVCC tests/docs |

## Capstone Requirement Mapping

| Requirement | Status | Evidence |
| --- | --- | --- |
| Page-based heap files | Complete | `src/minidb/pages.py`, `src/minidb/storage.py`, `tests/test_minidb.py` |
| Page manager | Complete | `src/minidb/pages.py::PageManager`, `tests/test_minidb.py` |
| Buffer pool | Complete | `src/minidb/buffer.py`, `tests/test_minidb.py`, `python -m unittest discover -s tests` |
| B+ Tree primary index | Complete | `src/minidb/index.py`, `src/minidb/engine.py`, `python scripts/demo_core.py` |
| SELECT / WHERE / JOIN / INSERT / DELETE | Complete | `src/minidb/parser.py`, `src/minidb/engine.py`, `tests/test_submission_audit.py`, `python scripts/demo_core.py` |
| Cost-based optimizer | Complete | `src/minidb/optimizer.py`, `EXPLAIN SELECT * FROM accounts WHERE id = 1;`, `tests/test_minidb.py` |
| Serializable strict 2PL | Complete | `src/minidb/transactions.py`, `tests/test_minidb.py`, `python scripts/demo_2pl_deadlock.py` |
| Deadlock handling | Complete | `src/minidb/transactions.py`, `tests/test_track_b.py`, `python scripts/demo_2pl_deadlock.py` |
| WAL recovery | Complete | `src/minidb/transactions.py`, `tests/test_track_b.py`, `python scripts/demo_recovery.py` |
| Track B MVCC extension | Complete | `SET MODE MVCC;`, `tests/test_track_b.py`, `python scripts/demo_mvcc_snapshot.py` |
| Benchmarks | Complete | `python benchmarks/run_baseline.py`, `python benchmarks/run_concurrency_comparison.py`, `benchmarks/benchmark_results.md` |
| Documentation and demo scripts | Complete | `docs/architecture.md`, `docs/BENCHMARK_REPORT.md`, `scripts/` |

## System Architecture

```text
SQL CLI / Demo Scripts
        |
        v
SQL Parser
        |
        v
Cost-Based Optimizer
        |
        v
Query Executor
        |
        v
StorageEngine Interface
        |
        v
Heap File + B+ Tree + Buffer Pool + Page Manager
        |
        v
Disk Files

Transaction Layer:
2PL Mode  -> Lock Manager   -> Deadlock Detector
MVCC Mode -> Version Store  -> Snapshot Visibility
Recovery  -> WAL Manager    -> Recovery Manager
```

The architecture is intentionally layered: SQL parsing and planning remain
independent from the physical storage layout, while concurrency control and
recovery are attached through transaction hooks rather than by replacing the
baseline engine. The deeper design discussion is in
[docs/architecture.md](docs/architecture.md).

## Module Overview

| Module | Files | Responsibility |
| --- | --- | --- |
| Page format and disk pages | `src/minidb/pages.py` | Defines fixed-size pages, slot directory layout, page allocation, page reads, and page writes |
| Buffer pool | `src/minidb/buffer.py` | Caches pages, tracks pin counts, manages dirty pages, and performs LRU eviction |
| Heap storage | `src/minidb/storage.py` | Inserts, deletes, scans, table rebuild for recovery, and WAL-before-page flush integration |
| Indexing | `src/minidb/index.py` | B+ Tree nodes and key-to-`RecordID` lookup path |
| Parsing | `src/minidb/parser.py` | Parses the supported SQL subset into typed statements |
| Optimization | `src/minidb/optimizer.py` | Chooses `INDEX_SCAN`, `TABLE_SCAN`, or `NESTED_LOOP_JOIN` using lightweight statistics |
| Execution and integration | `src/minidb/engine.py` | Coordinates parser, optimizer, executor, catalog, transaction manager, WAL, recovery, and MVCC visibility |
| Transactions, WAL, recovery, MVCC | `src/minidb/transactions.py` | Lock manager, deadlock detection, transaction lifecycle, WAL manager, recovery manager, version store, and MVCC manager |
| Baseline verification | `tests/test_minidb.py` | Storage, buffer pool, index, parser, executor, optimizer, and 2PL baseline checks |
| Track B verification | `tests/test_track_b.py` | WAL, recovery, MVCC visibility, rollback discard, tombstones, and mode coexistence checks |
| Submission audit | `tests/test_submission_audit.py` | End-to-end capstone command flow and benchmark smoke validation |
| Benchmarks | `benchmarks/run_baseline.py`, `benchmarks/run_concurrency_comparison.py` | Baseline microbenchmarks and 2PL-vs-MVCC concurrency comparison |
| Demos | `scripts/demo_*.py`, `scripts/*.sh` | Reproducible capstone demo flows for core SQL, deadlocks, recovery, MVCC, and benchmarks |

## Storage Layer

MiniDB stores tables as page-based heap files under the team-local runtime
directory. Each heap page is `4096` bytes and uses a slotted-page layout: a
small header stores metadata such as slot count and free-space boundary, the
slot directory stores fixed-width metadata for each record, and the
variable-length JSON row payloads are packed from the end of the page backward.
This keeps row insertion simple while preserving stable physical addresses
inside a page.

Records are identified physically as `RecordID(page_id, slot_id)`. Page ids are
file offsets in page units, and slot ids identify entries in the slot
directory. The persistence model is intentionally direct: pages are allocated,
read, and written through `PageManager`, while `HeapStorageEngine` handles
logical table operations on top of those pages.

The buffer pool sits between the executor and disk pages. Dirty pages are
retained in memory until unpinned and flushed, and `storage.py` calls the WAL
manager before every page flush so the write-ahead logging invariant stays
intact. This gives the engine a clear durability rule even though recovery is
logical rather than ARIES-style page recovery.

## B+ Tree Indexing

MiniDB uses an integer-key B+ Tree as the primary index path for point lookups.
The tree maps logical keys to `RecordID` anchors and supports search, insert,
and delete operations in `src/minidb/index.py`. Primary-key indexes are created
automatically, and additional integer indexes can be added with `CREATE INDEX`.

For point predicates such as `SELECT * FROM accounts WHERE id = 1;`, the
optimizer chooses `INDEX_SCAN` when an index exists on the predicate column.
The executor then uses the B+ Tree to locate the logical key anchor first and
applies either strict 2PL visibility or MVCC snapshot visibility afterward.
This is important for Track B: MVCC changes which version is visible, but it
does not bypass the baseline index structure.

Example:

```sql
EXPLAIN SELECT * FROM accounts WHERE id = 1;
```

The resulting plan shows `INDEX_SCAN` and includes the active transaction
strategy, making the index path directly visible during the demo.

## Query Execution And Optimizer

The supported SQL subset is intentionally scoped to the capstone:

- `CREATE TABLE`
- `CREATE INDEX`
- `INSERT`
- `SELECT`
- `SELECT COUNT(*)`
- `DELETE`
- `JOIN`
- `EXPLAIN`
- `BEGIN`
- `COMMIT`
- `ROLLBACK`
- `SET MODE`

`parser.py` converts these statements into typed statement objects that the
engine routes through the optimizer and executor. Single-table reads use
`TABLE_SCAN`, `INDEX_SCAN`, or `INDEX_RANGE_SCAN`; equi-joins use a nested-loop
join; simple `AND` predicates are supported across equality and integer range
filters; `COUNT(*)` returns a deterministic count row; deletes use predicate
matching plus transactional hooks; and `EXPLAIN` returns the chosen physical
operator and supporting details rather than executing the plan.

The optimizer is lightweight but explicit. It tracks row count and page count
statistics in the catalog, estimates equality selectivity as `1 / row_count`
for indexed primary-key predicates, falls back to `0.1` for unknown predicates,
chooses index access when it is available, and orders joins with the smaller
estimated input first. These are educational approximations, but they are
visible, testable, and consistent with the current engine behavior.

## Transaction Baseline: Strict 2PL

Strict 2PL remains the baseline concurrency mode and the default isolation
story of the engine. A transaction begins with `BEGIN;` or an implicit
statement transaction, acquires shared locks for reads and exclusive locks for
writes, holds those locks until `COMMIT;` or `ROLLBACK;`, and then releases
them at transaction end. This produces a serializable lock-based execution
model for the supported operations.

The lock manager enforces standard compatibility:

| Held \ Requested | Shared | Exclusive |
| --- | --- | --- |
| Shared | Allowed | Conflict |
| Exclusive | Conflict | Conflict |

When a lock cannot be granted immediately, the transaction waits and the
waits-for graph is updated. If the graph contains a cycle, the engine aborts
the youngest transaction in that cycle as a deterministic victim. The behavior
is easy to reproduce with `python scripts/demo_2pl_deadlock.py`, which is also
backed by automated deadlock coverage in `tests/test_track_b.py`.

## Recovery

Durability is implemented through an append-only WAL file. The WAL manager
records `BEGIN`, `INSERT`, `DELETE`, `COMMIT`, and `ABORT` events together with
enough logical information to reconstruct committed state during restart.
`storage.py` explicitly invokes WAL flushing before data-page flushing so the
log reaches durable storage first.

Recovery is intentionally logical, not a full ARIES implementation. On startup,
the recovery manager scans the WAL, identifies committed versus incomplete
transactions, rebuilds committed version history, and materializes the current
logical table state from those committed operations. This preserves committed
work and drops or ignores uncommitted work, which is demonstrated by
`python scripts/demo_recovery.py` and verified in `tests/test_track_b.py`.

Explicit limitation: this is logical WAL reconstruction rather than
page-LSN-driven ARIES redo/undo recovery. That trade-off keeps the design
understandable for the capstone while still satisfying the required
crash-recovery behavior.

## Track B - MVCC Extension

MVCC was chosen because it contrasts cleanly with the strict-locking baseline
and makes the concurrency trade-off visible in both deterministic demos and
benchmark workloads. Instead of replacing the baseline storage/index design,
the engine adds a second transaction mode selected with `SET MODE MVCC;`.

Each logical key can own a version chain in the persisted version store. A
transaction receives a snapshot timestamp at `BEGIN`, and each successful
commit receives a monotonically increasing commit timestamp. Visibility is then
resolved with a small set of rules:

- a transaction sees its own staged writes immediately
- committed versions are visible only when their begin timestamp is at or before the reader snapshot
- uncommitted versions from other transactions are hidden
- deletes become tombstone versions rather than immediate history erasure
- rollback discards uncommitted staged versions
- readers do not take shared locks in MVCC mode

The read path still starts with the B+ Tree for keyed access. The index finds
the logical row anchor, and the MVCC layer decides which version is visible for
the current snapshot. This keeps the baseline access-path story intact while
changing only the visibility policy.

| Aspect | Strict 2PL | MVCC |
| --- | --- | --- |
| Reads | Shared locks, may block behind writers | Snapshot visibility, non-blocking reads |
| Writes | Exclusive locks | Staged versions with write-conflict protection |
| Rollback | Undo/discard writes and release locks | Discard uncommitted versions and release locks |
| Strength | Simple serializable baseline | Better read concurrency under contention |
| Cost | Blocking under hot-key contention | Version storage overhead and cleanup debt |

## Additional Strengthening Features

- `COUNT(*)` now works for full-table reads, filtered reads, and join queries, returning a deterministic count row such as `[{\"count\": 2}]`.
- Simple `AND` predicate support now covers conjunctions of equality and integer range filters such as `id = 1 AND balance = 1000`.
- Integer primary-key range predicates such as `id >= 1 AND id <= 10` now use `INDEX_RANGE_SCAN` instead of falling back to a full table scan.
- `engine.vacuum()` provides a manual MVCC cleanup pass that removes obsolete committed versions while preserving any version still needed by an active snapshot.
- The recovery report now distinguishes committed replay from uncommitted discard with `redone_operations` and `undone_or_ignored_operations`.

## Benchmarks

### Objective

The benchmark suite compares the required strict 2PL baseline against the Track
B MVCC extension under workloads that stress point reads, mixed read/write
traffic, and hot-key contention. The goal is not to report machine-independent
OLTP numbers, but to show the relative concurrency behavior of both modes
within the same engine.

### Environment Note

The checked-in benchmark artifacts were generated from the repository scripts in
a local single-process Python runtime. Absolute throughput depends on the host
machine, but the relative difference between 2PL reader blocking and MVCC
snapshot reads is the intended signal.

### Workloads

| Workload | Description |
| --- | --- |
| `read_heavy` | 90% reads, 10% writes across three warm keys |
| `mixed` | 70% reads, 30% writes with moderate key reuse |
| `hot_key_contention` | Readers and writers collide on the same key |
| `write_heavy` | Balanced read/write workload with sustained version creation |

### Metrics

- throughput
- average read latency
- p95 read latency
- average write latency
- blocked-read count
- total wait time
- committed and aborted transactions
- MVCC version count and version-store bytes

### Result Highlights

The latest generated results in `benchmarks/benchmark_results.md` show:

- `read_heavy`: `2PL` reached `232.17 tx/s`, while `MVCC` reached `340.05 tx/s`
- `hot_key_contention`: `2PL` blocked `80` reads, while `MVCC` blocked `0`
- `mixed`: `MVCC` reduced average read latency from `14.487 ms` to `4.626 ms`
- `write_heavy`: `MVCC` still improved throughput from `116.97 tx/s` to `210.61 tx/s`

For the full result tables and baseline microbenchmarks, see:

- [benchmarks/benchmark_results.md](benchmarks/benchmark_results.md)
- [benchmarks/baseline_benchmark_results.md](benchmarks/baseline_benchmark_results.md)
- [docs/BENCHMARK_REPORT.md](docs/BENCHMARK_REPORT.md)

### Interpretation

The concurrency comparison shows the expected design trade-off. Strict 2PL
provides a simpler serializable baseline but blocks readers behind writers on
hot keys. MVCC removes those shared-lock waits for readers, dramatically
reducing blocked reads and improving read-heavy throughput, while paying for
that benefit with a larger version store and future cleanup requirements.

## How To Run

Run the commands below from `MiniDB_Projects/Team_ConcurrencyCrafters`.

Windows-friendly commands:

```bash
python -m compileall src tests benchmarks scripts
python -m unittest discover -s tests
python benchmarks/run_baseline.py
python benchmarks/run_concurrency_comparison.py
python scripts/demo_core.py
python scripts/demo_2pl_deadlock.py
python scripts/demo_mvcc_snapshot.py
python scripts/demo_recovery.py
```

Unix-like wrapper scripts:

```bash
bash scripts/demo_core.sh
bash scripts/demo_2pl_deadlock.sh
bash scripts/demo_mvcc_snapshot.sh
bash scripts/demo_recovery.sh
bash scripts/run_baseline_benchmarks.sh
bash scripts/run_concurrency_benchmarks.sh
```

## Demo Flow

- Core SQL flow: create tables, insert rows, run `SELECT`, `DELETE`, `ROLLBACK`, and transaction mode changes with `python scripts/demo_core.py`
- Additional SQL breadth: demonstrate `COUNT(*)`, `AND` predicates, and `INDEX_RANGE_SCAN` in the core demo
- EXPLAIN index scan: show `INDEX_SCAN` for `SELECT * FROM accounts WHERE id = 1;`
- JOIN query: demonstrate `accounts JOIN transactions` in the core demo
- 2PL deadlock demo: reproduce waits-for cycle detection with `python scripts/demo_2pl_deadlock.py`
- WAL recovery demo: prove committed state survives restart with `python scripts/demo_recovery.py`
- MVCC snapshot demo: show non-blocking snapshot reads and own-write visibility with `python scripts/demo_mvcc_snapshot.py`
- Manual MVCC cleanup: call `engine.vacuum()` from the core demo after an MVCC update
- 2PL vs MVCC benchmark: compare contention behavior with `python benchmarks/run_concurrency_comparison.py`

## Testing

Latest verification uses:

```bash
python -m unittest discover -s tests
```

Current suite size: `51` tests.

Coverage categories:

- storage
- buffer pool
- B+ Tree
- parser
- executor
- optimizer
- strict 2PL
- deadlock detection
- WAL
- recovery
- MVCC
- COUNT, `AND` predicates, and range-index scans
- manual MVCC vacuum
- benchmark smoke testing

## Limitations

- The SQL subset is intentionally scoped to the capstone and does not aim to be a full SQL implementation.
- Recovery is logical REDO/UNDO-style WAL reconstruction, not full ARIES redo/undo with page LSNs.
- MVCC cleanup is a manual `engine.vacuum()` pass, not a background vacuum or full garbage collector.
- Historical-version handling for non-primary-key secondary indexes is simplified.
- The engine is single-process and educational rather than production-grade.

## Future Improvements

- ARIES-style page recovery with page LSNs and finer-grained redo/undo
- background MVCC vacuum and fuller version garbage collection
- richer secondary-index support
- broader SQL grammar and more join forms
- statistics histograms and a more realistic cost model
- configurable durability and fsync behavior

## Contribution Split

Lekhana Dinesh:

- storage, page manager, and buffer pool
- B+ Tree implementation and index integration
- parser and query executor
- optimizer
- strict 2PL baseline
- deadlock detection
- baseline benchmarks, tests, and documentation foundation

Aparna Singha:

- WAL manager
- recovery manager
- MVCC manager and version store
- snapshot visibility rules
- MVCC transaction mode integration
- recovery and MVCC tests
- concurrency benchmarks and Track B documentation

Shared:

- end-to-end integration
- demo flow and scripts
- final README and benchmark report
- benchmark interpretation
- final verification and submission review
