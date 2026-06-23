# MiniDB - Team_LSMCrafters

A small relational database engine written in C++17, built for the
Advanced DBMS capstone. It integrates a page-based storage engine, a B+Tree
index, SQL query execution, a cost-based optimizer, transactions with two-phase
locking, and write-ahead-log crash recovery. Our extension is **Track C
(Modern Storage)**: an LSM-tree storage engine that plugs in behind the same
storage interface, with a benchmark comparing it against the default B+Tree/heap
store.


## Team

| Name | Roll Number | Email |
|---|---|---|
| Nishant Dasgupta | 24BCS10451 | nishant.24bcs10451@sst.scaler.com
 |
| Kartik Pettugani | 24BCS10418 | Veera.24BCS10418@sst.scaler.com 
 |
| NEERASA VEDA VARSHIT | 24bcs10005 | neerasa.24bcs10005@sst.scaler.com |


---

## 1. Project Overview

**Problem.** Across the course we built individual database components (buffer
pool, B-tree, locking, etc.). The capstone integrates them into one working
engine and adds a chosen extension.

**Goals.** Implement and integrate every required feature - storage engine,
B+Tree index, SQL (SELECT/WHERE/JOIN/INSERT/DELETE), a cost-based optimizer,
serializable transactions via 2PL with deadlock detection, and WAL crash
recovery - at a clean, demonstrable minimum, then add an extension.

**Chosen extension - Track C (Modern Storage):** an LSM-tree engine (MemTable +
SSTables + compaction) that implements the same storage interface as the default
row-store, plus a benchmark comparing the two on write throughput, read latency,
and space.

---

## 2. System Architecture

```
                          SQL text
                             |
            +----------------v-----------------+
            |  sql/   Lexer -> Parser -> AST    |
            +----------------+-----------------+
                             | Statement (SELECT/INSERT/DELETE)
            +----------------v-----------------+
            | plan/   Optimizer (cost-based)    |  reads TableStats
            +----------------+-----------------+
                             | physical operator tree
            +----------------v-----------------+
            | exec/   Volcano operators          |
            |  SeqScan IndexScan Filter Join     |
            |  Project Insert Delete             |
            +----------------+-----------------+
                             | insert/get/erase/scan (Key, Bytes)
            +----------------v-----------------+
            | storage/  StorageEngine interface  |
            |     +----------+    +-----------+  |
            |     | HeapTable |    | LsmTable  |  |  <- Track C
            |     | heap +    |    | memtable+ |  |
            |     | B+Tree    |    | SSTables  |  |
            +-----+----+------+----+-----+-----+--+
                       | pages              | files
            +----------v---------+   +------v---------+
            | BufferPool          |   | SSTables on    |
            | (clock-sweep)       |   | disk + manifest|
            +----------+---------+   +----------------+
                       | read/write page
            +----------v---------+
            | DiskManager (1 file)|
            +--------------------+

   txn/  LockManager (2PL) + LogManager (WAL) + RecoveryManager
         wrap the heap engine for serializable transactions and crash recovery.
```

**Major modules (`src/`):** `common` (shared types), `storage` (pages, slotted
pages, tuple codec, disk manager, buffer pool, heap file, the `StorageEngine`
interface, `HeapTable`), `index` (B+Tree), `catalog`, `sql` (lexer/parser/AST),
`plan` (optimizer), `exec` (operators + executor), `txn` (locking, WAL,
recovery), `lsm` (Track C), `demo` (transaction and recovery demos).

**Data flow (a SELECT):** SQL string -> tokens -> AST -> the optimizer builds a
physical operator tree -> the executor pulls rows operator-by-operator -> scans
read serialized rows from a `StorageEngine` -> the buffer pool serves the pages
from cache or disk.

---

## 3. Storage Layer

- **Pages** ([page.h](src/storage/page.h)) are a fixed 4096 bytes - the unit of
  disk I/O. Each heap page begins with a `PageHeader` (next-page link, slot
  count, free pointer, and two LSNs for recovery).
- **Slotted pages** ([slotted_page.cpp](src/storage/slotted_page.cpp)) store
  variable-length tuples: a slot directory grows forward from the header while
  tuple bytes grow backward from the end. A deleted slot keeps its place as a
  tombstone (length 0) so a row's RID never moves.
- **Tuple codec** ([tuple.cpp](src/storage/tuple.cpp)) serializes a row to bytes
  (INT = 8 bytes, TEXT = 2-byte length + characters). Types are INT and TEXT.
- **DiskManager** ([disk_manager.cpp](src/storage/disk_manager.cpp)) is the only
  code that touches disk: page N lives at byte `N * 4096`; allocation appends a
  zeroed page.
- **Buffer pool** ([buffer_pool.cpp](src/storage/buffer_pool.cpp)) caches 16
  pages and evicts with **clock-sweep** (a usage counter approximating LRU).
  Callers pin a page on fetch and unpin when done; pinned pages are never
  evicted. It tracks hits / misses / evictions (visible via `:stats`).
- **Heap file** ([heap_file.cpp](src/storage/heap_file.cpp)) stores a table as a
  linked chain of pages; inserts append to the last page and allocate a new one
  when it is full.

---

## 4. Indexing

- **B+Tree** ([bplus_tree.cpp](src/index/bplus_tree.cpp)) maps a primary key to a
  row's RID. Internal nodes route searches; all keys live in leaves, which are
  linked left-to-right for range scans.
- **Node structure:** each node holds a sorted key vector; internal nodes also
  hold child pointers, leaves hold parallel RIDs and a `next` leaf pointer.
- **Search path:** descend from the root, taking the child for the first
  separator greater than the key (equal keys route right), down to a leaf, then
  binary-search the leaf.
- **Insert** splits a full node and copies the median up (a true B+Tree, so
  every key stays in a leaf). **Delete** removes the key from its leaf (lazy: no
  rebalancing, which keeps it simple and still correct for search/range).
- It is an in-memory index rebuilt from a heap scan when a table opens, so the
  payload is a real RID and an IndexScan fetches real heap pages through the
  buffer pool - that is the index utilization shown in the demo.

---

## 5. Query Execution

- **Lexer / Parser** ([lexer.cpp](src/sql/lexer.cpp),
  [parser.cpp](src/sql/parser.cpp)) - a recursive-descent parser turns SQL into a
  typed AST. Expression precedence (OR < AND < comparison) is encoded by the call
  chain, so there is no separate precedence table.
- **Plan generation.** The optimizer ([optimizer.cpp](src/plan/optimizer.cpp))
  turns the AST into a physical operator tree: a `SELECT` becomes
  `Project -> Filter -> (SeqScan | IndexScan | NestedLoopJoin)`, while `INSERT` /
  `DELETE` map to the matching write operator. *Which* scan and join order to use
  is the optimizer's cost decision - see section 6.
- **Physical operators** ([operators.cpp](src/exec/operators.cpp)) use the
  Volcano model (`open` / `next` / `close`): `SeqScan`, `IndexScan`, `Filter`,
  `NestedLoopJoin`, `Project`, plus insert/delete handled by the executor. Each
  operator pulls rows from its child one at a time.
- **Executor** ([executor.cpp](src/exec/executor.cpp)) runs a statement end to
  end and returns rows (SELECT) or a status (INSERT/DELETE). `EXPLAIN <query>`
  prints the chosen physical plan instead of running it.

---

## 6. Optimizer

[optimizer.cpp](src/plan/optimizer.cpp) is cost-based in two concrete ways:

- **Selectivity + scan choice.** For a predicate on the primary key it estimates
  selectivity from `TableStats` (`= c` -> `1/row_count`; a range -> its fraction
  of `[min_key, max_key]`). It compares `cost(SeqScan) = row_count` against
  `cost(IndexScan) = selectivity * row_count + log2(row_count)` and picks the
  cheaper. So `WHERE id = 42` -> IndexScan, `WHERE age > 0` -> SeqScan,
  `WHERE id >= 10 AND id <= 15` -> IndexScan range. (Verify with `EXPLAIN`.)
- **Join ordering.** For a two-table join it makes the table with fewer rows the
  outer side of the nested-loop join, minimizing inner rescans.

---

## 7. Transactions & Concurrency

Scoped to the heap engine (`src/txn/`).

- **Locking strategy** ([lock_manager.cpp](src/txn/lock_manager.cpp)): row-level
  shared/exclusive locks under **strict two-phase locking**. Shared locks are
  mutually compatible; exclusive conflicts with everything. `release_all` is the
  only way to release locks and is called exactly once at commit/abort, so a
  transaction structurally cannot release early.
- **Isolation guarantee:** serializable - every read takes S, every write takes
  X, nobody releases early, so schedules are conflict-serializable.
- **Deadlock handling:** a waits-for graph with DFS cycle detection. A request
  that would close a cycle throws `TxnAbortException`; that transaction is the
  victim and aborts. (`src/demo/txn_demo.cpp` shows blocking and a resolved
  deadlock.)

---

## 8. Recovery

- **WAL design** ([wal.cpp](src/txn/wal.cpp)): an append-only log of records
  carrying a transaction's before- and after-images per key. Two rules give
  durability: the log record is forced to disk **before** the data change
  (write-ahead), and the COMMIT record is forced before commit returns.
- **Log records:** `BEGIN / INSERT / DELETE / COMMIT / ABORT`, each with the key
  plus before/after bytes, so redo and undo are simple, idempotent operations.
- **Crash recovery** ([recovery.cpp](src/txn/recovery.cpp)): *analysis* finds
  committed transactions (those with a COMMIT record), *redo* replays every
  committed operation forward, *undo* reverses uncommitted operations. Because
  the operations are logical and idempotent, recovery is correct no matter how
  much data reached disk. `src/demo/recovery_demo.cpp` commits one transaction,
  leaves another uncommitted, drops the buffer pool without flushing (the crash),
  then recovers - the committed rows survive and the uncommitted one is gone.

---

## 9. Extension Track C - LSM Storage

**Motivation.** B-tree storage updates pages in place; LSM trees turn writes into
in-memory inserts plus sequential flushes, trading read and space cost for write
simplicity. We implement one behind the same `StorageEngine` interface so the
rest of the system is unchanged.

**Design (`src/lsm/`):**
- **MemTable** - a sorted `std::map` of key -> versioned value; an update or
  delete is just a newer entry (a delete is a tombstone).
- **SSTables** - when the MemTable fills it is flushed to an immutable, sorted
  file with a sparse index and a footer (written to a `.tmp` then atomically
  renamed). See [docs/sstable_format.md](docs/sstable_format.md).
- **Reads** check the MemTable, then SSTables newest-first; the first hit wins
  (a tombstone means "absent").
- **Compaction** ([compactor.cpp](src/lsm/compactor.cpp)) merges all SSTables
  into one via a k-way, newest-wins merge ([merge_iterator.cpp](src/lsm/merge_iterator.cpp)),
  dropping overwritten values and tombstones - which reclaims space.
- **Durability** - a small WAL replays the active MemTable on reopen; a manifest
  file lists the live SSTables.

**Results (summary).** On an identical 50,000-key workload: the B+Tree wins
point-read latency (the LSM pays **read amplification**, probing several
SSTables); the LSM wins **space** after updates (compaction reclaims what the
heap leaves as dead rows); and for **durable** writes the LSM writes about 36x
less data than an in-place heap - the classic LSM write win. Full setup, tables,
and analysis are in section 10.

---

## 10. Benchmarks

`minidb_bench` drives both engines through the same `StorageEngine` interface on
identical data (50,000 random keys, 100-byte rows). A correctness cross-check
confirms both engines agree before any timing. We run two scenarios because
"who wins on writes" depends entirely on the durability model (see the Writes
note below).

**Scenario A - buffered, non-durable** (load -> 20,000 point reads -> 25,000
updates -> compact). Representative Release run; numbers vary by run/machine:

| Metric | B+Tree (heap + index) | LSM |
|---|---|---|
| Load throughput | ~1,040,000 ins/s | ~75,000 ins/s |
| Update throughput | ~172,000 ins/s | ~64,000 ins/s |
| Read latency (hit) | 2.6 us | 115 us |
| Read latency (miss) | 0.05 us | 0.01 us |
| Disk before / after compaction | 5556 / 8336 KB | 5920 / 6360 KB |
| Logical data | 5273 KB | 5273 KB |

**Scenario B - per-write durability.** Each insert must be made durable before
the next. The heap uses a **FORCE policy** (persist the dirty page after every
insert), the model the textbook "LSM wins writes" result assumes. The LSM gets a
MemTable large enough that no flush/compaction happens during the run, so each
insert is a MemTable put plus one sequential WAL append:

| Metric | heap (FORCE) | LSM (WAL) |
|---|---|---|
| Durable-write throughput | ~387,000 ins/s | **~463,000 ins/s** |
| Bytes written per row | 4,323 B/row | **121 B/row** |
| Total physical writes | 211 MB | **5.8 MB** |

**Analysis (honest, with reasoning):**
- **Reads (A):** the B+Tree is far faster on hits (one in-memory index lookup +
  one page fetch) than the LSM, which probes several SSTables with no block cache
  - this is **read amplification**, the classic LSM cost. (Missing keys are fast
  for both: the B+Tree fails in the index; the LSM prunes by each SSTable's
  min/max key range.)
- **Space (A):** after 25k updates the heap **bloats to 8.3 MB** because
  deleted/old rows are tombstoned but never reclaimed (no vacuum), while LSM
  **compaction reclaims** space (6.4 MB). This is the LSM space-efficiency win.
- **Writes - why the scenario matters:** in Scenario A the B+Tree looks *faster*,
  which deviates from "LSM wins writes". That is because our heap is itself
  append-structured (sequential appends) with an **in-memory** index, so it never
  pays the random in-place write penalty that motivates LSMs - while the LSM pays
  flush + compaction on the same thread. Once we make writes **durable**
  (Scenario B), the textbook result appears: a FORCE-policy heap must write a
  whole 4 KB page to persist one 100-byte row (**~36x write amplification**),
  while the LSM appends only the row to a sequential log (121 B/row). The LSM
  writes **~36x less data** and is faster. This is the LSM's real write win:
  turning durable random/in-place writes into cheap sequential log appends.

CSV output: `benchmarks/results/storage_bench.csv`.

---

## 11. Limitations

**Missing features.**
- No UPDATE statement (model it as DELETE + INSERT) and no DDL (tables are seeded
  in `main.cpp`); types are INT and TEXT only.
- One primary-key index per table (no secondary index); B+Tree delete is lazy (no
  rebalancing).
- Joins are nested-loop only, two tables max; no GROUP BY / ORDER BY / aggregates.

**Scalability limits.**
- The B+Tree index is in-memory and rebuilt by a full heap scan on open, so a
  table's keys must fit in RAM and open time grows with the table.
- Single-process, single data file; transactions/WAL/recovery cover the heap
  engine only (the LSM has its own WAL + manifest but is not wrapped by 2PL).
- The heap has no vacuum, so updates/deletes leave dead space, and recovery scans
  the whole WAL (no checkpoint).
- LSM compaction is synchronous (no background thread), so it pauses writes.

**Future improvements.**
- A paged, on-disk B+Tree, so the index is not memory-bound and is logged rather
  than rebuilt on open.
- Leveled compaction and a per-SSTable Bloom filter to speed LSM missing-key reads.
- Background compaction, a WAL checkpoint, and extending transactions to cover the
  LSM engine.

---

## 12. How to Run

**Dependencies:** a C++17 compiler and CMake 3.16+. No external libraries.

**Build:**
```
cd MiniDB_Projects/Team_LSMCrafters
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Interactive SQL shell:**
```
./build/minidb_cli
```
Example session:
```
EXPLAIN SELECT name FROM students WHERE id = 42      -- IndexScan
EXPLAIN SELECT name FROM students WHERE age > 20      -- SeqScan
SELECT students.name, enrollments.course
  FROM students JOIN enrollments ON students.id = enrollments.student_id
  WHERE students.id = 5
INSERT INTO students VALUES (9001, 'late_joiner', 25)
DELETE FROM students WHERE id = 9001
:stats        -- buffer-pool hits / misses / evictions
:quit
```

**Demonstrations and benchmark:**
```
./build/txn_demo        # 2PL blocking + deadlock detection
./build/recovery_demo   # WAL crash recovery
./build/minidb_bench    # LSM vs B+Tree (writes results CSV)
```

**Tests:**
```
ctest --test-dir build --output-on-failure
```
