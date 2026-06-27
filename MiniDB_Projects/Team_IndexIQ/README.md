# MiniDB — Team IndexIQ

**Branch:** TEAM_IndexIQ | **Extension:** Track B — Concurrency (MVCC)


| Name                | Roll Number | Scaler Email                                                                |
| ------------------- | ----------- | --------------------------------------------------------------------------- |
| Hariny Patel        | 10407       | [hariny.24bcs10407@sst.scaler.com](mailto:hariny.24bcs10407@sst.scaler.com) |
| Akshansh Sinha      | 10468       | [akshansh.24bcs10468@sst.scaler.com](mailto:akshansh.24bcs10468@sst.scaler.com) |
| Pratham Onkar Singh | 10136       | [pratham.24bcs10136@sst.scaler.com](mailto:pratham.24bcs10136@sst.scaler.com) |


---

## Architecture

```
SQL string
  → Lexer → Token list
  → Parser → AST (Statement variant)
  → Database::execute()
       ├── Optimizer  → QueryPlan (INDEX_SCAN or TABLE_SCAN)
       ├── Executor   → vector<Row>
       ├── WAL        → wal.log (append-only, text)
       └── TxnManager → 2PL + MVCC
                         └── HeapFile ←→ BufferPool
                                          └── B+ Tree (index)
```

**Storage layer:** Each table is one binary file (`data/{table}.db`) of 4096-byte pages. Records are fixed-width (INT = 4 bytes, TEXT = 256 bytes). Each slot has a 1-byte live flag.

**Buffer pool:** LRU-evict cache (64 pages max). Keyed on `(file, page_id)` so multiple tables never collide.

**B+ tree:** Order-4, in-memory, rebuilt from a heap scan on startup. Leaf nodes form a linked list for range scans. Primary key always first column, always INT.

**Optimizer:** If `WHERE pk = <int>` → INDEX_SCAN. Everything else → TABLE_SCAN. JOINs always use nested-loop over two TABLE_SCANs.

**WAL:** Text log at `data/wal.log`. Format: `LSN|TXN|OP|TABLE|KEY|VALUE`. On startup, redo all INSERT/DELETE ops from committed transactions (idempotent).

**Transaction manager:** Strict Two-Phase Locking (2PL). Lock table per resource. Deadlock detection via DFS on the waits-for graph. MVCC snapshot isolation via per-transaction `xmin` snapshots and version chains (in-memory).

---

## Build

Requires g++with C++17 and pthreads.

```bash
mkdir -p build

# REPL
g++ -std=c++17 -Isrc -O2 \
  src/storage/heap_file.cpp src/storage/buffer_pool.cpp \
  src/index/btree.cpp src/catalog/catalog.cpp \
  src/parser/lexer.cpp src/parser/parser.cpp \
  src/recovery/wal.cpp src/transaction/txn_manager.cpp \
  src/optimizer/optimizer.cpp src/executor/executor.cpp \
  src/db.cpp src/main.cpp -lpthread -o build/minidb

# Concurrent transaction demo
g++ -std=c++17 -Isrc -O2 \
  src/storage/heap_file.cpp src/storage/buffer_pool.cpp \
  src/index/btree.cpp src/catalog/catalog.cpp \
  src/parser/lexer.cpp src/parser/parser.cpp \
  src/recovery/wal.cpp src/transaction/txn_manager.cpp \
  src/optimizer/optimizer.cpp src/executor/executor.cpp \
  src/db.cpp demo/demo.cpp -lpthread -o build/minidb_demo

# Benchmark
g++ -std=c++17 -Isrc -O2 \
  src/transaction/txn_manager.cpp benchmarks/bench.cpp \
  -lpthread -o build/minidb_bench
```

---

## Usage

```bash
./build/minidb [data_dir]   # default: ./data
```

Supported SQL:

```sql
CREATE TABLE students (id INT, name TEXT, grade INT)
INSERT INTO students VALUES (1, Alice, 90)
SELECT * FROM students
SELECT * FROM students WHERE id = 1
SELECT * FROM students WHERE grade = 75
SELECT * FROM students JOIN courses ON students.id = courses.id
EXPLAIN SELECT * FROM students WHERE id = 1
DELETE FROM students WHERE id = 2
BEGIN
INSERT INTO students VALUES (4, Dave, 95)
COMMIT
```

---

## Demo Script (Viva)

```sql
-- 1. Setup
CREATE TABLE students (id INT, name TEXT, grade INT)
CREATE TABLE courses (id INT, title TEXT)
INSERT INTO students VALUES (1, Alice, 90)
INSERT INTO students VALUES (2, Bob, 75)
INSERT INTO students VALUES (3, Carol, 88)
INSERT INTO courses VALUES (1, DBMS)
INSERT INTO courses VALUES (2, OS)

-- 2. Cost-based optimizer: index scan vs table scan
EXPLAIN SELECT * FROM students WHERE id = 1
-- INDEX SCAN on students using id = 1
SELECT * FROM students WHERE id = 1

EXPLAIN SELECT * FROM students WHERE grade = 75
-- TABLE SCAN on students (no index on grade)
SELECT * FROM students WHERE grade = 75

-- 3. JOIN (nested-loop)
SELECT * FROM students JOIN courses ON students.id = courses.id

-- 4. Multi-statement transaction
BEGIN
INSERT INTO students VALUES (4, Dave, 95)
COMMIT
SELECT * FROM students

-- 5. Crash recovery: kill process (Ctrl+C), restart ./build/minidb data
-- Data survives via WAL redo
```

---

## Track B: MVCC vs 2PL Benchmark

```bash
./build/minidb_bench
```

**Results:**

```
2PL reads:  76,020   (readers block behind writer's exclusive lock)
MVCC reads: 1,215,912
MVCC speedup: ~16x
```

**Why MVCC wins:** With 2PL, every read must acquire a shared lock. While a writer holds an exclusive lock for 10 ms, readers queue up and block. With MVCC, readers take a snapshot at transaction start and read committed versions — no lock needed, no blocking.

---

## Concurrent Transaction Demo

```bash
./build/minidb_demo
```

Three scenarios:

1. **2PL blocking:** Reader waits while writer holds exclusive lock, unblocks on commit.
2. **MVCC snapshot:** Reader sees pre-commit value (100); new reader after commit sees updated value (999).
3. **Deadlock detection:** T1 holds A wants B, T2 holds B wants A → DFS finds cycle → one transaction aborted.

---

## Design Decisions


| Question                 | Decision                               | Reason                                                          |
| ------------------------ | -------------------------------------- | --------------------------------------------------------------- |
| Variable-length records? | No — fixed (INT=4, TEXT=256)           | Simplifies slot addressing to `page_id * slots_per_page + slot` |
| B+ tree on disk?         | No — rebuilt from heap scan on startup | Avoids serialization complexity                                 |
| Row-level locking?       | No — table-level                       | Simpler; still demonstrates 2PL semantics                       |                                                          |
| MVCC on disk?            | No — in-memory only                    | REPL uses disk; MVCC demo uses in-memory version chains         |
| WAL format?              | Text, one line per record              | Human-readable; easy to inspect in viva                         |


---

## File Map

```
src/storage/page.h              Page struct (4096 bytes + dirty flag)
src/storage/heap_file.*         Binary file I/O (read/write/alloc page)
src/storage/buffer_pool.*       Page cache (keyed on file+page_id)
src/index/btree.*               B+ tree (order 4, int keys, lazy delete)
src/catalog/catalog.*           Schema load/save, encode/decode records
src/parser/types.h              Token enum
src/parser/lexer.*              Tokenizer
src/parser/ast.h                AST node types
src/parser/parser.*             Recursive descent SQL parser
src/recovery/wal.*              Write-ahead log
src/transaction/txn_manager.*   2PL + MVCC + deadlock detection
src/optimizer/optimizer.*       Cost-based plan selection
src/executor/executor.*         Query execution engine
src/db.*                        Database facade (ties all modules together)
src/main.cpp                    REPL
demo/demo.cpp                   Concurrent transaction demo
benchmarks/bench.cpp            2PL vs MVCC read throughput benchmark
```

