# MiniDB — A Relational Database Engine from Scratch

> Advanced DBMS Capstone Project · Extension **Track C — Modern Storage (LSM-tree)**

MiniDB is a small but complete relational database engine written from scratch
in **C++17**. It integrates the components studied across the course — a
page-based storage engine with a buffer pool, a B+ tree index, a SQL parser and
a Volcano-model executor, a cost-based optimizer, strict two-phase-locking
transactions with deadlock detection, and write-ahead-logging crash recovery —
into one coherent system driven by a SQL shell. For the extension track it adds
an **LSM-tree storage engine** behind the same storage interface and benchmarks
it head-to-head against the B+ tree row store.

---

## Team Information

**Team Name:** Invicta

| Full Name | Scaler Email | Roll Number |
|-----------|--------------|-------------|
| Gauri Shukla | gauri.24bcs10115@sst.scaler.com | 10115 |
| Ridaa | ridaa.24bcs10394@sst.scaler.com | 10394 |
| Abdur | abdur.24bcs10244@sst.scaler.com | 10244 |

_Submitted as PR title_ `TEAM_Invicta` _to_
<https://github.com/KnightKnight27/scaler-Adv-DBMS>.

---

## 1. Project Overview

**Problem statement.** Build a working relational database engine that
demonstrates how real databases are engineered internally — not a feature
checklist, but a correct, integrated system whose every layer can be explained.

**Goals.**
- One engine, end-to-end: `SQL text → parse → optimize → execute → storage`.
- Durable, recoverable storage with ACID-flavoured transactions.
- A real cost-based decision (index scan vs table scan; join order).
- A second storage engine (LSM) to study a fundamental storage trade-off.

**Chosen extension track: C — Modern Storage.** We implemented an LSM-tree
(MemTable → SSTables → compaction, with per-SSTable Bloom filters) and
benchmarked write throughput, read latency, and storage amplification against
the B+ tree based row store.

---

## 2. System Architecture

```
                       ┌─────────────────────────┐
  SQL text  ─────────► │  sql/  Lexer + Parser    │ → Statement (AST)
                       └────────────┬────────────┘
                       ┌────────────▼────────────┐
                       │ optimizer/  Cost-based   │  stats, selectivity,
                       │   planner                │  scan choice, join order
                       └────────────┬────────────┘
                       ┌────────────▼────────────┐
                       │ exec/  Volcano operators │  SeqScan / IndexScan /
                       │   Init() + Next()        │  Filter / Join / Project / Count
                       └────────────┬────────────┘
                       ┌────────────▼────────────┐
                       │ storage/  RowStore       │  one interface, two engines
                       └──────┬──────────────┬────┘
              ┌───────────────▼──┐     ┌─────▼─────────────────┐
              │ HeapRowStore     │     │ LSMRowStore (Track C) │
              │  B+ tree + heap  │     │  MemTable→SSTable→     │
              └───────┬──────────┘     │  Bloom→compaction      │
                      │                └────────────────────────┘
            ┌─────────▼──────────┐   ┌───────────────────┐
            │ BufferPoolManager  │   │ txn/ LockManager  │ 2PL +
            │  (LRU, pin/unpin)  │   │ TransactionManager│ deadlock detect
            └─────────┬──────────┘   └───────────────────┘
            ┌─────────▼──────────┐   ┌───────────────────┐
            │ DiskManager        │   │ recovery/ WAL +   │ redo/undo
            │  (4 KB pages)      │   │  Recover()        │ on restart
            └─────────┬──────────┘   └───────────────────┘
                      ▼
         <db>.db  +  <db>.catalog  +  <db>.wal  +  <db>_lsm_<table>/*.sst
```

**Major modules** (`src/`):

| Module | Responsibility |
|--------|----------------|
| `common/` | shared types (`Value`, `RID`), page-size/id config |
| `storage/` | `DiskManager`, `Page`, `BufferPoolManager` (LRU), slotted `TablePage`, `TableHeap`, the `RowStore` interface + `HeapRowStore`/`LSMRowStore` |
| `record/` | `Schema`, `Tuple` (row ↔ bytes) |
| `catalog/` | table/column metadata, persisted to `<db>.catalog` |
| `index/` | `BPlusTree` — page-backed primary-key index |
| `sql/` | `Parser` (lexer + recursive descent) → `Statement` AST |
| `exec/` | Volcano operators + predicate binding/evaluation |
| `optimizer/` | statistics, selectivity, scan/join planning |
| `txn/` | `LockManager` (2PL + deadlock), `TransactionManager`, `Transaction` |
| `recovery/` | `WAL` log records + redo/undo recovery |
| `lsm/` | `MemTable`, `SSTable`, `BloomFilter`, `LSMTree` (Track C) |
| `engine/` | `Database` — ties everything together, dispatches SQL |

**Data flow.** `Database::Execute(sql)` parses one statement; a `SELECT` goes
through the optimizer to build a Volcano operator tree pulled with
`Init()/Next()`; `INSERT`/`DELETE` acquire an exclusive lock, append a WAL record
(write-ahead), then mutate the store. All heap page access goes through the
buffer pool to a single `<db>.db` file; LSM tables live in their own directory.

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
the end. Delete sets `length = 0` (a tombstone) but keeps the slot, so a row's
`RID = (page_id, slot)` stays stable for the index.

**Heap files.** A `TableHeap` is a singly linked list of slotted pages
(`next_page_id`). Inserts walk the chain for free space and append a new page
when full; a forward iterator drives sequential scans.

**Buffer pool.** `BufferPoolManager` caches a fixed number of 4 KB frames. Pages
are **pinned** while in use; only unpinned pages are evicted (chosen by an **LRU
replacer**), and **dirty pages are written back** before their frame is reused.
All storage I/O passes through this one choke point.

---

## 4. Indexing

**B+ tree design.** A page-backed B+ tree (`index/bplus_tree.*`) maps an `int64`
primary key → `RID`. A durable **header page** (id stored in the catalog) holds
the current root page id, so the tree survives restart and root splits.

**Node structure.**
- **Leaf:** `type | size | next_leaf` then `size` × `(key, RID)` entries; the
  `next_leaf` pointer chains leaves for range scans.
- **Internal:** `type | size` then `size+1` child page ids and `size` separator
  keys.

Order is 250 keys/node, fitting a 4 KB page.

**Search path.** From the root, in each internal node scan separators to pick
the child, descend to a leaf, then scan the leaf. Insert records the descent
path so a split propagates a separator upward, creating a new root when the old
root splits. Range scans locate the low key's leaf and follow `next_leaf`.
`make run-tests` exercises 5,000 keys through insert/search/range/delete with
multi-level splits.

---

## 5. Query Execution

**Parser.** `sql/parser.*` is a hand-written lexer + recursive-descent parser:

```sql
CREATE TABLE t (col INTEGER PRIMARY KEY, col VARCHAR, ...) [USING LSM];
INSERT INTO t VALUES (..., ...);
SELECT * | col,... | COUNT(*) FROM t [JOIN t2 ON t.a = t2.b] [WHERE p AND ...];
DELETE FROM t [WHERE p AND ...];
BEGIN; COMMIT; ROLLBACK;
```

**Operator execution — Volcano (iterator) model.** Every operator implements
`Init()` and `Next(Tuple*)`; the root is pulled until exhausted. Operators:
`SeqScanExecutor`, `IndexScanExecutor`, `FilterExecutor`,
`NestedLoopJoinExecutor` (rescans the inner per outer row), `ProjectionExecutor`,
`CountExecutor`. `WHERE` predicates are *bound* to column positions in the
operator's output schema, with ambiguity/unknown-column checks.

---

## 6. Optimizer

`optimizer/optimizer.*` is cost-based.

- **Statistics:** per table, row count + observed `[min,max]` primary key.
- **Selectivity:** a PK predicate is turned into a `[low,high]` range; estimated
  fraction `frac = (hi-lo+1)/(max-min+1)`.
- **Scan choice:** compare `seqCost = N` vs `idxCost = log2(N) + frac*N`; use the
  index scan only when it is cheaper. A `Filter` re-checks all predicates above
  the scan, so the result is correct on either path.
- **Join ordering:** the nested-loop join drives with the smaller relation.

The chosen plan is printed via `.explain on`. Example: `WHERE id = 42` →
IndexScan; `WHERE id >= 1` (matches nearly all rows) → SeqScan.

---

## 7. Transactions & Concurrency

- **Locking strategy:** row-level shared/exclusive locks keyed by
  `(table, primary key)`, under **strict two-phase locking** — locks are held
  until commit/abort.
- **Isolation:** serializable for the operations the engine locks (writes take
  exclusive locks); two shared readers are compatible.
- **Deadlock handling:** when a request would block, the `LockManager` builds a
  waits-for graph; a cycle aborts the **youngest** transaction (largest id),
  which releases its locks so others proceed.
- **Rollback:** each transaction keeps an undo log of inverse operations; ABORT
  replays it in reverse. (`tests/test_txn.cpp` demonstrates concurrent threads,
  lock acquisition, and a deadlock with deterministic victim selection.)

---

## 8. Recovery

- **WAL design:** an append-only log of BEGIN / COMMIT / ABORT / INSERT / DELETE
  records (INSERT/DELETE carry the row image). The record is flushed **before**
  the data page changes (the write-ahead rule).
- **Crash recovery procedure** (on startup):
  1. Scan the log; a transaction counts only if it has a COMMIT record.
  2. **REDO** committed INSERT/DELETE forward, idempotently.
  3. **UNDO** uncommitted INSERT/DELETE backward, idempotently.
  4. Checkpoint: flush the consistent data file, truncate the log.
- **Demo:** `tests/test_recovery.cpp` forks a child that writes committed and
  uncommitted data then `_exit`s (a real crash, no flush); the parent reopens and
  verifies committed rows survived and uncommitted ones were rolled back.

---

## 9. Extension Track — C (LSM-tree)

**Motivation.** The B+ tree heap updates pages in place (random writes). An
LSM-tree turns writes into sequential appends, trading some read amplification
for much higher write throughput — the opposite trade-off, worth studying.

**Design.**
- **MemTable** — sorted in-memory map; absorbs all writes; flushed to an SSTable
  past a size threshold.
- **SSTable** — immutable sorted on-disk run with a **Bloom filter** and a key
  index; reads check the Bloom filter first to skip files that can't hold a key.
- **Tombstones** — deletes write a marker; reads honour it, compaction drops it.
- **Compaction** — size-tiered: merge accumulated SSTables into one, keeping the
  newest version of each key and discarding tombstones.
- **Integration** — `LSMRowStore` implements the same `RowStore` interface as the
  heap store, so the parser, optimizer, and executor run over it unchanged;
  selected per table with `CREATE TABLE ... USING LSM`.

**Results.** See §10 and `benchmarks/results.md`.

---

## 10. Benchmarks

Full setup, results, and analysis are in **`benchmarks/results.md`**
(reproduce with `make bench`). Headline (N = 20,000, `-O2`, Apple Silicon):

| Metric | B+ tree heap | LSM | Winner |
|--------|-------------:|----:|--------|
| Write throughput (rows/sec) | 21,292 | **304,052** | **LSM ~14×** |
| Point read — hit (µs)  | **0.36** | 1.81 | **B+ tree ~5×** |
| Point read — miss (µs) | 0.30 | **0.03** | **LSM ~10×** |
| Disk bytes after load | 1,495,040 | **1,033,947** | LSM |
| Disk bytes after deleting half | 1,495,040 | 1,176,475 | LSM (compaction) |

The LSM store delivers far higher **write throughput** (writes are buffered in
the MemTable and flushed sequentially instead of updating B+ tree pages in
place); the B+ tree wins **point-read hits** (one logarithmic descent vs LSM
read amplification across SSTables); **Bloom filters** make LSM point-**misses**
nearly free; and LSM **compaction** reclaims the space left by deleted keys
(the heap file never shrinks). See `results.md` for the full discussion.

---

## 11. Limitations

- B+ tree deletes don't merge under-full nodes (searches stay correct).
- Engine-level locking covers write operations; full predicate/scan locking is
  not implemented — concurrency is demonstrated at the LockManager layer.
- LSM uses a dense in-memory index per SSTable (real systems use sparse) and
  full (not levelled) compaction.
- Types are limited to `INTEGER` and `VARCHAR`; no NULLs, `UPDATE`, `GROUP BY`,
  or aggregates beyond `COUNT(*)`; one data file per database.

## Future improvements
- Levelled LSM compaction; sparse SSTable index; `UPDATE`/`GROUP BY`; multi-table
  query optimization with predicate pushdown; richer types.

---

## 12. How to Run

**Dependencies:** a C++17 compiler (`clang++`/`g++`) and `make`. No third-party
libraries.

```bash
cd MiniDB_Projects/Team_<TEAM_NAME>

make             # build the ./minidb SQL shell
make run-tests   # build + run the full test suite (8 suites)
make bench       # LSM vs B+ tree benchmark
./minidb mydb    # start the shell on database "mydb"
./minidb /tmp/d < demo.sql   # run the scripted demo
```

**Example session:**

```sql
CREATE TABLE users (id INTEGER PRIMARY KEY, name VARCHAR, age INTEGER);
INSERT INTO users VALUES (1, 'alice', 30);
.explain on
SELECT name FROM users WHERE id = 1;     -- IndexScan
CREATE TABLE events (id INTEGER PRIMARY KEY, kind VARCHAR) USING LSM;
SELECT users.name, events.kind FROM users JOIN events ON users.id = events.id;
```

See `docs/DESIGN.md` for the internals.
