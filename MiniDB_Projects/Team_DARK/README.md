# MiniDB

An embedded relational database engine built for the Advanced DBMS capstone. MiniDB integrates page-based heap storage, Clock Sweep buffer pool, on-disk B+ Tree indexing, AST-based SQL parsing, cost-based scan optimization, MVCC with Strict 2PL write locking, and WAL-based crash recovery.

---

## Team Information

**Team Name:** DARK

| Name | Roll Number | Scaler Email |
|------|-------------|--------------|
| Mrinmay Dev Sarma | 24BCS10280 | mrinmay.24bcs10280@sst.scaler.com |
| Aryan Sirohi| 24BCS10283 | aryan.24bcs10283@sst.scaler.com |
| Parth Taneja | 24BCS10230|parth.24bcs10230@sst.scaler.com|
| Krisshank Agarwal | 24BCS10218 |krisshank.24bcs10218@sst.scaler.com|

---

## 1. Project Overview

**Problem statement.** Build a working database engine from foundational components taught in Advanced DBMS — storage, indexing, query processing, transactions, and recovery — and demonstrate system integration through a functioning MiniDB.

**Goals.**

- Page-based heap files with a custom buffer pool bypassing OS page cache
- B+ Tree primary-key index integrated with a cost-based optimizer
- SQL SELECT / INSERT / DELETE with AST parsing and Volcano-style execution
- Serializable write behavior via Strict 2PL; read-write concurrency via MVCC (Track B)
- WAL-based crash recovery with REDO/UNDO

**Chosen extension track:** **Track B — MVCC Concurrency Control.** Readers use snapshot isolation without acquiring locks; writers serialize via exclusive row locks. Version garbage collection prunes dead committed versions at commit time.

---

## 2. System Architecture

```
              ┌────────────────────────────────────────┐
              │          SQL Query String              │
              └───────────────────┬────────────────────┘
                                  ▼
              ┌────────────────────────────────────────┐
              │          Parser (AST Gen)              │
              └───────────────────┬────────────────────┘
                                  ▼
              ┌────────────────────────────────────────┐
              │       Cost-Based Optimizer             │
              └───────────────────┬────────────────────┘
                                  ▼
              ┌────────────────────────────────────────┐
              │          Execution Engine              │
              └─────────┬───────────────────┬──────────┘
                        ▼                   ▼
              ┌──────────────────┐ ┌──────────────────┐
              │TransactionManager│ │  LockManager     │
              │ (MVCC Snapshots) │ │  (Strict 2PL)    │
              └─────────┬────────┘ └─────────┬────────┘
                        ▼                   ▼
              ┌────────────────────────────────────────┐
              │         Buffer Pool (Clock Sweep)        │
              └───────────────────┬────────────────────┘
                                  ▼
              ┌────────────────────────────────────────┐
              │    Disk Storage (Heap + B+ Tree + WAL)   │
              └────────────────────────────────────────┘
```

**Major modules:** `storage/` (pages, disk I/O, buffer pool, heap), `index/` (B+ Tree), `parser/` + `execution/` (SQL → plan → operators), `concurrency/` (MVCC, 2PL, deadlock detection), `recovery/` (WAL, ARIES-style REDO/UNDO).

**Data flow (read):** SQL → AST → optimizer picks IndexScan or SeqScan → executor fetches heap page → TransactionManager applies MVCC visibility on the version chain → result returned.

**Data flow (write):** Executor acquires X lock on row key → new version appended with `xmin = TxID`, old version gets `xmax = TxID` → page marked dirty → WAL record appended → commit flushes WAL and runs version GC.

---

## 3. Storage Layer

**Page format (4096 bytes).** 24-byte header (checksum, slot count, free-space pointer, LSN), slot array growing downward, row storage growing upward. Each row version has a 24-byte MVCC header (`xmin`, `xmax`, `prev_version_tid`) followed by tab-separated key/value payload.

**Heap files.** `TableHeap` manages variable-length rows via slot arrays on numbered pages. `DiskManager` uses direct I/O (`O_DIRECT` on Linux, `F_NOCACHE` on macOS) with page-aligned buffers.

**Buffer pool.** Per-connection `BufferPoolManager` with Clock Sweep eviction (`usage_count`, `pin_count`). Pinned pages are never evicted; dirty pages are written back on eviction or explicit flush.

---

## 4. Indexing

**B+ Tree design.** On-disk nodes stored in 4096-byte pages as `[[gnu::packed]]` structs. Child pointers are page IDs, not memory addresses. Degree parameter *t* bounds keys per node (*t*−1 to 2*t*−1).

**Node structure.** Internal nodes hold separator keys and child page IDs; leaf nodes hold `(key → RecordId)` pairs with sibling links for range scans.

**Search path.** Root → internal nodes via binary search on keys → leaf → `RecordId` (page_id, slot_id) returned in O(log *t* N) page accesses. Keys/values copied with `std::memcpy` to avoid strict-aliasing issues on ARM64.

---

## 5. Query Execution

**Parser.** Lexer + recursive-descent parser produce an AST for `SELECT` (single column, WHERE, one INNER JOIN), `INSERT`, and `DELETE`. Supports `AND`/`OR`/parentheses and `=`, `>`, `<` predicates.

**Query plan generation.** Optimizer builds a tree of `PlanNode`s: `SEQ_SCAN`, `INDEX_SCAN`, `FILTER`, `NESTED_LOOP_JOIN`, `PROJECT`, `INSERT`, `DELETE`.

**Operator execution.** Volcano iterator interface (`Init`, `Next`, `Close`) in `executor.cpp`. Joins use nested-loop with equi-join on integer columns. Each CLI statement auto-commits in its own transaction.

---

## 6. Optimizer

**Cost estimation.** `EstimateCost(SEQ_SCAN) = cardinality × page_read_cost`; `EstimateCost(INDEX_SCAN) = log(cardinality) × page_read_cost`.

**Selectivity estimation.** Heuristic formulas: equality → 1/N, range `>` → 0.33, `OR` → sum capped at 1.0. No histograms — cardinality from catalog row-key counts.

**Join ordering.** Fixed lexical order (left `FROM` table, right `JOIN` table). Both join legs use sequential scans. Single-table queries get cost-based IndexScan vs SeqScan selection when the filter column is indexed.

---

## 7. Transactions & Concurrency

**Locking strategy.** Strict 2PL for writes: exclusive row locks on `RowKey` strings held until commit/abort. Readers use MVCC snapshots — no shared locks.

**Isolation guarantees.** Snapshot isolation via transaction IDs and active-transaction lists captured at `Begin()`. Visibility rules check `xmin`/`xmax` against the reader's snapshot.

**Deadlock handling.** Waits-For graph built from lock wait queues; DFS cycle detection aborts the younger transaction.

**Version GC.** On each `Commit()`, a global xmin horizon (minimum snapshot ID among active transactions) is computed. Dead versions — committed deletes/updates whose `xmax` is below the horizon — are pruned from the in-memory version catalog. Physical page space is not compacted (minimal GC scope).

---

## 8. Recovery

**WAL design.** Append-only log file (`*.log`) with record types: `BEGIN`, `COMMIT`, `ABORT`, `INSERT_ROW`, `UPDATE_XMAX`. Force-WAL on commit (`fsync` before returning).

**Log records.** Physiological records store page ID, slot index, row offset/length, key, MVCC headers, and value bytes for redo; `UPDATE_XMAX` records old/new xmax for idempotent replay.

**Crash recovery.** ARIES-style REDO (replay all records, skip pages with `page_lsn ≥ record_lsn`) then UNDO (roll back loser transactions). `RecoveryManager` restores transaction table and advances `next_xid`.

---

## 9. Extension Track (MVCC — Track B)

**Motivation.** Standard 2PL blocks readers during writes. MVCC lets readers see consistent snapshots without locking, improving read-write concurrency for embedded workloads.

**Design.** Each write creates a new heap version; old versions remain until GC. Snapshot visibility uses `xmin`/`xmax` and the active-transaction set at transaction start. Write-write conflicts still serialize via X locks.

**Results.** Concurrent read benchmark: 8 threads × 5,000 reads = 40,000 snapshot reads with no reader-side locking (~7,260 reads/s on ARM64 macOS). Version GC keeps version chains bounded after commits complete — verified in `concurrency_test`.

---

## 10. Benchmarks

See [`benchmarks/BENCHMARK_REPORT.md`](benchmarks/BENCHMARK_REPORT.md) for full analysis.

| Benchmark | Median result (Release, 3 runs) |
|-----------|--------------------------------|
| 10k heap inserts | 3.85 ms |
| 50k B+ Tree lookups | 71.91 ms |
| 40k MVCC concurrent reads (8 threads) | 5.51 s (~7,260 reads/s) |
| Index scan vs seq scan (5k rows) | 1.55 ms vs 5.06 ms (**3.27×**) |

Reproduce: `./benchmarks/run_benchmarks.sh 3`

---

## 11. Limitations

Honest scope boundaries (see also [`known-limitations.md`](known-limitations.md)):

- **SQL surface:** SELECT / INSERT / DELETE only; no DDL, UPDATE SQL, or transaction control statements
- **Single-column projection;** one INNER JOIN per query; fixed join order (no reordering)
- **Heuristic selectivity;** no statistics or secondary indexes
- **DELETE** scans all catalog row keys (no index-assisted delete)
- **MVCC GC:** prunes version catalog entries only — no physical page compaction or background vacuum thread
- **CLI default is non-durable** (temp file, no WAL); durability proven via `TransactionManager(db_path)` in tests
- **Snapshot isolation,** not full serializable snapshot isolation (SSI)
- **Embedded single-process** model — no network server or shared global buffer pool

---

## 12. How to Run

**Dependencies:** CMake ≥ 3.14, C++17 compiler (Apple Clang or GCC 8+), Git (doctest fetched at configure time).

**Build:**

```bash
cd MiniDB_Projects/Team_DARK
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

**Run tests:**

```bash
cd build && ctest --output-on-failure
```

**Interactive CLI:**

```bash
./build/minidb_cli
```

Example commands (no trailing semicolon required):

```
SELECT name FROM users WHERE id = 3
SELECT name FROM users WHERE age > 20 OR id < 5
INSERT INTO users (id, name, age) VALUES (10, Alice, 25)
DELETE FROM users WHERE id = 10
```

**AddressSanitizer build (optional):**

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DMINIDB_ASAN=ON
cmake --build build-asan -j
cd build-asan && ctest --output-on-failure
```

**Benchmarks:**

```bash
./benchmarks/run_benchmarks.sh 3
```

For durability/recovery demos and detailed test procedures, see the recovery and concurrency test suites.

---

## Project Structure

```
MiniDB_Projects/Team_DARK/
├── src/
│   ├── storage/       # DiskManager, BufferPoolManager, TableHeap, Page
│   ├── index/         # B+ Tree
│   ├── parser/        # Lexer, AST parser
│   ├── execution/     # Catalog, Optimizer, Executor, CLI engine
│   ├── concurrency/   # TransactionManager, LockManager, MVCC GC
│   └── recovery/      # LogManager, RecoveryManager
├── tests/             # doctest binaries (storage, index, concurrency, recovery, query)
├── benchmarks/        # benchmark.cpp, run_benchmarks.sh, BENCHMARK_REPORT.md
├── architecture.md
└── known-limitations.md
```
