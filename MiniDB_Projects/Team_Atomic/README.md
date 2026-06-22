# MiniDB — A Relational Database Engine from Scratch

> Advanced DBMS Capstone Project · Extension **Track C — Modern Storage (LSM-tree)**

MiniDB is a small but complete relational database engine written from scratch
in C++17. It integrates the components studied across the course — a page-based
storage engine, a B+ tree index, a SQL parser and Volcano-model executor, a
cost-based optimizer, strict two-phase-locking transactions with deadlock
detection, and write-ahead-logging crash recovery — into one coherent system
driven by a SQL shell. For the extension track it adds an **LSM-tree storage
engine** and benchmarks it head-to-head against the B+ tree row store.

---

## Team Information

**Team Name:** Atomic

| Full Name | Scaler Email | Roll Number |
|-----------|--------------|-------------|
| Jils Patel | patel.24bcs10232@sst.scaler.com | 24BCS10232 |
| Mohit Kumar | mohit.24bcs10222@sst.scaler.com | 24BCS10222 |
| Jaivardhan D Rao | jaivardhan.24bcs10117@sst.scaler.com | 24BCS10117 |
| Vibhuti Bhatnagar | vibhuti.24bcs10288@sst.scaler.com | 24BCS10288 |

_Submitted as PR title_ `TEAM_Atomic` _to_ <https://github.com/KnightKnight27/scaler-Adv-DBMS>.

---

## 1. Project Overview

**Problem statement.** Build a working relational database engine that
demonstrates how real databases are engineered internally — not a feature
checklist, but a correct, integrated system whose every layer can be explained.

**Goals.**
- One engine, end-to-end: `SQL text → parse → optimize → execute → storage`.
- Durable, recoverable storage with ACID-flavoured transactions.
- A real cost-based decision (index scan vs table scan, join order).
- A second storage engine (LSM) to study a fundamental storage trade-off.

**Chosen extension track: C — Modern Storage.** We implemented an LSM-tree
(MemTable → SSTables → compaction) and benchmarked write throughput, read
latency, and storage amplification against the B+ tree based row store.

---

## 2. System Architecture

```
                         ┌─────────────────────────┐
   SQL text  ──────────► │   sql/  Lexer + Parser   │  → Statement (AST)
                         └────────────┬────────────┘
                                      │
                         ┌────────────▼────────────┐
                         │ optimizer/  Cost-based   │  stats, selectivity,
                         │   planner                │  scan choice, join order
                         └────────────┬────────────┘
                                      │ operator tree
                         ┌────────────▼────────────┐
                         │ exec/  Volcano executors │  SeqScan / IndexScan /
                         │   Init() + Next()        │  Filter / Join / Project
                         └──────┬─────────────┬─────┘
                                │             │
                ┌───────────────▼──┐   ┌──────▼──────────────┐
                │ index/ B+ tree   │   │ storage/ TableHeap  │  rows in
                │  key → RID       │   │  (slotted pages)    │  slotted pages
                └────────┬─────────┘   └──────────┬──────────┘
                         │                        │
                         └──────────┬─────────────┘
                          ┌─────────▼──────────┐    ┌───────────────────┐
                          │ storage/ BufferPool │   │ txn/ LockManager  │ 2PL +
                          │  (LRU, pin/unpin)   │   │ TransactionManager│ deadlock
                          └─────────┬──────────┘    └───────────────────┘
                          ┌─────────▼──────────┐    ┌───────────────────┐
                          │ storage/ DiskManager│   │ recovery/ WAL +   │ redo/undo
                          │   (4 KB pages)      │   │  Recover()        │ on restart
                          └─────────┬──────────┘    └───────────────────┘
                                    ▼
                              <db>.db  +  <db>.catalog  +  <db>.wal

   Extension:  lsm/  MemTable → SSTable (Bloom + index) → Compaction
               (alternative storage engine; benchmarked vs the B+ tree store)
```

**Major modules** (`src/`):

| Module | Responsibility |
|--------|----------------|
| `common/` | shared types (`Value`, `RID`), page-size/id config |
| `storage/` | `DiskManager`, `Page`, `BufferPoolManager` (LRU), slotted `TablePage`, `TableHeap` |
| `record/` | `Schema`, `Tuple` (row ↔ bytes serialization) |
| `catalog/` | table/column metadata, persisted to `<db>.catalog` |
| `index/` | `BPlusTree` — page-backed primary-key index |
| `sql/` | `Parser` (lexer + recursive descent) → `Statement` AST |
| `exec/` | Volcano operators + predicate binding/evaluation |
| `optimizer/` | statistics, selectivity, scan/join planning |
| `txn/` | `LockManager` (2PL + deadlock), `TransactionManager` |
| `recovery/` | `WAL` log records + redo/undo recovery |
| `lsm/` | `MemTable`, `SSTable`, `BloomFilter`, `LSMTree` (Track C) |
| `engine/` | `Database` — ties everything together, dispatches SQL |

**Data flow.** `Database::Execute(sql)` parses one statement; `SELECT` goes
through the optimizer to build a Volcano operator tree pulled with
`Init()/Next()`; `INSERT`/`DELETE` acquire locks, append a WAL record
(write-ahead), then mutate the heap and index. All page access goes through the
buffer pool to the single `<db>.db` file.

---

## 3. Storage Layer

**Page format.** Fixed **4 KB** pages, addressed by `page_id` at byte offset
`page_id * 4096` in one data file (`DiskManager`). A heap page (`TablePage`) is
**slotted**:

```
[ next_page_id | num_slots | free_ptr ][ slot0 | slot1 | ... ]
                                              ... free space ...
                              [ ...... tuple1 ][ tuple0 ]
```

Slots `(offset, length)` grow forward from the header; tuples grow backward from
the end. A delete sets `length = 0` (a tombstone) but never removes the slot, so
a row's `RID = (page_id, slot)` stays stable for the index to point at.

**Heap files.** A `TableHeap` is a singly linked list of slotted pages
(`next_page_id`). Inserts walk the chain for free space and append a new page
when full; a forward iterator drives sequential scans.

**Buffer pool.** `BufferPoolManager` caches a fixed number of 4 KB frames. Pages
are **pinned** while in use; only unpinned pages are evicted, chosen by an
**LRU replacer**, and **dirty pages are written back** before their frame is
reused. This is the single choke point through which all storage I/O passes.

---

## 4. Indexing

**B+ tree design.** A page-backed B+ tree (`index/bplus_tree.*`) maps an
`int64` primary key → `RID`. A durable **header page** (id stored in the
catalog) holds the current root page id, so the tree survives restart and root
splits.

**Node structure.**
- **Leaf:** `type | size | next_leaf` then `size` × `(key, RID)` entries; the
  `next_leaf` pointer chains leaves for range scans.
- **Internal:** `type | size` then `size+1` child page ids and `size` separator
  keys.

Nodes are read/modified/written whole (clarity over raw speed). Order is 254
keys/node, comfortably fitting a 4 KB page.

**Search path.** Start at the root; in each internal node binary-search
(`upper_bound`) the separator keys to pick the child, descend until a leaf, then
scan the leaf for the key. Insert records the descent path so a split can
propagate a separator upward, creating a new root when the old root splits.
Range scans locate the low key's leaf, then follow `next_leaf` pointers.

The optimizer turns PK predicates into a `[low, high]` range and uses this tree
when selective (see §6). `make run-tests` exercises 5,000 keys through
insert/search/range/delete with multi-level splits.

---

## 5. Query Execution

**Parser.** `sql/parser.*` is a hand-written lexer + recursive-descent parser
for the supported subset:

```sql
CREATE TABLE t (col INTEGER PRIMARY KEY, col VARCHAR, ...);
INSERT INTO t VALUES (..., ...);
SELECT * | col,...  | COUNT(*) FROM t [JOIN t2 ON t.a = t2.b] [WHERE p AND ...];
DELETE FROM t [WHERE p AND ...];
BEGIN; COMMIT; ROLLBACK;
```

It produces a `Statement` AST (`sql/ast.h`).

**Query plan generation.** The optimizer (§6) lowers a `SELECT` into a tree of
operators. `WHERE` predicates are *bound* to concrete column positions in the
operator's output schema (`BindPredicate`), with ambiguity/unknown-column
checks.

**Operator execution — Volcano (iterator) model.** Every operator implements
`Init()` and `Next(Row*)`; the root is pulled until exhausted. Operators:
`SeqScanExecutor`, `IndexScanExecutor`, `FilterExecutor`,
`NestedLoopJoinExecutor` (rescans the inner child per outer row),
`ProjectionExecutor`, and `CountExecutor`. `INSERT`/`DELETE` are executed
directly by the engine (with locking + WAL).

---

## 6. Optimizer

`optimizer/optimizer.*` is cost-based.

**Statistics.** `Analyze()` scans a table to get its row count and the
min/max primary key.

**Selectivity estimation.** For a PK range `[low, high]`, estimated matching rows
= `rows × (high−low+1) / (pk_max−pk_min+1)`, clamped to `[1, rows]`. Equality on
a unique PK estimates ~1 row.

**Scan choice (table scan vs index scan).** PK predicates (`=, <, <=, >, >=`)
are folded into one `[low, high]` range. The optimizer compares
`seq_cost ≈ rows` against `idx_cost ≈ log2(rows) + estimated_matches` and picks
the cheaper; residual (non-PK) predicates become a `Filter` on top. The chosen
plan and its cost numbers are reported (visible via `.plan on` in the shell):

```
IndexScan(t, pk in [777,777]) est_rows=1 idx_cost=11.0 < seq_cost=1001.0
SeqScan(t) rows=1000 (idx_cost=1010.0 >= seq_cost=1001.0, range not selective)
```

**Join ordering.** For a two-table join, nested-loop cost is
`|outer| + |outer|·|inner|`, so the optimizer makes the **smaller relation the
outer** one and reports the decision (`outer=small(2), inner=t(1000)`).

---

## 7. Transactions & Concurrency

**Locking strategy — Strict 2PL.** `txn/lock_manager.*` provides shared/exclusive
locks keyed per row (`table:pk`). Reads would take `SHARED`, writes take
`EXCLUSIVE`; **locks are held until commit/abort** (strict 2PL), which gives
serializable schedules and avoids cascading aborts. `TransactionManager` issues
monotonically increasing transaction ids and owns the lock manager.

**Isolation guarantees.** Strict two-phase locking yields **serializable**
isolation: an exclusive lock blocks any other reader/writer of the row until the
holder commits, and shared locks let readers coexist but block writers.

**Deadlock handling.** Blocked acquisitions record **waits-for** edges. On every
wait the manager runs DFS over the waits-for graph; if it finds a cycle it
selects the **youngest transaction** (largest id) as the victim and raises
`TxnAborted`, so older transactions never starve and the system makes progress.
The concurrency test demonstrates two threads deadlocking → exactly one aborts,
one commits, and shared/exclusive blocking behaviour.

The SQL engine exposes this via `BEGIN` / `COMMIT` / `ROLLBACK`; each
write takes an exclusive lock, and `ROLLBACK` undoes the transaction's applied
row operations.

---

## 8. Recovery

**WAL design.** `recovery/wal.*` is an append-only binary log
(`<db>.wal`). The **write-ahead rule** is honored: a record is pushed to the OS
on append, and the log is `fsync`-ed before a `COMMIT` is reported, so a
committed transaction is always recoverable.

**Log records.** `BEGIN`, `INSERT` (table, key, *after-image*), `DELETE`
(table, key, *before-image*), `COMMIT`, `ABORT`, `CHECKPOINT`. Before- and
after-images make both redo and undo possible.

**Crash recovery procedure.** On startup `Database::Recover()`:
1. **Analysis** — scan the log; a transaction is a *winner* iff it has a
   `COMMIT` record.
2. **Redo** — replay every winner's `INSERT`/`DELETE` in log order.
3. **Undo** — reverse every loser's operations in reverse order.

Both passes are **idempotent by primary key** (redo-insert skips if the key
exists; undo-insert reinserts only if absent), so recovery is correct
regardless of which dirty pages happened to reach disk before the crash.
Recovery then checkpoints (flush pages, save catalog, truncate the log). The
recovery test crashes mid-transaction and verifies committed rows survive while
the uncommitted transaction is rolled back.

---

## 9. Extension Track — C: LSM-tree Storage

**Motivation.** B+ tree storage turns every write into a (possibly random) page
update. Write-heavy workloads pay for this in random I/O and node splits. An
LSM-tree converts random writes into **sequential** ones, trading some read cost
for large write gains — a different and important point in the storage design
space.

**Design** (`lsm/`):
- **MemTable** — an in-memory sorted `std::map` write buffer; deletes insert a
  *tombstone* rather than removing.
- **SSTable** — when the MemTable fills, it is flushed to an immutable, sorted
  on-disk run (`key | tombstone | len | value`). On open it builds an in-memory
  offset index and a **Bloom filter**, so a point read is *bloom → index →
  one read*.
- **Reads** consult MemTable, then SSTables newest → oldest (newer versions and
  tombstones shadow older ones).
- **Compaction** — size-tiered: when enough runs accumulate they are k-way
  merged into one, keeping the newest version of each key and **dropping
  tombstones**, which bounds read amplification and reclaims space.

**Integration.** The LSM is a first-class engine selectable per table via
`CREATE TABLE ... USING LSM`. Both engines implement a common `RowStore`
interface (`storage/row_store.h`), so the *same* parser, optimizer, executor,
transaction, and WAL-recovery code paths run over either — the engine choice is
transparent to SQL (see `tests/test_lsm_sql.cpp`).

**Results.** vs the B+ tree row store on 200k rows (full data in
[`benchmarks/results.md`](benchmarks/results.md)): LSM writes ~65–75× faster
and uses ~1.3× space (vs ~1.8–2.1×), at the cost of ~3–4× higher point-read
latency — the textbook LSM trade-off.

---

## 10. Benchmarks

**Setup.** Apple M5 Pro, Apple clang 21 `-O2`; `make bench` (N = 200,000;
sequential and random key orders; N random point reads). See
[`benchmarks/results.md`](benchmarks/results.md) for methodology and full
tables.

**Results (random-key inserts, N = 200k).**

| metric | B+tree store | LSM store |
|--------|-------------:|----------:|
| write throughput | 26,902 /s | 1,714,664 /s |
| read latency | 1.95 µs | 7.05 µs |
| space amplification | 1.76× | 1.29× |

**Analysis.** LSM wins decisively on write throughput (sequential, batched
flushes vs random page writes + splits) and on space (dense sorted runs +
compaction vs partially-full pages). The B+ tree wins on read latency (one
logarithmic descent vs probing several SSTables even with Bloom filters). The
right engine depends on whether the workload is write- or read-dominated.

---

## 11. Limitations

- **Index keys are `int64`.** The primary-key B+ tree indexes integer PKs only;
  string-PK tables use a heap scan. Secondary indexes are not implemented.
- **B+ tree delete does not rebalance** (entries are removed from leaves without
  merge/redistribute) — lookups stay correct but the tree can become sparse
  under heavy deletion.
- **SQL subset.** No `UPDATE`, `GROUP BY`, `ORDER BY`, sub-queries, or
  multi-way (>2 table) joins; aggregation is limited to `COUNT(*)`; only
  conjunctive (`AND`) predicates.
- **Single-connection SQL shell.** 2PL is exercised by the concurrency tests;
  the shell executes statements serially (trivially serializable). Reads in the
  SQL layer are not lock-protected (writers are).
- **LSM caveats.** The LSM engine is fully wired under SQL
  (`CREATE TABLE ... USING LSM`), but its full-table scan materializes rows
  in memory (point/range lookups stream), and it keeps its own files outside
  the shared buffer pool.
- **Crash model** is process crash (kill); `fsync` at commit additionally guards
  against power loss. No group commit (commit `fsync`s per transaction).

---

## 12. How to Run

**Dependencies.** A C++17 compiler (Apple clang / g++) and `make`. No external
libraries.

```bash
# Build the engine + SQL shell
make                      # produces ./minidb

# Run the interactive shell (creates demo.db / .catalog / .wal)
./minidb demo
```

```sql
minidb> CREATE TABLE users (id INTEGER PRIMARY KEY, name VARCHAR, age INTEGER);
minidb> INSERT INTO users VALUES (1, 'alice', 30);
minidb> INSERT INTO users VALUES (2, 'bob', 25);
minidb> .plan on
minidb> SELECT name FROM users WHERE id = 1;       -- uses the index
minidb> SELECT COUNT(*) FROM users;
minidb> BEGIN;
minidb> INSERT INTO users VALUES (3, 'carol', 40);
minidb> ROLLBACK;                                  -- carol is undone

-- The same SQL runs over the LSM storage engine (Track C):
minidb> CREATE TABLE logs (id INTEGER PRIMARY KEY, msg VARCHAR) USING LSM;
minidb> INSERT INTO logs VALUES (1, 'hello');
minidb> SELECT * FROM logs WHERE id = 1;           -- range scan over SSTables
minidb> .tables
minidb> .exit
```

Shell commands: `.tables`, `.plan on|off`, `.exit`.

```bash
# Run the full test suite (storage, index, SQL, optimizer, txn, recovery, LSM)
make run-tests

# Run the Track C benchmark (LSM vs B+ tree)
make bench                # or: ./build/bench 500000
```
