# miniDB Known Limitations

This document lists deliberate and inherited limitations in the miniDB capstone engine. Each entry explains what is missing, what works instead, why it exists (scope and course alignment), and a short viva talking point where useful.

miniDB is an embedded C++17 database built from lecture/lab concepts: page-based heap files, Clock Sweep buffer pool, on-disk B+ Tree, AST-based SQL parsing, cost-based scan selection, MVCC with Strict 2PL write locking, and WAL-based crash recovery. It is **not** a production SQL server.

---

## Contents

- [SQL and parser](#sql-and-parser)
- [Schema and catalog](#schema-and-catalog)
- [Query execution](#query-execution)
- [Optimizer](#optimizer)
- [Storage and indexing](#storage-and-indexing)
- [Concurrency and MVCC (Track B)](#concurrency-and-mvcc-track-b)
- [Recovery and durability](#recovery-and-durability)
- [CLI and runtime model](#cli-and-runtime-model)
- [Testing and submission gaps](#testing-and-submission-gaps)

---

## SQL and parser

### Only SELECT, INSERT, DELETE

| | |
|---|---|
| **What it is** | The lexer/parser accepts only `SELECT`, `INSERT`, and `DELETE`. There is no `CREATE TABLE`, `UPDATE`, `DROP`, `ALTER`, or transaction control SQL (`BEGIN`/`COMMIT`). |
| **What works instead** | Tables are registered in C++ via `Catalog::RegisterTable()`. Row updates exist at the API layer (`TransactionManager::Update`) but are not exposed through SQL. |
| **Why** | Course scope requires demonstrating end-to-end SELECT/WHERE/JOIN/INSERT/DELETE with an AST—not a full SQL dialect. Lecture notes list extended parser features (NOT, ORDER BY, GROUP BY) as future work, not capstone requirements. |
| **Viva talking point** | "We implemented the minimum SQL surface the rubric asks for and kept parsing as a real AST pipeline, not string splitting. DDL would require catalog persistence and DDL executor operators we scoped out." |

### Single-column projection

| | |
|---|---|
| **What it is** | `SELECT` supports exactly one column: `SELECT name FROM users ...`. No `SELECT *`, no multi-column lists, no expressions in the projection list. |
| **What works instead** | Joins merge row values internally; the final `Project` operator returns one named column. |
| **Why** | Keeps the Volcano executor and result formatting simple while still demonstrating projection as a distinct plan node. |
| **Viva talking point** | "Projection is a separate operator in our iterator tree; extending to `SELECT col1, col2` is mostly parser and formatter work, not a storage change." |

### Limited predicate grammar

| | |
|---|---|
| **What it is** | WHERE supports `AND`, `OR`, parentheses, and comparisons `=`, `>`, `<` between a column and a literal. No `NOT`, no column-to-column predicates, no `>=`/`<=`/`<>`. String literals work in INSERT/VALUES; in WHERE, only `=` is evaluated for string columns (`>`/`<` apply to INT columns only). |
| **What works instead** | Compound boolean filters and OR (e.g. `age > 20 OR id < 5`) are tested in `query_test`. |
| **Why** | Matches lab parser progression: binary expressions on columns and integer literals first. |
| **Viva talking point** | "Our evaluator is recursive over the AST for AND/OR; adding NOT is one more operator case in `EvaluatePredicate`." |

### One INNER JOIN per query

| | |
|---|---|
| **What it is** | Syntax is `SELECT col FROM left JOIN right ON left_col = right_col [WHERE ...]`. No multi-way joins, no LEFT/RIGHT/FULL, no arbitrary join predicates beyond equality on integer columns. |
| **What works instead** | Nested-loop join over two tables with equi-join on integer columns (see `query_test` users/scores example). |
| **Why** | Nested-loop join is the join algorithm taught for the executor; multi-join optimization adds search space we did not implement. |
| **Viva talking point** | "We use textbook nested-loop join with re-initialization of the inner scan—correct and easy to explain in a demo." |

### No semicolon requirement (despite CLI prompt)

| | |
|---|---|
| **What it is** | The CLI prompt mentions SQL "ending with `;`", but the lexer does not tokenize or require semicolons. |
| **What works instead** | One statement per line works; trailing semicolons would currently cause a parse error if typed. |
| **Why** | Minor UX/doc mismatch; parser treats one line as one statement. |
| **Viva talking point** | "Known polish gap—the tokenizer ignores `;`; easy fix if we add a SEMICOLON token and strip it." |

---

## Schema and catalog

### No CREATE TABLE DDL

| | |
|---|---|
| **What it is** | Schema is not created through SQL. The default `users` table is registered in `QueryEngine`'s constructor; additional tables require calling `RegisterTable()` from C++ or test code. |
| **What works instead** | `Catalog::DefaultUsersTable()` defines `users(id INT indexed, name STRING, age INT)`. `query_test` registers a `scores` table programmatically for join demos. |
| **Why** | Catalog metadata (column types, primary key, index flags) lives in memory. Persisting DDL would need catalog pages and a DDL executor—out of capstone scope. |
| **Viva talking point** | "Table definitions are code-first for the demo; heap data can be rebuilt into the catalog on restart via `RebuildFromTableHeap`, but schema itself is not stored on disk." |

### Catalog metadata is mostly in-memory

| | |
|---|---|
| **What it is** | Row keys, index B+ Trees, and table definitions are held in `Catalog` data structures. Only heap row data and B+ Tree pages are on disk (index meta pages start at page ID 10000). |
| **What works instead** | On durable restart, `RebuildFromTableHeap()` re-tracks rows and rebuilds the primary-key index from visible heap versions. Tables must still be `RegisterTable()`'d with the same schema as before. |
| **Why** | Course focus is heap + index page layout and recovery, not a full system catalog relation. |
| **Viva talking point** | "After crash recovery we replay WAL into the heap, then rebuild catalog indexes from heap contents—similar in spirit to Postgres catalog bootstrap but simplified." |

### Fixed schema types

| | |
|---|---|
| **What it is** | Columns are `INT` or `STRING` only. Rows are tab-serialized strings in the heap; no NULL, no DECIMAL, no DATE. |
| **What works instead** | Demo and tests use integer PKs and string names. |
| **Why** | Type system complexity is intentionally minimized so MVCC headers and slot layout stay the focus. |

---

## Query execution

### No SQL UPDATE statement

| | |
|---|---|
| **What it is** | There is no `UPDATE ... SET ...` parser or executor. |
| **What works instead** | `TransactionManager::Update` implements MVCC update (new version, old version gets `xmax`). Concurrency and recovery tests use the API directly. |
| **Why** | DELETE + INSERT could simulate updates; implementing UPDATE SQL was deprioritized vs. required DELETE. |
| **Viva talking point** | "Update semantics already exist in the transaction layer; SQL UPDATE would reuse `MvccUpdate` and WAL `UPDATE_XMAX` logging." |

### Auto-commit per SQL statement

| | |
|---|---|
| **What it is** | `QueryEngine::ExecuteSql` wraps each statement in `Begin()` → execute → `Commit()`. Multi-statement transactions are not available from the CLI or SQL. |
| **What works instead** | Tests use `ExecutePlanWithTx` or `TransactionManager` directly for multi-operation transactions. |
| **Why** | Interactive shell simplicity; real transaction demos use the C++ API or unit tests. |
| **Viva talking point** | "Each CLI query is one serializable transaction—good for demos, not for banking transfer examples across two SQL lines." |

### DELETE scans all row keys

| | |
|---|---|
| **What it is** | `DeleteExecutor` iterates `Catalog::GetRowKeys(table)` (full table key list), reads each row, evaluates the predicate, then deletes matches. It does not use the B+ Tree even for `WHERE id = N`. |
| **What works instead** | Correctness for small demo tables; index is updated on delete when `primary_key == "id"`. |
| **Why** | Index-assisted delete was scoped out; SELECT already demonstrates index usage. |
| **Viva talking point** | "Delete is intentionally O(n) on catalog keys; optimizing `DELETE WHERE id = ?` would mirror our index scan path." |

### DeleteExecutor assumes primary key column is named `id`

| | |
|---|---|
| **What it is** | Index removal on delete is guarded by `table->primary_key == "id"` (hardcoded string compare), not `table->primary_key` generically. |
| **What works instead** | Default `users` and test tables using `id` or `user_id` as PK name—`scores` uses `user_id` so index cleanup on DELETE may not run for that table. |
| **Why** | Quick demo assumption; general PK name would be a one-line fix. |
| **Viva talking point** | "Known bug/shortcut—we'd replace the literal `"id"` check with the catalog's `primary_key` field." |

### Join plans always use sequential scans

| | |
|---|---|
| **What it is** | For joins, both children are always `SEQ_SCAN` plans; the optimizer does not push index scans under join legs. |
| **What works instead** | Single-table queries still get index vs. seq scan optimization. |
| **Why** | Fixed join planning keeps optimizer code paths testable without join-order × access-path combinatorics. |

---

## Optimizer

### Index vs. sequential scan only (no join order search)

| | |
|---|---|
| **What it is** | The optimizer chooses between `IndexScan` and `SeqScan` for single-table filters. Join order is **fixed**: left table from `FROM`, right table from `JOIN`—no reordering, no bushy plans. |
| **What works instead** | Cost model compares `EstimateCost(SEQ_SCAN)` vs `EstimateCost(INDEX_SCAN)` using table cardinality and heuristic selectivity (`catalog.cpp`: `= → 1/N`, `> → 0.33`, etc.). |
| **Why** | Rubric requires selectivity estimation and scan choice; full join enumeration is NP-hard and beyond course lab scope. Lecture material discusses join ordering conceptually; our implementation proves the cost-based scan decision. |
| **Viva talking point** | "We implement cost-based **access path** selection, not full System-R join order search. Join order is lexical—honest limitation we can show on a 3-table query." |

### Index scan only on indexed integer columns

| | |
|---|---|
| **What it is** | `TryExtractPredicate` requires `column op int_literal`. Index scans apply only to columns marked `indexed: true` in `TableDef` (today: primary key columns). |
| **What works instead** | `WHERE id = 3` uses index scan; `WHERE age > 20` uses seq scan (age is not indexed). |
| **Why** | Primary key index is required; secondary indexes are optional in guidelines and not implemented. |

### Heuristic selectivity (no statistics)

| | |
|---|---|
| **What it is** | No histograms or table samples—selectivity uses fixed formulas, ignoring actual data distribution. |
| **What works instead** | Cardinality comes from tracked row key counts in the catalog. |
| **Why** | Statistics collection is advanced optimizer material; heuristics suffice to demonstrate the cost comparison pattern from class. |

---

## Storage and indexing

### Primary key index only (no secondary index)

| | |
|---|---|
| **What it is** | Only columns with `indexed: true` in `RegisterTable` get a B+ Tree. In practice only PK columns (e.g. `users.id`, `scores.user_id`). No secondary index on `age` or `name`. |
| **What works instead** | B+ Tree supports search, insert, delete, and range scan—validated in `index_test` with 50k+ keys. |
| **Why** | Guidelines mark one secondary index as **optional**; team prioritized correct PK index + optimizer integration. |
| **Viva talking point** | "Secondary index is the same B+ Tree code with a different catalog registration—we chose depth on MVCC and recovery instead." |

### B+ Tree: no latch crabbing / no concurrent index latches

| | |
|---|---|
| **What it is** | Index operations run single-threaded under connection/transaction locking—no per-node latches as in production Postgres. |
| **What works instead** | Matches the design specifications and lecture simplification: index protected by connection mutex and row-level X locks for writes. |
| **Why** | Latch crabbing is not a capstone requirement; correctness over index micro-concurrency. |

### macOS vs. Linux direct I/O difference

| | |
|---|---|
| **What it is** | Linux uses `O_DIRECT`; macOS uses `F_NOCACHE` because `O_DIRECT` is unavailable. Both bypass OS page cache for database pages. |
| **What works instead** | Same 4096-byte page layout and aligned buffers on both platforms. |
| **Why** | Platform portability while honoring the lab mandate to manage our own buffer pool. |

### Embedded single-process engine

| | |
|---|---|
| **What it is** | No network protocol, no multi-process server, no shared global buffer pool across connections. |
| **What works instead** | Embedded library model (like SQLite): one process, connection-level buffer pool instance. |
| **Why** | Explicit architectural trade-off in `architecture.md` to reduce scope and focus on storage/transaction internals. |

### Page and row size practical limits

| | |
|---|---|
| **What it is** | Fixed 4 KB pages, slot array + row storage, MVCC version headers (24 bytes per version). Very wide rows or deep version chains can exhaust page space. |
| **What works instead** | Demo rows are small tab-separated strings. |
| **Why** | Standard teaching page size; variable-length rows within a page are the lab heap model. |

---

## Concurrency and MVCC (Track B)

### MVCC version garbage collection (minimal)

| | |
|---|---|
| **What it is** | On each `Commit()`, dead committed versions whose `xmax` is below the global xmin horizon are pruned from the in-memory version catalog. Physical page slots are **not** compacted — no background vacuum thread. |
| **What works instead** | Snapshot visibility (`xmin`/`xmax`, active transaction list) correctly hides dead versions from readers. `RollbackVersions` cleans up on abort. GC verified in `concurrency_test`. |
| **Why** | Full Postgres-style `VACUUM` with page compaction is beyond capstone scope; minimal horizon-based pruning satisfies Track B GC requirement without autovacuum complexity. |
| **Viva talking point** | "We compute global xmin from active snapshots at commit time and drop dead versions from the catalog — correct visibility with bounded version chains, though heap pages still carry tombstone slots until a future compaction pass." |

### Strict 2PL for writes; readers lock-free

| | |
|---|---|
| **What it is** | Reads use MVCC snapshots without S locks. Writes take exclusive row locks (`RowKey` strings) until commit/abort. Deadlock detection aborts the younger transaction (Waits-For graph + DFS). |
| **What works instead** | Demonstrated in `concurrency_test`: snapshot isolation, concurrent readers, write-write serialization, deadlock handling. |
| **Why** | Matches Track B design: MVCC for read/write concurrency + 2PL for write-write conflicts, as taught in lecture. |
| **Viva talking point** | "Readers don't block writers and writers don't block readers—that's the MVCC win. Writers still 2PL each other on the same row key." |

### Serializable isolation intent, snapshot isolation implementation

| | |
|---|---|
| **What it is** | Course rubric mentions serializable isolation; engine implements snapshot isolation via MVCC without full SSI (serializable snapshot isolation) or predicate locking. |
| **What works instead** | No write skew demo cases in tests; common MVCC anomaly classes are acceptable for capstone if explained. |
| **Why** | Lecture Track B emphasizes snapshots and version chains, not PostgreSQL SSI. |
| **Viva talking point** | "We provide transaction-order consistency for writes and snapshot reads; phantom protection for range scans would need gap locks we don't implement." |

---

## Recovery and durability

### CLI uses non-durable TransactionManager by default

| | |
|---|---|
| **What it is** | `minidb_cli` constructs `TransactionManager()` with no db path—creates a temp file under `/tmp`, **`durable_mode_ = false`**, no WAL on commit. Data survives only for the process lifetime; recovery is not exercised from the shell. |
| **What works instead** | `TransactionManager(db_path, log_path)` enables WAL, Force-WAL commit flush, and ARIES-style REDO/UNDO on restart—used in `recovery_test` and durable `query_test` restart case. |
| **Why** | Fast interactive demo without disk setup; durability is proven in automated tests. |
| **Viva talking point** | "The CLI is a thin demo shell; durability is one constructor away—we deliberately test recovery through `TransactionManager(path)` and unit tests, not the default CLI." |

### Recovery requires durable mode and log file

| | |
|---|---|
| **What it is** | Default in-memory-style path skips `LogManager` appends on commit. Crash recovery runs only when reopening a db file with an existing WAL. |
| **What works instead** | Tests cover committed survive, uncommitted undone, aborted rolled back, Force-WAL, zeroed-page REDO, and heap-only restart after log deletion (`recovery_test`). |
| **Why** | WAL/recovery is Phase 5 deliverable; wiring it into CLI was lower priority than correctness proofs. |

### Flush policy: explicit `FlushRecoveryState` for some scenarios

| | |
|---|---|
| **What it is** | Tests call `FlushRecoveryState()` to push dirty buffer pool pages and log tail before simulated crash. Not automatic on every commit in all code paths the way Postgres background writer would. |
| **What works instead** | Commit still fsyncs WAL in durable mode (Force-WAL); heap pages may lag until flush/eviction—recovery REDO handles this. |
| **Why** | Matches ARIES teaching: log records enable redo even if data pages weren't flushed. |

---

## CLI and runtime model

### Demo data re-seeded every launch

| | |
|---|---|
| **What it is** | `SeedDemoData()` inserts five `users` rows on every CLI start (IDs 1–5). Not idempotent if run twice in one process without clearing. |
| **What works instead** | Fresh demo for viva queries (`SELECT name FROM users WHERE id = 3` → Sandip). |
| **Why** | Zero-config demo for evaluators. |

### Error reporting to stderr; minimal result formatting

| | |
|---|---|
| **What it is** | Parse/execution errors print `ERROR: ...`. INSERT/DELETE print `OK`; SELECT prints one value per line (single column). No table borders, row counts, or timing. |
| **What works instead** | Sufficient for functional demo. |

### No concurrent CLI sessions

| | |
|---|---|
| **What it is** | Single-threaded REPL; no job queue or worker pool in the shell. |
| **What works instead** | Concurrency demonstrated via `concurrency_test` threads calling `TransactionManager` APIs. |

---

## Testing and submission gaps

These are not engine bugs but **deliverable gaps** relative to `context/projet-guidelines.md`:

| Gap | Status | Notes |
|-----|--------|-------|
| **README.md** at project root | Done | Architecture, setup, benchmarks, team info, limitations. |
| **Benchmark report / `benchmarks/`** | Done | Release measurements in `benchmarks/BENCHMARK_REPORT.md`; harness in `benchmarks/benchmark.cpp`. |
| **Git submission packaging** | Team responsibility | PR to course repo with `TEAM_<NAME>` title. |
| **Automated test coverage** | Partial | Six doctest binaries via CTest (storage, index, concurrency incl. MVCC GC, recovery, query)—all passing; no dedicated CLI integration test. |

### What *is* in good shape (for contrast)

- All five CTest targets pass (`storage_test`, `index_test`, `concurrency_test`, `recovery_test`, `query_test`).
- AddressSanitizer build profile available (`-DMINIDB_ASAN=ON`).
- Core rubric features implemented: heap + buffer pool, B+ Tree PK index, SQL SELECT/WHERE/JOIN/INSERT/DELETE, scan optimizer, MVCC + 2PL, WAL + recovery.

---

## Quick reference: defending scope in viva

1. **"Why so few SQL statements?"** — Capstone tests integration of taught subsystems, not SQL completeness. AST pipeline for the required DML is present.
2. **"Why no join order optimization?"** — We implement cost-based **scan** selection with selectivity estimates; join order is fixed and stated honestly in this doc.
3. **"Why no full vacuum?"** — We prune dead versions from the catalog at commit using a global xmin horizon; physical page compaction is future work.
4. **"Does recovery work?"** — Yes, in durable mode with WAL; CLI default is non-durable for convenience—show `recovery_test` or durable restart test instead.
5. **"Secondary index?"** — Optional in guidelines; PK index demonstrates B+ Tree integration with the optimizer.

---

*Last updated: June 2026 — aligned with codebase at `MiniDB_Projects/Team_DARK`.*
