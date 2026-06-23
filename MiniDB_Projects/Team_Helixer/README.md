# MiniDB — A Relational Database Engine from First Principles

> Advanced DBMS Capstone Project. A working, single-node relational database
> engine built from foundational components: page-based storage, a buffer pool,
> a B+Tree index, a SQL parser/optimizer/executor, strict-2PL transactions, and
> write-ahead-log crash recovery. **Extension Track C: an LSM-tree storage
> engine** benchmarked against the B+Tree.

---

## Team Information

**Team Name:** `Helixer`  &nbsp;(PR title: `TEAM_Helixer`)

| Full Name        | Scaler Email                      | Roll Number  |
|------------------|-----------------------------------|--------------|
| Piyush Goenka    | piyush.24bcs10234@sst.scaler.com  | 24BCS10234   |
| Dhruv Davda      | dhruv.24bcs10203@sst.scaler.com   | 24BCS10203   |
| Siddhanth Kapoor | siddhanth.24bcs10154@sst.scaler.com | 24BCS10154 |
| Vansh Dobhal     | vansh.24bcs10099@sst.scaler.com   | 24BCS10099   |

---

## 1. Project Overview

**Problem statement.** Modern databases hide enormous internal machinery behind
a simple `SELECT`. The goal of this project is to *build that machinery
ourselves* and understand how the layers fit together: how bytes on disk become
pages, pages become tuples, tuples become query results, and how transactions
and recovery keep all of it correct under concurrency and crashes.

**Goals.**
- Implement every required core subsystem and integrate them into one engine.
- Favour **correctness, clarity, and explainability** over feature count.
- Demonstrate real engineering trade-offs and measure them with benchmarks.

**Chosen Extension Track: C — Modern Storage (LSM-tree).** We add a
log-structured merge tree (MemTable + SSTables + compaction) as an alternative
storage engine and benchmark it against the page-based B+Tree on write
throughput, read latency, and space amplification.

---

## 2. System Architecture

```
                         ┌──────────────────────────┐
        SQL text  ─────▶ │  Parser (lexer + AST)     │   src/sql
                         └─────────────┬────────────┘
                                       ▼
                         ┌──────────────────────────┐
                         │  Cost-Based Optimizer     │   src/optimizer
                         │  (selectivity, join order,│
                         │   seq-scan vs index-scan) │
                         └─────────────┬────────────┘
                                       ▼
                         ┌──────────────────────────┐
                         │  Executor / Operators     │   src/exec
                         │  seq-scan, index-scan,    │
                         │  filter, nested-loop join,│
                         │  insert, delete           │
                         └───┬───────────────┬──────┘
            locks (2PL)      │               │   reads/writes
                ┌────────────▼───┐     ┌─────▼───────────────┐
                │ Lock Manager   │     │ Catalog (schemas,   │ src/catalog
                │ + Txn / Undo   │     │ heaps, PK indexes)  │
                │   src/txn      │     └─────┬─────────┬─────┘
                └────────────────┘           │         │
                                    B+Tree   │         │  Heap (slotted pages)
                                    src/index│         │  src/storage
                                             ▼         ▼
                                   ┌──────────────────────────┐
                                   │  Buffer Pool (LRU)        │   src/storage
                                   └─────────────┬────────────┘
                                                 ▼
                                   ┌──────────────────────────┐
                                   │  Disk Manager (pread/     │   src/storage
                                   │  pwrite, page = 4 KB)     │
                                   └──────────────────────────┘

   Write-Ahead Log  ◀── every mutation logged before commit ──  src/recovery
   (source of truth for crash recovery)

   Extension Track C:  LSM-tree (MemTable → SSTables → compaction)   src/lsm
```

**Data flow for a query.** SQL → tokens → AST → optimizer picks a plan
(access method per table + join order) → executor acquires locks, runs the
operators against the catalog's heaps/indexes via the buffer pool → results are
materialised and returned. Mutations additionally append WAL records; commit
forces the WAL to disk.

**Top-level object.** [`Database`](src/engine/database.h) owns every subsystem
and exposes `execute(sql)` plus transaction control (`begin/commit/abort`).

---

## 3. Storage Layer

- **Page format.** Fixed **4 KB** pages ([config.h](src/common/config.h)). The
  disk manager maps `page_id → offset = page_id * 4096` and uses POSIX
  `pread`/`pwrite` (chosen over `std::fstream`, which flushes its buffer on every
  read/write-mode switch and was ~1000× slower in our measurements).
- **Heap files.** Each table is a singly linked list of **slotted pages**
  ([table_heap.cpp](src/storage/table_heap.cpp)). A page has a header
  (`next_page_id`, `num_slots`, `free_offset`), a slot directory growing from the
  front, and variable-length records growing from the back. A tail-page pointer
  makes inserts **O(1)** (amortised). Deletes tombstone a slot (`length = -1`).
- **Buffer pool.** [buffer_pool.cpp](src/storage/buffer_pool.cpp) caches pages in
  a fixed set of frames, tracks **pin counts** and **dirty flags**, and evicts
  with an **LRU replacer** ([lru_replacer.h](src/storage/lru_replacer.h)), writing
  dirty victims back first. Every component (heap *and* B+Tree) reads/writes only
  through the buffer pool.

---

## 4. Indexing

- **B+Tree design.** A disk-resident B+Tree
  ([btree.cpp](src/index/btree.cpp)) mapping an **INTEGER key → RID**
  (`{page_id, slot_id}`). Each node *is* a page fetched through the buffer pool.
- **Node structure.** Order 200. Internal nodes hold sorted keys + child page
  ids; leaf nodes hold sorted keys + RIDs and are linked left-to-right
  (`next`) for range scans. Both node layouts are `static_assert`-ed to fit in a
  page.
- **Operations.** `search` (point), `insert` (with leaf **and** internal node
  splits propagated up the recorded root-to-leaf path), `remove` (lazy delete),
  and `range(low, high)` following leaf links.
- **Search path.** Descend from the root, at each internal node pick the child
  for the first key greater than the search key, until a leaf is reached, then
  scan the leaf. The primary-key index is used automatically for `WHERE pk = c`.

---

## 5. Query Execution

- **Parser.** A hand-written lexer + recursive-descent parser
  ([parser.cpp](src/sql/parser.cpp)) producing a typed AST
  ([ast.h](src/sql/ast.h)). Supported: `CREATE TABLE`, `INSERT`, `SELECT`
  (`*`/projection, `WHERE`, single `JOIN ... ON`), `DELETE`, and
  `BEGIN/COMMIT/ABORT`. `WHERE` is a conjunction (`AND`) of comparisons.
- **Plan generation.** The optimizer turns a `SELECT` into a `QueryPlan`: an
  access method per table and (for joins) a join order.
- **Operator execution.** A simple **materialised** model
  ([executor.cpp](src/exec/executor.cpp)): `scan_table` produces rows via
  sequential scan or index point lookup and applies residual filters;
  joins use a **nested-loop** with the smaller relation as the outer; `project`
  selects the output columns. Column references resolve by qualified
  (`table.col`) or unqualified name, so joins disambiguate correctly.

---

## 6. Optimizer

[optimizer.cpp](src/optimizer/optimizer.cpp) is cost-based:
- **Selectivity estimation.** Equality on a key ≈ `1/N`; ranges ≈ `1/3`
  (textbook defaults), using a live `row_count` statistic kept per table.
- **Access-path choice.** For `WHERE pk = const` it chooses **INDEX_POINT**
  (cost ≈ tree height); otherwise **SEQ_SCAN** (cost ≈ N rows). You can see the
  decision in every result's `[plan: ...]` annotation.
- **Join ordering.** Each side is costed; the relation with the smaller estimated
  cardinality becomes the **outer** loop, minimising inner rescans.

---

## 7. Transactions & Concurrency

- **Locking strategy.** [lock_manager.cpp](src/txn/lock_manager.cpp) implements
  **strict two-phase locking** with shared/exclusive locks on named resources
  (table-level for queries). Locks are acquired during execution and **all
  released together at commit/abort** — guaranteeing serializability and
  recoverability.
- **Isolation guarantees.** Coarse table-level strict 2PL yields **serializable**
  isolation: reads take `S`, writes take `X`, and the usual compatibility matrix
  applies (`S/S` compatible; anything with `X` conflicts).
- **Deadlock handling.** A **wait-for graph** is maintained; before a transaction
  blocks, edges to lock holders are added and **cycle detection** (DFS) runs. On a
  cycle, the requesting transaction is chosen as the victim and aborted
  (`TransactionAbortException`), then rolled back via its undo log.

---

## 8. Recovery

- **WAL design.** [log_manager.cpp](src/recovery/log_manager.cpp) is an
  append-only log. Record types: `BEGIN, COMMIT, ABORT, CREATE_TABLE, INSERT,
  DELETE, CHECKPOINT`. The **WAL rule** holds: changes are logged before commit,
  and `COMMIT` **force-flushes** the log (the durability point).
- **Log records.** `INSERT`/`DELETE` carry the table name and the full
  schema-serialized tuple; `CREATE_TABLE` carries the schema. This makes redo
  *logical* and idempotent.
- **Crash recovery procedure.** The WAL is the source of truth. On startup the
  data file is treated as scratch and **rebuilt by replaying the log**: committed
  transactions are **redone**; uncommitted ones are simply never replayed (that
  *is* the undo). In-session `ABORT` rolls back immediately using the
  transaction's undo log. Verified by [test_recovery](tests/test_recovery.cpp)
  and `demos/demo_recovery.sh` (which `kill -9`s the process mid-run).

---

## 9. Extension Track C — LSM-Tree Storage

- **Motivation.** A B+Tree turns random-key inserts into random page writes. An
  LSM-tree converts writes into in-memory updates plus **sequential** flushes,
  trading some read/space cost for far higher write throughput — the right
  engine for write-heavy workloads.
- **Design** ([lsm_tree.cpp](src/lsm/lsm_tree.cpp)):
  - **MemTable** — an in-memory ordered map of recent writes (deletes write a
    tombstone). Writes are O(log n) with no random disk I/O.
  - **SSTable** — when the MemTable fills, it is flushed as an immutable sorted
    run written **sequentially** to disk; reads binary-search a cached copy.
  - **Compaction** — merges all SSTables into one run, dropping overwritten keys
    and tombstones (reclaims space, speeds reads).
  - A read checks the MemTable, then SSTables newest→oldest; first hit wins.
- **Results** (see §10): **~2.8× higher write throughput** and **~0.5× the disk
  footprint** of the B+Tree on a 200k random-key workload.

---

## 10. Benchmarks

**Experimental setup.** Apple Silicon (macOS), `clang -O2`, single-threaded.
Reproduce with `./build/bench_lsm_vs_btree 200000` and `./build/bench_query
100000`. Raw output is saved in [benchmarks/results.txt](benchmarks/results.txt).

**Results.**

| Storage engine | Write throughput | Point-read | On-disk size (200k keys) |
|----------------|------------------|------------|--------------------------|
| B+Tree         | ~0.47 M ins/s    | ~0.52 M lookups/s | 5.92 MB |
| **LSM-tree**   | **~1.3 M put/s** | ~2.4 M lookups/s  | **2.89 MB** (1 SSTable after compaction) |

| Access path (100k-row table) | Latency per query |
|------------------------------|-------------------|
| `INDEX_POINT` (`WHERE id=?`) | **~26 µs** |
| `SEQ_SCAN` (`WHERE k=?`)     | ~45 ms |

**Analysis.**
- **LSM vs B+Tree writes:** the LSM wins (~2.8×) because writes hit the MemTable
  and flush sequentially, while the B+Tree pays for random page reads/writes and
  node splits. The LSM also stores less because compact records beat half-full
  B+Tree pages.
- **Reads:** both are fast; the LSM's cached, fully-sorted runs make point reads
  very cheap at this scale, while the B+Tree's strength (bounded I/O, range
  scans) shows on larger-than-memory data.
- **Index vs sequential scan:** the cost-based optimizer's preference for the
  index is justified — point lookups are **~1700× faster** than full scans here,
  exactly the gap the optimizer's cost model predicts.

---

## 11. Limitations

- **Types:** only `INT` and `VARCHAR`; no `NULL`s or constraints beyond a single
  primary key. The B+Tree indexes integer keys only.
- **SQL subset:** no `UPDATE`, `GROUP BY`/aggregates, `ORDER BY`, sub-queries, or
  multi-way joins (one `JOIN` per query); `WHERE` is `AND`-only.
- **Concurrency granularity:** table-level strict 2PL (correct & serializable but
  coarse); no lock upgrade (writers take `X` up front). Single-process.
- **B+Tree deletes** are lazy (no node merge/rebalance); **heap deletes** tombstone
  without space reclamation (compaction is future work).
- **Recovery** replays the full WAL on every startup (no log truncation /
  checkpoint-based replay yet); the index is rebuilt during replay rather than
  persisted.
- **LSM** keeps each SSTable's sorted run cached in memory (no block-level paging)
  and uses single-level (full) compaction.

**Future improvements:** `UPDATE`/aggregates, row-level locking with MVCC,
checkpoint-bounded WAL replay with page-LSN redo, leveled LSM compaction, and a
proper buffer-managed SSTable reader.

---

## 12. How to Run

**Dependencies:** a C++17 compiler (clang/gcc) and CMake ≥ 3.15. No third-party
libraries.

```bash
# Build
cd MiniDB_Projects/Team_Helixer
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4

# Interactive shell (creates <name>.db / <name>.wal)
./build/minidb mydb
# ...or run the scripted feature demo:
./build/minidb demo < demos/demo_basic.sql

# Demos
./demos/demo_transactions.sh    # BEGIN/ABORT rollback + COMMIT persist
./demos/demo_recovery.sh        # crash (kill -9) then WAL recovery

# Tests (all should print [OK])
ctest --test-dir build --output-on-failure
#   or run individually: ./build/test_buffer_pool ./build/test_btree
#   ./build/test_table ./build/test_engine ./build/test_recovery
#   ./build/test_concurrency ./build/test_lsm

# Benchmarks
./build/bench_lsm_vs_btree 200000
./build/bench_query 100000
```

**Example session.**
```sql
CREATE TABLE users (id INT, name VARCHAR, age INT, PRIMARY KEY (id))
INSERT INTO users VALUES (1, 'alice', 30)
SELECT name FROM users WHERE id = 1     -- uses the primary-key index
```

---

### Repository layout
```
Team_Helixer/
├── README.md
├── CMakeLists.txt
├── src/        engine source (storage, index, sql, exec, optimizer, txn, recovery, lsm, engine)
├── tests/      unit + integration tests
├── benchmarks/ performance experiments + results.txt
├── demos/      runnable demonstration scripts
└── docs/       design notes
```
