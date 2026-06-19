# MiniDB

A small but complete relational database engine, built from foundational
components for the Advanced DBMS capstone. MiniDB implements a page-based storage
engine, a page-backed B+ tree index, SQL query execution, a cost-based optimizer,
two-phase-locking transactions, and write-ahead-logging crash recovery — and an
extension track for performance.

---

## Team

**Team Name:** Team_MiniDB

| Member | Scaler Email | Roll Number |
| --- | --- | --- |
| Daksh Jain | dakshjainn2004@gmail.com | _add roll number_ |
| Paawanjot Kaur | paawanjotkaur05@gmail.com | _add roll number_ |

> Please fill in the roll numbers before final submission.

---

## 1. Project Overview

**Problem statement.** Build a working relational database engine from scratch that
integrates the core internals studied across the course — storage, indexing, query
processing, the optimizer, transactions, and recovery — into one coherent system
that can be demonstrated and explained.

**Goals.**

- A page-based storage engine with a buffer pool (not an in-memory toy).
- A persistent B+ tree index that real queries actually use.
- End-to-end SQL: `CREATE TABLE/INDEX`, `INSERT`, `SELECT` (projection, `WHERE`,
  `JOIN`), `DELETE`, and `BEGIN/COMMIT/ROLLBACK`.
- A cost-based optimizer that chooses access paths and join order.
- Serializable transactions via strict two-phase locking with deadlock handling.
- Durability and crash recovery via write-ahead logging.

**Chosen extension track.** **Track A — Performance** (vectorized / batch
execution), benchmarked against the row-store baseline. See §9.

---

## 2. System Architecture

```
                         ┌──────────────────────────┐
              SQL text →  │  Lexer → Parser (AST)    │
                         └─────────────┬────────────┘
                                       │  Statement
                         ┌─────────────▼────────────┐
                         │  Binder (resolve columns) │
                         └─────────────┬────────────┘
                                       │
                         ┌─────────────▼────────────┐
                         │  Optimizer                │
                         │  • access path: index/seq │
                         │  • join order + INL join   │
                         └─────────────┬────────────┘
                                       │  Executor tree (Volcano)
        ┌──────────────────────────────▼──────────────────────────────┐
        │  SeqScan │ IndexScan │ Filter │ Projection │ (Index)NLJoin    │
        └───────┬───────────────┬───────────────────────────┬─────────┘
                │               │                           │
        ┌───────▼──────┐ ┌──────▼───────┐          ┌────────▼────────┐
        │  TableHeap   │ │  B+ Tree     │          │  Lock Manager   │
        │ (slotted pgs)│ │  (index pgs) │          │  (2PL/wait-die) │
        └───────┬──────┘ └──────┬───────┘          └─────────────────┘
                │               │
        ┌───────▼───────────────▼────────┐   ┌──────────────────────┐
        │      Buffer Pool (LRU)         │◄──┤  Catalog (page 0)    │
        └───────────────┬────────────────┘   └──────────────────────┘
                        │ pages                ┌──────────────────────┐
        ┌───────────────▼────────────────┐    │  Log Manager (WAL)   │
        │   Disk Manager  (<db> file)    │    │  (<db>.wal file)     │
        └────────────────────────────────┘    └──────────────────────┘
```

**Major modules** (under `src/`):

| Module | Responsibility |
| --- | --- |
| `storage/` | Disk manager, slotted pages, heap files, tuples |
| `buffer/` | Buffer pool manager + LRU replacer |
| `catalog/` | System catalog (schemas, indexes, stats) on page 0 |
| `index/` | Page-backed B+ tree, node layout, key codec |
| `sql/` | Lexer, recursive-descent parser, AST |
| `execution/` | Volcano-style executors |
| `optimizer/` | Cost model + plan selection |
| `concurrency/` | Transaction, lock manager, transaction manager |
| `recovery/` | Write-ahead log manager |
| `engine/` | `Database` — ties everything together |

**Data flow.** A SQL string is tokenized, parsed to an AST, bound (column names →
indices), planned by the optimizer into a tree of executors, and pulled row-by-row
(`Next()`). All data access flows through the buffer pool to the disk manager; the
catalog persists metadata; the WAL records every change.

---

## 3. Storage Layer

**Page format.** Everything is a fixed 4 KB page (`PAGE_SIZE`). Heap pages are
*slotted pages*: a header (`next_page_id`, `num_slots`, `free_space_ptr`), a slot
directory growing down from the header, and tuple bytes growing up from the end. A
slot stores `(offset, length)`; a deleted slot is tombstoned by zeroing its offset
(the bytes are left in place — this is what makes transactional rollback of a delete
cheap). Tuples encode INTEGER as 4 raw bytes and VARCHAR as a 4-byte length prefix
plus characters.

**Heap files.** A table is a singly-linked list of slotted pages. New tuples append
to the tail page; when it fills, a fresh page is allocated and linked. Tuples are
addressed by `RID = (page_id, slot)` and scanned by a forward iterator that skips
tombstones.

**Buffer pool.** A fixed array of frames caches pages. `FetchPage`/`NewPage` pin a
page; `UnpinPage` releases it; a dirty victim is written back before its frame is
reused (LRU replacement). It is the single chokepoint for page access and tracks hit
/ miss counts for benchmarking.

---

## 4. Indexing

**B+ tree design.** A page-backed, non-unique B+ tree mapping a fixed-width key to
RIDs. Each node is one buffer-pool page (`src/index/b_plus_tree_page.h`):

- **Header (8 bytes):** node type (leaf/internal), key count, and (for leaves) the
  `next_leaf` page id so leaves form a left-to-right linked list.
- **Leaf node:** fixed-stride entries of `key_bytes ++ RID(8)`, sorted by key.
  Capacity ≈ `(4096 − 8) / (key_width + 8)` (≈ 340 for 4-byte keys).
- **Internal node:** `n` separator keys + `n + 1` child page ids in fixed-capacity
  regions, so slot offsets never shift as the count changes.

**Keys.** INTEGER keys are 4 bytes; VARCHAR(n) keys are the column's declared width,
NUL-padded. Keys are decoded back to `Value` and compared with `Value::Compare`, so
comparison respects type semantics (no signed-integer byte-order pitfalls).

**Search path.** From the root, a binary search in each internal node selects the
child (`upper_bound`: keys equal to a separator descend right), down to a leaf; a
binary search in the leaf finds the key, and duplicate keys are collected by walking
`next_leaf`. **Insert** descends with a path stack, splits a full leaf (copy-up
separator) or internal node (move-up middle key), and grows a new root when the root
splits; the new root id is persisted via a callback into the catalog. **Delete** is
lazy — the entry is removed in place with no merge/rebalance (a documented
trade-off). Indexes are created with `CREATE INDEX i ON t (col)`, built from the heap,
and maintained automatically on `INSERT`/`DELETE`.

---

## 5. Query Execution

**Parser.** A hand-written lexer tokenizes the input; a recursive-descent parser
produces a statement AST. Supported grammar:

```sql
CREATE TABLE t (c INT, c VARCHAR(n), ...)
CREATE INDEX i ON t (c)
INSERT INTO t VALUES (..), (..)
SELECT a, b | *  FROM t [JOIN u ON t.x = u.y] [WHERE pred]
DELETE FROM t [WHERE pred]
BEGIN | COMMIT | ROLLBACK
```

`pred` is a conjunction/disjunction (`AND`/`OR`) of comparisons (`= != <> < <= > >=`)
over columns and literals.

**Plan generation.** After binding column references to indices, the optimizer builds
a tree of executors. A single-table scan becomes a `SeqScan` or `IndexScan`; a join
becomes a nested-loop or index-nested-loop join; a `WHERE` adds a `Filter`; the select
list adds a `Projection`.

**Operator execution.** Executors follow the **Volcano (iterator) model** — each
`Next()` pulls one row from its children: `SeqScanExecutor`, `IndexScanExecutor`,
`FilterExecutor`, `ProjectionExecutor`, `NestedLoopJoinExecutor`,
`IndexNestedLoopJoinExecutor`.

---

## 6. Optimizer

The optimizer (`src/optimizer/optimizer.{h,cpp}`) is a pure, unit-testable cost model
driven by statistics kept in the catalog: per-table row count **N** and per-index
distinct-key count **NDV** (gathered for free during index build, incremented on
insert).

**Selectivity & cost estimation.** For an equality `col = const` on an indexed column:

```
est_match     = ceil(N / NDV)                 # estimated matching rows
index_cost    = est_match + 2                  # root→leaf descent + heap fetches
seq_cost      = ceil(N / tuples_per_page)      # heap pages to scan
choose index ⇔ index_cost < seq_cost
```

So a selective lookup on a large table picks the index, while a tiny table or a
low-cardinality column correctly falls back to a sequential scan.

**Join ordering.** For a two-table equi-join, if one side has an index on its join
column, the optimizer makes that side the *probed inner* and drives with the other
table using an **index-nested-loop join** (one index probe per outer row instead of a
full rescan). When both sides are indexed, the larger relation becomes the inner.

---

## 7. Transactions & Concurrency

**Locking strategy.** A row-level lock manager (`src/concurrency/lock_manager.cpp`)
provides shared (S) and exclusive (X) locks keyed by RID, under **strict two-phase
locking**: a transaction acquires locks as it runs and releases them all at
commit/abort.

**Isolation guarantees.** Strict 2PL yields **serializable**, recoverable schedules.
Writes (`INSERT`/`DELETE`) take X locks on affected rows inside a transaction.

**Deadlock handling.** Deadlock is *prevented* with **wait-die** using the
transaction id as an age: if an older transaction holds a conflicting lock, the
younger requester aborts (and retries); if only younger transactions conflict, the
older one waits. Because only older transactions ever wait on younger ones, the
wait-for graph can never contain a cycle. `BEGIN/COMMIT/ROLLBACK` expose this at the
SQL level; `ROLLBACK` undoes a transaction's writes in reverse (an insert is
tombstoned; a delete is restored in place).

---

## 8. Recovery

**WAL design.** Every row change is appended to a write-ahead log (`<db>.wal`,
`src/recovery/log_manager.cpp`) before it is considered durable, and the log is
fsync'd at **commit** (force-log-at-commit). MiniDB uses a no-force / no-steal buffer
policy in spirit, so recovery is **redo-only**.

**Log records.** `BEGIN`, `INSERT(txn, table, row-bytes)`, `DELETE(txn, table,
row-bytes)`, `COMMIT`, `ABORT`. The first byte of the WAL is a clean/dirty marker: a
clean shutdown writes *clean*; every open writes *dirty*. Finding the marker still
dirty on open means the previous run crashed.

**Crash recovery procedure.** On a crash-detected open, recovery reads the log,
collects the set of committed transactions, resets every table's heap and indexes to
empty, and **redoes the committed records in order** (re-inserting rows and rebuilding
indexes; redoing committed deletes). Uncommitted transactions are simply never
replayed. Committed transactions are therefore preserved across a crash even though
their data pages never reached disk. (Limitation: the WAL is not checkpointed/
truncated — see §11.)

---

## 9. Extension Track — A: Performance (Vectorized Execution)

**Motivation.** The baseline executors are tuple-at-a-time (Volcano): one virtual
`Next()` call and one row materialized per tuple. For scan-and-filter-heavy queries
this is dominated by per-tuple interpretation overhead. Vectorized execution amortizes
that overhead by passing **batches** of column values between operators.

**Design.** A batch-at-a-time scan/filter pipeline that pulls blocks of rows from the
heap and applies predicates over a whole batch before materializing output, reducing
per-tuple dispatch and improving cache behaviour. Implemented on the
`track-a-vectorized` branch.

**Results.** The branch includes `benchmarks/bench_vectorized.cpp`, which compares the
vectorized scan/filter against the row-at-a-time baseline on the same data and reports
the latency / throughput improvement. See that branch's README section and benchmark
output for the measured speedup.

---

## 10. Benchmarks

Full setup and analysis: [`benchmarks/RESULTS.md`](benchmarks/RESULTS.md).
Representative run of `bench_index` (50,000 rows, 2,000 point queries):

| Metric | Value |
| --- | --- |
| Insert throughput | ~363,000 rows/sec |
| Sequential scan | 7.41 ms / query |
| Index scan | 0.003 ms / query |
| **Index speedup** | **~2,100×** |
| Buffer-pool hit rate | 99.9% |

The index turns an O(N) scan into an O(log N) descent plus one heap fetch — the exact
access-path difference the optimizer selects between. On tiny tables the optimizer
correctly prefers the sequential scan.

---

## 11. Limitations

- **Index delete is lazy** — no node merge/rebalance, so heavy delete workloads leave
  under-full nodes (space, not correctness).
- **Equality-only index access** — range predicates (`<`, `>`) still use a sequential
  scan, though the linked leaves make range scans a natural extension.
- **No WAL checkpointing** — the log is never truncated, so it grows across sessions
  and recovery replays the full committed history. A checkpoint would bound both.
- **Redo-only recovery assumes no-steal** — committed pages are assumed not evicted
  before a checkpoint; a full ARIES-style undo pass would handle stolen pages.
- **SQL read locks** — explicit transactions take write locks; the lock manager fully
  supports shared locks (and is exercised concurrently in tests), but SQL-level
  `SELECT` does not yet take per-row read locks.
- **Single-process engine** — one `Database` per file; no client/server layer.
- **Small type system** — INTEGER and VARCHAR; no NULLs or `UPDATE` statement.

---

## 12. How to Run

**Dependencies.** A C++20 compiler and CMake ≥ 3.16. No external libraries.

**Build.**

```bash
cmake -S . -B build
cmake --build build -j
```

**Run the tests.**

```bash
ctest --test-dir build --output-on-failure
```

**Run the interactive shell.**

```bash
./build/minidb mydb.db
```

**Example session.**

```sql
CREATE TABLE users (id INT, name VARCHAR(20));
INSERT INTO users VALUES (1,'alice'),(2,'bob'),(3,'carol');
CREATE INDEX users_id ON users (id);
SELECT name FROM users WHERE id = 2;      -- uses the index on a large table

BEGIN;
DELETE FROM users WHERE id = 1;
SELECT id FROM users;                     -- 2 rows, inside the transaction
ROLLBACK;
SELECT id FROM users;                     -- 3 rows again
```

**Run the benchmark.**

```bash
cmake --build build --target bench_index -j
./build/bench_index
```

You can also pipe a script of statements: `./build/minidb db < script.sql`.
