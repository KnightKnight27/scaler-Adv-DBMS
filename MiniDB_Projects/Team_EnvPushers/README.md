# MiniDB — A Relational Database Engine from Scratch (C++17)

> Advanced DBMS Capstone — a working, single-node relational database that
> integrates storage, indexing, query processing, a cost-based optimizer,
> transactions, recovery, and an **LSM-tree** extension track.

---

## Team Information

**Team Name:** `TEAM_<EnvPushers>`

| Full Name | Roll Number | Scaler Email |
|-----------|-------------|--------------|
| _Aastik Nayyar_ | SCALER24BCS10101 | _aastik.24bcs10101@sst.scaler.com_ |
| _Nanakjot Singh Chahal_ | SCALER24BCS10132 | _nanakjot.24bcs10132@sst.scaler.com_ |


---

## 1. Project Overview

**Problem.** Real databases are built from layered components — a byte-level
storage engine, indexes, a query processor, a concurrency-control layer, and a
recovery subsystem — that must cooperate correctly. The goal of this capstone is
to build a small but *coherent* relational engine where all of these layers are
implemented from first principles and actually work together end-to-end.

**Goals.**
- Page-based storage with a buffer pool, heap files, and a B+ Tree index.
- A SQL front-end (lexer → parser → AST) and a Volcano-style execution engine.
- A **cost-based optimizer** that chooses access paths and join order.
- **Serializable** transactions via **strict two-phase locking** with deadlock
  detection.
- **Crash recovery** via a **Write-Ahead Log**.
- One extension track, implemented and benchmarked.

**Chosen Extension Track: C — Modern Storage (LSM-Tree).**
We chose the LSM-tree because its mental model — *MemTable → SSTable →
Compaction* — is the easiest to explain and reason about, it is self-contained
(a clean key/value store), and it produces a crisp, demonstrable result: higher
**durable write throughput** than the B+ Tree store, at the cost of read/space
amplification that compaction bounds.

Everything is **pure C++17 with no third-party dependencies**, built with a
plain `Makefile`.

---

## 2. System Architecture

```
                         ┌─────────────────────────────┐
   SQL text  ───────────▶│  Lexer → Parser → AST        │   (src/sql)
                         └──────────────┬──────────────┘
                                        ▼
                         ┌─────────────────────────────┐
                         │  Cost-Based Optimizer        │   (src/optimizer)
                         │  • selectivity estimation    │
                         │  • SeqScan vs IndexScan       │
                         │  • greedy join ordering       │
                         └──────────────┬──────────────┘
                                        ▼  physical plan (operator tree)
                         ┌─────────────────────────────┐
                         │  Volcano Executor            │   (src/execution)
                         │  SeqScan IndexScan Filter     │
                         │  NestedLoopJoin Aggregate ... │
                         └───────┬──────────────┬───────┘
              reads/writes       │              │  index lookups
                                 ▼              ▼
   ┌──────────────┐   ┌────────────────┐  ┌──────────────────┐
   │ Transaction  │   │  Heap Files    │  │  B+ Tree (PK)    │  (src/index)
   │ Manager+2PL  │   │  (slotted pgs) │  │  search/insert/   │
   │ Lock Manager │   └───────┬────────┘  │  delete/range     │
   │ (src/txn)    │           ▼           └──────────────────┘
   └──────┬───────┘   ┌────────────────┐
          │           │  Buffer Pool   │  LRU + pin/unpin + dirty  (src/storage)
          │ WAL hook  └───────┬────────┘
          ▼                   ▼
   ┌──────────────┐   ┌────────────────┐
   │  Write-Ahead │   │  Disk Manager  │  page-granular file I/O
   │  Log (WAL)   │   │  minidb.db     │
   │ (src/recovery)│  └────────────────┘
   └──────────────┘

   Extension (Track C):  src/lsm  — MemTable + SSTable + Compaction
                         (benchmarked against the B+ Tree store)
```

**Data flow for a query.** `Database::execute()` parses the SQL into an AST,
acquires the necessary table locks for the current transaction, asks the
optimizer for a physical plan, then pulls rows through the operator tree. Scans
read tuples from heap files via the buffer pool; index scans walk the B+ Tree.
Mutations append a WAL record *before* the dirty page can be flushed, and
register an in-memory undo action for rollback.

**Major modules** (`src/`): `common` (value/RID types), `storage` (page, disk
manager, buffer pool, heap file), `index` (B+ tree), `catalog` (schema, tuple
serialization, table metadata), `sql` (lexer, parser, AST), `execution`
(operators), `optimizer`, `transaction` (locks, txns), `recovery` (WAL),
`lsm` (extension), and `database.{hpp,cpp}` which wires it all together.

---

## 3. Storage Layer

**Page format (`src/storage/page.hpp`).** Fixed **4 KiB** pages using the
classic **slotted-page** layout. A header holds `page_id`, `next_page_id` (heap
chain link), `num_slots`, and a `free_ptr`. The slot directory grows forward
from the header while records grow backward from the end of the page; the gap
between them is free space. Each slot stores `(offset, length)`; a length of `0`
is a **tombstone** so deletes keep RIDs stable.

**Heap files (`src/storage/heap_file.hpp`).** Each table is a singly linked list
of slotted pages. A record is addressed by an **RID = (page_id, slot_id)** that
is stable for the record's lifetime, which is what lets the index point at it.
Inserts scan the chain for free space and append a new page when full.

**Buffer pool (`src/storage/buffer_pool.hpp`).** A fixed set of in-memory frames
caching disk pages, with **pin/unpin**, **dirty tracking**, and **LRU eviction**
of unpinned frames. A `before_flush` hook enforces the **write-ahead rule** (the
log is flushed before any data page is written back). The disk manager
(`disk_manager.hpp`) is the only component performing raw page I/O, mapping
`page_id → byte offset` in `minidb.db`.

---

## 4. Indexing

**B+ Tree (`src/index/bplus_tree.hpp`).** An order-64 B+ Tree mapping a key
`Value → RID`, used as the **primary-key index**.

- **Node structure.** Internal nodes hold up to `ORDER-1` separator keys and
  `ORDER` child pointers; leaves hold up to `ORDER-1` `(key, RID)` pairs and a
  `next` pointer linking leaves left-to-right.
- **Search path.** A lookup binary-searches separators at each level to descend
  to a leaf, then binary-searches the leaf — `O(log N)`.
- **Insert.** Descends to the target leaf, inserts in sorted order, and **splits
  bottom-up**, copying-up (leaf) or pushing-up (internal) a separator when a
  node overflows; the root splits to grow a new level.
- **Delete.** Removes from the leaf and repairs underflow by **borrowing from**
  or **merging with** a sibling, propagating toward the root.
- **Range scans** walk the linked leaves, powering `WHERE pk BETWEEN ...`.

The index is **in-memory and rebuilt from the heap on open**. This is a
deliberate trade-off (see *Limitations*) that keeps the index algorithms front
and center and easy to defend, while remaining fully functional.

---

## 5. Query Execution

**Parser (`src/sql/`).** A hand-written lexer and **recursive-descent parser**
produce an AST. Supported SQL: `CREATE TABLE`, `INSERT` (multi-row, named or
positional columns), `SELECT` with `WHERE`, `JOIN ... ON`, `GROUP BY`,
aggregates (`COUNT/SUM/MIN/MAX/AVG`) and `ORDER BY`, `DELETE`, `UPDATE`, and
`BEGIN/COMMIT/ABORT`. `WHERE` supports `AND`/`OR` and the six comparison
operators with proper precedence.

**Plan generation.** The optimizer turns a `SELECT` AST into a tree of physical
operators (next section).

**Operator execution (`src/execution/`).** A **Volcano / iterator** model: every
operator implements `open() / next() / close()` and exposes an output schema, so
operators compose into a pipeline. Implemented operators: `SeqScan`,
`IndexScan`, `Filter`, `NestedLoopJoin` (left-deep), `Projection`, `Aggregate`
(with `GROUP BY`), and `Sort`. Predicates are evaluated against a row using the
operator's qualified output schema.

---

## 6. Optimizer

**Cost-based optimizer (`src/optimizer/optimizer.hpp`).**

- **Selectivity estimation.** Equality on a unique primary key → `1/row_count`;
  equality on a non-key column → `0.1`; range predicates → `0.33`. These combine
  multiplicatively to estimate each table's post-filter cardinality.
- **Access-path choice.** If `WHERE` contains a usable predicate on the primary
  key, the optimizer emits an **IndexScan** over the matching key range;
  otherwise a **SeqScan**. (`EXPLAIN` is printed in the REPL with `.explain on`
  and in the demo.)
- **Join ordering.** A greedy heuristic scans the **most selective table first**
  (smallest estimated cardinality), building a **left-deep** nested-loop tree.
- **Predicate placement.** `WHERE`/`ON` conjuncts referencing one table are
  **pushed down** as filters above that table's scan; multi-table conjuncts
  become join conditions applied once both inputs are available.

---

## 7. Transactions & Concurrency

**Strict two-phase locking (`src/transaction/`).** MiniDB locks at **table
granularity**: `SELECT` takes a **SHARED** lock, and `INSERT/UPDATE/DELETE` take
an **EXCLUSIVE** lock. Locks are held until commit/abort (the "strict" in strict
2PL), which provides **serializable** isolation.

- **Isolation guarantee.** Because no lock is released before commit, every
  schedule is conflict-serializable; readers and writers never observe
  uncommitted state.
- **Deadlock handling.** Before a request blocks, the lock manager builds the
  global **wait-for graph** and runs **cycle detection**. If waiting would close
  a cycle, the requesting transaction is chosen as the **deadlock victim** and a
  `DeadlockError` is thrown; the engine aborts it (rolling back via undo actions)
  and the other transaction proceeds.
- **Rollback.** Each mutation registers an in-memory undo closure; `ABORT` runs
  them in LIFO order, restoring both the heap and the in-memory index.

---

## 8. Recovery

**Write-Ahead Log (`src/recovery/wal.hpp`).** MiniDB uses **logical,
primary-key-keyed** logging. Records are appended as
`[len][type][txn][payload]`; types are `BEGIN, COMMIT, ABORT, INSERT, UPDATE,
DELETE, CREATE_TABLE`. `INSERT/UPDATE` store the after-image; `UPDATE/DELETE`
store the before-image. **`COMMIT` forces (fsyncs) the log** — the durability
point — and the buffer pool flushes the log before any data page (write-ahead).

**Crash recovery procedure (`Database::recover`).** Two passes, ARIES-style but
logical, and **idempotent** because operations are keyed by primary key and
applied as upserts:

1. **Analysis** — scan the log, collect the set of committed transactions.
2. **Redo** — replay every data operation in order (`INSERT/UPDATE` → upsert,
   `DELETE` → delete-by-key), reconstructing the crash-time state on top of the
   possibly partially-flushed data file.
3. **Undo** — for transactions that never committed, reverse their operations
   using the stored before-images.

A clean shutdown calls `checkpoint()` (flush pages, persist the catalog, then
truncate the WAL) so the next open has nothing to recover. The crash-recovery
path is demonstrated by `tools/demo.cpp` §6 and asserted by the test suite
(child process writes committed + in-flight rows, then `_exit()`s without a
checkpoint; the parent reopens and verifies committed survived, in-flight did
not).

---

## 9. Extension Track C — LSM-Tree

**Motivation.** The core engine uses an update-in-place B+ Tree store, which
turns writes into random I/O. A **log-structured merge tree** instead batches
writes in memory and flushes them **sequentially**, which is far friendlier to
both disks and write-heavy workloads.

**Design (`src/lsm/`).**
- **MemTable** (`memtable.hpp`) — an in-memory sorted map absorbing all writes;
  a delete inserts a **tombstone**. Flushed when it exceeds a byte threshold.
- **SSTable** (`sstable.hpp/.cpp`) — an immutable, sorted on-disk run written in
  one linear pass; a dense in-memory key→offset index gives point reads in one
  seek.
- **LSM-Tree** (`lsm_tree.hpp/.cpp`) — routes reads through MemTable →
  SSTables (newest→oldest, first hit wins) and triggers **compaction**, a
  k-way merge of the runs that keeps the newest value per key and drops
  tombstones, bounding read and space amplification.

**Results.** See §10 — the LSM delivers **~3× the durable write throughput** of
the B+ Tree store in our benchmark.

---

## 10. Benchmarks

**Setup.** `benchmarks/bench_lsm.cpp`, built as `bin/bench_lsm`. Single machine,
`-O2`. Two experiments over a shuffled integer key space.

**Experiment 1 — in-memory micro-benchmark (200k keys).**

| Store     | Write (Kops/s) | Read (Kops/s) | On disk        |
|-----------|----------------|---------------|----------------|
| B+ Tree   | ~1600          | ~2360         | in-memory      |
| LSM-tree  | ~40            | ~140          | ~4 MB (2–3 SST)|

**Experiment 2 — durable write throughput (20k rows, fsync-backed).**

| Store                         | Write (Kops/s) |
|-------------------------------|----------------|
| B+ Tree store (heap+WAL, fsync/commit) | ~110  |
| LSM-tree (batched, sequential flush)   | ~320  |

> **LSM write speedup ≈ 2.9×.** (Exact numbers vary per run/machine.)

**Analysis.**
- The in-memory B+ Tree wins *raw* ops (no I/O), but it is volatile and does
  random in-place updates; LSM reads must probe several runs (**read
  amplification**), which compaction keeps small.
- With **durability on**, the LSM's batched **sequential** writes beat the B+
  Tree store's per-commit random write + `fsync` — the LSM's core advantage.
- The LSM keeps overlapping runs until compaction (**space amplification**);
  compaction reclaims tombstoned/overwritten keys.

Run it yourself: `make bench` (or `./bin/bench_lsm 200000`).

---

## 11. Limitations

- **In-memory B+ Tree index**, rebuilt by scanning the heap on open — bounded by
  RAM and adds startup cost for very large tables. A paged, on-disk B+ Tree is
  the natural next step.
- **Table-granularity locking.** Correct and serializable, but coarser than
  row-level locking; concurrency on a single hot table is limited. The lock
  manager is generic over a `LockId`, so it generalizes to row locks.
- **WAL is not truncated mid-session**; a long-running session grows the log
  until the next clean checkpoint. Periodic checkpointing is future work.
- **No-steal-friendly recovery assumes every table has a primary key** (enforced
  at `CREATE TABLE`), since logical logging is keyed by it.
- **Nested-loop joins only** (no hash/merge join); inner side is materialized.
- Types are limited to `INT` and `TEXT`; no `NULL`-aware three-valued logic
  beyond basic comparisons; no subqueries or `HAVING`.
- The LSM extension is a standalone key/value store used for the benchmark; it is
  not (yet) wired underneath SQL tables.

---

## 12. How to Run

**Dependencies.** A C++17 compiler (`g++` ≥ 9) and `make`. No external libraries.

```bash
make            # build everything into ./bin
make test       # build & run the automated test suite (32 checks)
make demo       # narrated walkthrough of every feature (incl. crash recovery)
make bench      # LSM-tree vs B+ Tree benchmark
make repl       # interactive SQL shell
```

**Interactive shell.**

```bash
./bin/minidb mydata           # opens/creates the database in ./mydata
```

```sql
CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT);
INSERT INTO users VALUES (1,'alice',30),(2,'bob',25),(3,'carol',35);
CREATE TABLE orders (oid INT PRIMARY KEY, uid INT, amount INT);
INSERT INTO orders VALUES (10,1,100),(11,1,250),(12,3,300);

.explain on
SELECT id, name FROM users WHERE id = 2;                 -- IndexScan
SELECT name, age FROM users WHERE age >= 30 ORDER BY age DESC;
SELECT u.name, o.amount FROM users u JOIN orders o ON u.id = o.uid;
SELECT uid, COUNT(*), SUM(amount) FROM orders GROUP BY uid;

BEGIN;
UPDATE users SET age = 99 WHERE id = 1;
ABORT;                                                   -- rolled back

.tables
.schema users
.exit
```

**Project layout.**

```
src/        core engine (storage, index, catalog, sql, execution,
            optimizer, transaction, recovery, lsm, database)
tools/      repl.cpp (shell), demo.cpp (narrated demo)
benchmarks/ bench_lsm.cpp (Track C benchmark)
tests/      run_tests.cpp (automated suite)
docs/       architecture.md (design deep-dive)
```

See `docs/architecture.md` for a component-by-component deep dive and the
viva-prep notes.
