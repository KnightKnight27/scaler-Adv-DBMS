# MiniDB — Advanced DBMS Capstone Project

**Team Name:** Team_MutexNChill  
**Extension Track:** Track B — Concurrency (MVCC)  
**Submission Deadline:** 23 June 2026

---

## Team Members

| Full Name | Scaler Email | Roll Number |
|---|---|---|
| Mohammed Abdur Rahman | mohammed.24bcs10130@sst.scaler.com | 24BCS10130 |
| Mahir Abidi | mahir.24bcs10125@sst.scaler.com | 24BCS10125 |
| Varun Mundada | varun.24bcs10326@sst.scaler.com | 24BCS10326 |
| Syed Farzeen Ahmad | syed.24bcs10228@sst.scaler.com | 24BCS10228 |

---

## 1. Project Overview

### Problem Statement

Building a database engine is one of the hardest systems engineering challenges: every layer (storage, indexing, query execution, concurrency, recovery) must work correctly both in isolation and when integrated. Most students learn these concepts independently through labs but never see how they connect. This project bridges that gap — we build a complete, working relational database from scratch, integrating all five layers into a single runnable system.

### Goals

- Build a working database engine from foundational components covered across all labs
- Integrate storage, indexing, query processing, transactions, and recovery into one system
- Demonstrate understanding of the internals through a live demo and benchmark
- Make sound engineering trade-offs and be able to justify them

### Chosen Extension Track

**Track B — Concurrency (MVCC):** replace 2PL read locks with Multi-Version Concurrency Control so readers never block writers. Benchmark demonstrates ~26× higher read throughput under a read-heavy workload with contention.

---

## 2. System Architecture

```
SQL String
    │
    ▼
 Lexer  ──► tokens
    │
    ▼
 Parser ──► AST (SelectStmt / InsertStmt / DeleteStmt)
    │
    ▼
Optimizer ──► QueryPlan  (SeqScan or IndexScan, join order)
    │
    ▼
Executor
 ├── reads/writes → HeapFile (via BufferPool → PageManager → disk)
 ├── lookups      → BPlusTree (in-memory primary-key index)
 ├── locks        → TransactionManager (2PL + deadlock detection)
 └── changes      → WalManager (WAL log → crash recovery)
```

**Module map:**
```
src/
├── storage/      page layout, page manager (disk I/O), buffer pool, heap file
├── index/        B+ Tree
├── catalog/      table registry + per-column statistics
├── query/        lexer, parser, executor
├── optimizer/    cost-based optimizer
├── txn/          transaction manager (2PL + deadlock detection)
└── wal/          Write-Ahead Log + crash recovery

benchmarks/       MVCC vs 2PL throughput comparison (Track B)
```

---

## 3. Storage Layer

### Page Format

Every page is exactly 4096 bytes (matching the OS page size to avoid split I/O):

```
[ PageHeader (8 bytes) | Row[0] | Row[1] | ... | Row[N-1] ]
```

`PageHeader`: `page_id (int)` + `num_rows (int)`  
`Row`: `id (int)` + `name (char[32])` + `age (int)` + `extra (int)` + `is_valid (bool)` = ~48 bytes  
Rows per page ≈ 85

### Heap File

Each table is stored in a separate binary file (`tablename.db`). The `HeapFile` class provides:
- `insertRow(row)` — finds a page with a free slot, writes the row, returns a `RID {page_id, slot}`
- `deleteRow(rid)` — sets `is_valid = false` (tombstone)
- `scanAll()` — iterates all pages, returns valid rows

### Buffer Pool

A fixed pool of 10 frames sits between the heap file and disk. Eviction policy: **Clock Sweep** (same algorithm PostgreSQL uses). The clock hand sweeps frames, decrementing `usage_count`; the first frame that reaches 0 and is not pinned is evicted.

**Trade-off vs LRU:** Clock Sweep is O(1) per eviction decision. LRU is more accurate but requires updating a sorted structure on every access.

---

## 4. B+ Tree Indexing

Each table has a primary-key B+ Tree index built in memory (`src/index/bplus_tree.h`).

**B+ Tree vs plain B-Tree:**
- In a B-Tree: data (key + record) lives in every node
- In a B+ Tree: data lives **only in leaf nodes**; internal nodes hold only routing keys
- Leaf nodes are linked in a chain → efficient range scans

**Parameters:** minimum degree t=2, so max 3 keys per node.

**Insert:** recursive. If a leaf overflows (> 3 keys), it splits. The first key of the new right leaf is *copied up* to the parent (not removed from the leaf). If an internal node overflows, its middle key is *pushed up* and removed.

**Delete:** removes the key from the leaf. Rebalancing (borrowing / merging) is omitted at this scale; correctness is preserved because findLeaf still routes correctly.

**Operations:** `search(key)` → O(log n), `rangeSearch(lo, hi)` → O(log n + k), `insert(key, rid)` → O(log n), `remove(key)` → O(log n)

---

## 5. SQL Query Execution

Supported SQL:
```sql
SELECT col1, col2 FROM table [JOIN other ON t1.col = t2.col] [WHERE expr]
INSERT INTO table VALUES (id, 'name', age, extra)
DELETE FROM table WHERE expr
```

WHERE expressions support: `=`, `!=`, `<`, `>`, `<=`, `>=`, `AND`, `OR`.

### Parser

A hand-written recursive-descent parser (`src/query/parser.cpp`). It reads the token stream produced by the Lexer and builds an Abstract Syntax Tree (AST):
- `SelectStmt` — holds column list, table name, optional JOIN clause, optional WHERE expression
- `InsertStmt` — holds table name and four column values
- `DeleteStmt` — holds table name and WHERE expression

The expression grammar is: `expr → and_expr (OR and_expr)*` → `comparison (AND comparison)*` → `primary op primary`.

### Query Plan Generation

After parsing, the `Optimizer` reads table statistics from the `Catalog` and returns a `QueryPlan` that specifies:
- Which scan type to use: `SeqScan` (read all pages) or `IndexScan` (use B+ Tree)
- For a JOIN: which table is the outer (driving) loop and which is the inner loop

The plan is passed to the Executor along with the parsed statement.

### Operator Execution

The `Executor` (`src/query/executor.cpp`) implements three operators:
- **Filter** — for each row, evaluate the WHERE expression recursively against the row's column values
- **NestedLoopJoin** — for each outer row, scan all inner rows and emit pairs where the ON condition holds
- **Project** — pick only the requested columns from the result rows for output

---

## 6. Cost-Based Optimizer

The optimizer reads per-table statistics from the Catalog and decides between two scan strategies:

| Strategy | When chosen |
|---|---|
| **IndexScan** | WHERE has equality (`id = x`) or low selectivity (< 20%) on the `id` column |
| **SeqScan** | All other cases |

**Selectivity estimation:**
- `id = v` → `1 / distinct_count`
- `id > v` → `(max - v) / (max - min)`
- `AND` → `s1 × s2`; `OR` → `s1 + s2 - s1×s2`

**Join order:** given two tables, the one with fewer rows becomes the outer (driving) loop. This minimises total comparisons: `outer × inner` is smallest when outer is smaller.

---

## 7. Transactions & Concurrency

**Isolation guarantee:** because strict 2PL holds every lock until the transaction ends, no transaction can read another's uncommitted writes, and any conflicting pair of transactions is forced into a serial order. MiniDB therefore provides **serializable** isolation.

**Strict Two-Phase Locking (2PL):**
- Growing phase: acquire SHARED (read) or EXCLUSIVE (write) locks
- Shrinking phase: all locks released at once on `commit()` or `abort()`

**Lock table:** keyed by `"table:row_id"`. Multiple transactions can share a SHARED lock; EXCLUSIVE requires sole ownership.

**Deadlock detection:** a *waits-for graph* tracks which transaction is waiting for which. On every lock request that encounters a conflict, we walk the waits-for chain (DFS). If we return to the starting transaction, there is a cycle → `DeadlockException` is thrown and the requesting transaction is aborted.

---

## 8. Recovery (WAL)

**Write-Ahead Log rule:** every change is appended to the log file *before* the page is modified. This ensures that after a crash we can reconstruct committed state.

**Log format** (plain text, human-readable):
```
BEGIN   1
INSERT  1  students  3  Charlie  22  1
COMMIT  1
BEGIN   2
INSERT  2  students  4  Diana  25  2
(crash — no COMMIT)
```

**Recovery algorithm (redo-only):**
1. Pass 1 — scan log, collect all committed transaction IDs
2. Pass 2 — for every INSERT/DELETE in a committed transaction, redo it if not already present

**Crash simulation:** `simulateCrash()` truncates the last log record to mimic a mid-write crash.

---

## 9. Extension Track — Track B: MVCC

**Motivation:** under strict 2PL, readers acquire SHARED locks. When a writer holds an EXCLUSIVE lock (even for 5ms), all readers block. In a read-heavy workload this is a bottleneck.

**MVCC solution:**
- Each row has a *version*. When a writer commits, it increments `committed_version`.
- Readers take a *snapshot* of `committed_version` at the moment they begin. They read that version — they never block on a write.
- Writers still acquire exclusive locks (no lost updates).

**Result (from benchmark, 10 readers + 1 writer, 500ms):**

| Mode | Reads/sec |
|---|---|
| 2PL | ~900K |
| MVCC | ~24M |
| **Speedup** | **~26×** |

**Why the gap is so large:** the writer holds the exclusive lock for 5ms every 6ms. In 2PL, readers are blocked 83% of the time. In MVCC, readers are never blocked.

---

## 10. Benchmarks

| Experiment | Setup | Result |
|---|---|---|
| 2PL read throughput | 10 readers, 1 writer (5ms lock hold), 500ms | ~900K reads/sec |
| MVCC read throughput | Same setup | ~24M reads/sec |
| MVCC speedup | Ratio | ~26× |

**Analysis:** the improvement is proportional to how long the writer holds the lock relative to the read operation time. For heavier writes, the speedup would be even larger.

---

## 11. Limitations

- Single fixed Row schema (5 fields). A production DB would have a dynamic schema/type system.
- B+ Tree is in-memory; it is not persisted to disk. On restart the index is rebuilt from the heap file via the WAL.
- Delete does not rebalance the B+ Tree (no leaf merging). Correct but not space-optimal.
- No UPDATE statement (can be achieved as DELETE + INSERT).
- No multi-table CREATE TABLE DDL; tables are registered in code.
- Recovery is redo-only. Undo (for uncommitted transactions that modified pages before the crash) is not implemented.
- Buffer pool size is fixed at 10 frames.

---

## 12. How to Run

### Dependencies

- C++17 compiler (clang++ / g++)
- CMake ≥ 3.14

### Build

```bash
cd MiniDB_Projects/Team_MutexNChill
mkdir build && cd build
cmake ..
make
```

### Run the demo

```bash
./src/minidb
```

Demonstrates all five components end-to-end: storage, B+ Tree, SQL, transactions, WAL.

### Run the MVCC benchmark (Track B)

```bash
./benchmarks/minidb_bench
```

Prints 2PL vs MVCC read throughput and the speedup ratio.

### Clean generated files

```bash
rm -f *.db minidb.wal
```
