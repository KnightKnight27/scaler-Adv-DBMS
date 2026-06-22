# Team SoloEngine — Relational Database Engine (C++17)

---

## Team

| Name | Roll Number | Email |
|------|-------------|-------|
| Talin Daga | 24bcs10321 | talin.24bcs10321@sst.scaler.com |
| Ayaan Khan | 23bcs10029 | ayaan.23bcs10029@sst.scaler.com |
| Shifa M | 24bcs10354 | shifa.24bcs10354@sst.scaler.com |

**Extension Track:** Track A — Performance (Vectorized Execution)

---

## Project Overview

Team SoloEngine is a from-scratch relational database engine written in C++17.
It implements every major subsystem of a real DBMS — disk management, buffer
pool, B+ Tree indexing, a Volcano-model query executor, a cost-based optimizer,
Strict 2PL concurrency control, and Write-Ahead Log recovery — in roughly 2,000
lines of hand-written code with zero external dependencies.

**Track A (Performance)** was selected as the extension.  A vectorized
`BatchSeqScanExecutor` was added alongside the classical Volcano
`SeqScanExecutor`, achieving a **10.4× throughput improvement** on a 100,000-
tuple full-table scan by amortizing buffer-pool pin/unpin overhead across
batches of tuples rather than paying it once per row.

---

## System Architecture

```
SQL / AST (DummyAST)
        │
        ▼
  ┌─────────────┐
  │  Optimizer  │  cost-based: selectivity estimation + join-order selection
  └──────┬──────┘
         │  AbstractExecutor tree
         ▼
  ┌─────────────────────────────────────────┐
  │         Execution Engine                │
  │  SeqScan / BatchSeqScan / IndexScan     │
  │  InsertExecutor / NestedLoopJoin        │
  └──────┬──────────────────────────────────┘
         │  TableHeap::GetTuple / InsertTuple
         ▼
  ┌─────────────┐    ┌─────────────┐
  │  TableHeap  │    │  BPlusTree  │
  └──────┬──────┘    └──────┬──────┘
         │  FetchPage / UnpinPage / NewPage
         ▼
  ┌──────────────────────┐
  │  BufferPoolManager   │  LRU eviction, dirty-page tracking
  └──────────┬───────────┘
             │  ReadPage / WritePage
             ▼
  ┌──────────────────────┐
  │     DiskManager      │  raw binary file, fixed 4 096-byte pages
  └──────────────────────┘
```

Concurrency and recovery are layered orthogonally:

- `LockManager` (Strict 2PL, `transaction.h`) intercepts row-level access.
- `LogManager` + `RecoveryManager` (`recovery.h`) implement Write-Ahead Logging
  with a two-pass Redo-only recovery path.

---

## Storage Layer

**Files:** `src/storage.h`, `src/storage.cpp`, `src/buffer_pool.h`,
`src/buffer_pool.cpp`

### DiskManager

`DiskManager` maintains a single binary file (`*.db`).  Every page is exactly
`PAGE_SIZE = 4 096` bytes.  `AllocatePage()` appends a new zeroed page and
returns its sequential `page_id_t` (a 32-bit integer).  `ReadPage` and
`WritePage` seek to `page_id × 4096` before every I/O call, so the file is the
authoritative source of page contents.  Reopening an existing file recovers
`num_pages_` from the file size, making the storage layer crash-consistent at
the page level.

### BufferPoolManager

The BPM keeps at most `pool_size` frames in DRAM.  Its internal data structures
are:

| Structure | Purpose |
|-----------|---------|
| `Page frames[pool_size]` | In-memory page storage |
| `unordered_map<page_id_t, size_t> page_table_` | page-id → frame index |
| `list<size_t> lru_list_` | MRU at front, LRU at back |
| `unordered_map<size_t, list<size_t>::iterator> lru_map_` | O(1) LRU touch |
| `list<size_t> free_list_` | unused frames |

**`FetchPage(page_id)`** checks `page_table_`; on a hit it touches the LRU
list and increments `pin_count`.  On a miss it evicts the LRU-eligible (unpinned)
frame, writes its dirty contents back to disk if needed, then reads the
requested page in.

**`UnpinPage(page_id, is_dirty)`** decrements `pin_count` and sets `is_dirty`
if the caller modified the page.  Pages with `pin_count > 0` are never evicted.

**`AllUnpinned()`** iterates `page_table_` and returns `false` if any frame has
`pin_count > 0`.  Every test suite calls this after each operation as a
correctness oracle.

---

## Indexing

**Files:** `src/btree.h`, `src/btree.cpp`

The index is an order-254 B+ Tree stored entirely in the BPM (one node per
page).  Node layouts:

| Node type | Capacity | Struct size |
|-----------|----------|-------------|
| `LeafNode` | 254 key-value pairs | 4 080 bytes ≤ 4 096 |
| `InternalNode` | 338 keys, 339 child pointers | 4 080 bytes ≤ 4 096 |

### Pinning Contract

The central invariant that makes the B+ Tree correct is the **pinning
contract**:

- **`Search()`** unpins each internal page _before_ fetching the next level, so
  at most one page is pinned at any moment during a traversal.
- **`Insert()`** builds a `PathEntry` vector of `{page_id, Page*, child_idx}`
  triples while descending.  All ancestors are pinned simultaneously during the
  descent.  On the way back up, split pages are allocated with `NewPage`,
  immediately populated, and unpinned — the parent is updated and then unpinned.
  The root split allocates a new root page via `NewPage` and unpins it at the
  end.
- Helper functions (`InsertIntoLeaf`, `InsertIntoInternal`) never unpin their
  input page — that responsibility belongs to the caller.

This explicit contract means `AllUnpinned()` is always true between any two
public B+ Tree calls, which the pin-stress test verifies after every insertion.

---

## Query Execution

**Files:** `src/execution.h`, `src/execution.cpp`

### Volcano (Iterator) Model

All executors inherit from `AbstractExecutor`:

```cpp
class AbstractExecutor {
public:
    virtual void   Init() = 0;
    virtual bool   Next(Tuple *out) = 0;
    virtual size_t NextBatch(std::vector<Tuple> *out, size_t batch_size);
};
```

`Init()` resets the executor to its start state.  `Next()` returns one tuple
per call; `false` signals end-of-stream.

| Executor | Description |
|----------|-------------|
| `ValueExecutor` | Emits a fixed in-memory tuple list; source for `InsertExecutor` |
| `SeqScanExecutor` | Scans every `HeapPage`, optionally filters with a predicate; skips tombstones |
| `IndexScanExecutor` | Point lookup via B+ Tree; emits zero or one tuple |
| `InsertExecutor` | Pulls from a child, calls `TableHeap::InsertTuple` + `BPlusTree::Insert` |
| `DeleteExecutor` | Takes a child executor and removes matching tuples from `TableHeap` (soft-delete tombstone) and `BPlusTree` |
| `NestedLoopJoinExecutor` | For each left tuple, re-`Init()`s and rescans the right side |
| `BatchSeqScanExecutor` | Vectorized variant of `SeqScanExecutor` (see below) |

### Pin Discipline in SeqScanExecutor

`SeqScanExecutor::Next()` calls `FetchPage`, reads exactly one matching tuple,
then calls `UnpinPage` — all within a single `Next()` call.  Between calls,
zero pages are pinned.  This makes it safe for `NestedLoopJoinExecutor` to
call `right_->Init()` at any point.

### Vectorized Execution (Track A)

`BatchSeqScanExecutor::NextBatch(out, batch_size)` replaces the one-tuple-per-
call pattern with a bulk-read loop:

```
for each page until batch is full:
    FetchPage(cur_page_id)          ← 1 BPM lock/unlock
    while cur_slot < n and batch not full:
        copy tuple[cur_slot++] → out
    UnpinPage(cur_page_id)          ← 1 BPM lock/unlock
    if page exhausted: advance to next_page
```

For 170 tuples per 4 KB page and `batch_size = 100`, a full scan of 100 000
tuples requires **≈ 1 200 `FetchPage` calls** instead of the Volcano model's
**100 000** — an 83× reduction in BPM operations.

`Next()` on `BatchSeqScanExecutor` drains an internal 256-tuple buffer filled
by `NextBatch()`, so it is fully backward-compatible with the Volcano protocol.

---

## Optimizer

**Files:** `src/optimizer.h`, `src/optimizer.cpp`

The optimizer operates on a `DummyAST` that carries:

- `Type` — `SCAN`, `NESTED_LOOP_JOIN`, or `DELETE`
- `table_name` — which table to scan / delete from
- `filter_field` — `NONE`, `ID`, `VAL1`, or `VAL2`
- `filter_value` — the literal to compare against
- `left` / `right` — child AST nodes (for joins)

`Optimizer::RegisterTable(name, heap*, index*)` populates the catalog, which
also exposes each table's live-tuple count (`GetNumTuples()`) for cost estimates.

### Selectivity Estimation

The optimizer assigns a per-field selectivity that models how many rows an
equality filter is expected to return:

| Filter field | Selectivity | Rationale |
|-------------|-------------|-----------|
| `ID` | 1 / 100 | Primary key — very high cardinality |
| `VAL1` / `VAL2` | 1 / 10 | Lower cardinality secondary attributes |
| `NONE` | 1.0 | Full scan, no filter |

### Cost-Based Scan Selection

For a SCAN node the optimizer computes:

```
seq_cost   = N                             (visit every slot)
index_cost = log₂(N+1) + selectivity × N  (B-tree traversal + result output)
```

When `filter_field == ID` (the only indexed field) and `index_cost < seq_cost`,
the optimizer returns an `IndexScanExecutor`; otherwise it falls back to
`SeqScanExecutor` with an inline predicate lambda.  For any table with more
than a handful of rows, `index_cost ≈ log₂N + N/100 ≪ N`, so point lookups
on the primary key always choose the index path.

### Join-Order Optimisation (NLJ)

When both children of a `NESTED_LOOP_JOIN` are table scans, the optimizer
compares their live-tuple counts and places the **smaller table on the left**
(outer loop):

```
Cost(NLJ) ≈ |outer| × cost(inner re-scan)
```

Minimising `|outer|` reduces the number of `Init()` calls on the inner
executor, which is especially valuable when the inner table is large.

### DELETE Support

A `DELETE` AST node is lowered by:

1. Building a SCAN child executor (cost-based, same rules as above) that
   identifies the rows matching the filter.
2. Wrapping it in `DeleteExecutor`, which for each matched tuple:
   - Looks up the physical slot via `BPlusTree::Search(t.id)`.
   - Calls `TableHeap::DeleteTuple(rid)` (soft-delete: writes `DELETED_TUPLE_ID`
     tombstone, marks page dirty, decrements live-tuple count).
   - Calls `BPlusTree::Delete(t.id)` (shifts remaining leaf entries left;
     no node rebalancing).

---

## Transactions & Concurrency

**Files:** `src/transaction.h`, `src/transaction.cpp`

The engine implements **Strict Two-Phase Locking (Strict 2PL)**:

- Locks may only be acquired while a transaction is in the `GROWING` state.
- All locks are held until the transaction commits or aborts (`ReleaseLocks`).
- `SHARED` locks are compatible with other shared locks; `EXCLUSIVE` locks block
  all others.

### Isolation Guarantee

Strict 2PL guarantees **Serializable isolation**: because no lock is released
until the transaction ends, no other transaction can observe a partial write,
and the resulting execution is equivalent to some serial schedule.

### Deadlock Handling

The engine uses **timeout-based deadlock resolution**.  When a lock request
cannot be granted immediately, the requesting transaction waits on a
`condition_variable` for up to **50 ms**.  If the lock is still not granted
after the timeout, the transaction is marked `ABORTED` and
`TransactionAbortException` is thrown — effectively breaking any deadlock cycle
without the overhead of a waits-for graph.  The `test_lock_timeout` test
demonstrates this: Thread 2 attempts to acquire a conflicting lock while
Thread 1 holds it for 100 ms, and Thread 2 is automatically aborted at the
50 ms threshold.

### LockManager

`LockManager` maintains one `LockRequestQueue` per RID.  Each queue holds a
`std::list<LockRequest>` (stable iterator addresses through insertions) and a
`std::condition_variable`.  The map stores queues as `unique_ptr<LockRequestQueue>`
because `condition_variable` is non-movable and cannot live directly in an
`unordered_map` value that may be rehashed.

**`AcquireLock(txn, rid, mode)`:**

1. Appends an ungranted `LockRequest` to the queue.
2. Checks `CanGrantShared` / `CanGrantExclusive`.
3. If compatible, grants immediately.
4. Otherwise, waits on the condition variable for up to **50 ms**.
5. On timeout: removes the request, sets the transaction state to `ABORTED`,
   and throws `TransactionAbortException`.

**`ReleaseLocks(txn)`:** removes all of the transaction's requests from every
locked RID's queue, calls `GrantPending()` (FIFO compatible-request promotion),
and notifies waiting threads.

---

## Recovery

**Files:** `src/recovery.h`, `src/recovery.cpp`

The engine uses **Write-Ahead Logging** with Redo-only recovery.

### Log Format

Every `LogRecord` is a fixed **40-byte** struct:

```
[int32 txn_id][int32 type][int32 page_id][int32 slot_num]
[int64 id    ][int64 val1][int64 val2   ]
```

`LogType` values: `BEGIN`, `INSERT`, `COMMIT`, `ABORT`.  For non-INSERT records
the page/slot/tuple fields are zero.  `static_assert(sizeof(LogRecord) == 40)`
enforces the layout at compile time.

### LogManager

`LogManager` opens the WAL in **append mode** (`std::ios::app`), protecting
every write with a mutex.  `Flush()` calls `std::ofstream::flush()`, which must
be called before reporting a `COMMIT` to the caller.

### RecoveryManager::Redo()

Two-pass algorithm:

1. **Pass 1** — read all records, collect the set of `committed` transaction IDs
   (those with a `COMMIT` record).
2. **Pass 2** — replay every `INSERT` record whose `txn_id` is in `committed`
   by calling `heap_->InsertTuple()`.

Uncommitted and aborted transactions are silently skipped.  This is safe for
the insert-only workload because there are no in-place updates to undo.

---

## Extension Track — Vectorized Execution (Track A)

### Design

`BatchSeqScanExecutor` inherits from `AbstractExecutor` and overrides
`NextBatch()`.  The optimization is that the BPM's internal mutex is acquired
once per page per batch rather than once per tuple per `Next()` call.

With `HEAP_PAGE_TUPLES = 170` and `batch_size = 100`:

| Metric | SeqScanExecutor | BatchSeqScanExecutor |
|--------|-----------------|----------------------|
| Virtual calls per 100K scan | 100 000 | ≈ 1 000 |
| FetchPage calls per 100K scan | 100 000 | ≈ 1 200 |
| BPM lock/unlock pairs | 200 000 | ≈ 2 400 |

### Benchmark Results

Measured on Apple M-series, macOS 15, compiled with `-O2`, pool size 1 024
(all pages resident in memory — measures execution overhead only, not I/O):

```
=== Execution Benchmark: 100000 tuples, batch_size=100 ===

Executor                Time (ms)   Throughput (t/s)
SeqScanExecutor              2.84         35,257,823
BatchSeqScanExecutor         0.27        366,413,109

Speedup: 10.39x
```

The **10.4× speedup** comes from reducing the number of BPM mutex operations
from 200 000 to roughly 2 400.  The remaining gap between the theoretical 83×
reduction in `FetchPage` calls and the observed 10× wall-clock speedup reflects
the BPM's hash-map lookup and LRU-list bookkeeping, which are faster than full
mutex acquisitions but still contribute overhead.

---

## Limitations & Future Improvements

| Area | Current State | Possible Improvement |
|------|---------------|---------------------|
| Schema | Fixed three-column schema (`id`, `val1`, `val2`) | Variable-width tuples, catalog |
| Updates | No support | In-place update + UNDO logging |
| Recovery | Redo-only | Full ARIES (Undo pass for aborted txns) |
| Concurrency | Row-level Strict 2PL, no deadlock detection | Waits-for graph + cycle detection |
| Index | Single B+ Tree on `id`; no range scans | Multi-index, range iterators |
| SQL | `DummyAST` only | Real SQL parser (e.g. antlr4) |
| Buffer replacement | LRU | Clock, LRU-K, or ARC |
| Vectorization | `NextBatch` for heap scans only | SIMD predicate evaluation, columnar layout |
| Durability | Single WAL file, no checkpoints | Periodic fuzzy checkpoints to bound recovery time |

---

## How to Build and Run

### Prerequisites

- CMake ≥ 3.14
- A C++17-compliant compiler (tested with AppleClang 17 / GCC 13)
- POSIX threads (provided by the OS)

### Build

```bash
git clone <repo-url>
cd Team_SoloEngine
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)   # or: make -j$(sysctl -n hw.logicalcpu)  on macOS
```

### Run Tests

```bash
# From the build/ directory:
./test_storage        # 9  tests — DiskManager + BufferPoolManager
./test_btree          # 3 tests — B+ Tree correctness + pin-leak detection
./test_execution      # 7  tests — insert, seq scan, filtered scan, index scan, NLJ, pin stress, delete
./test_transactions   # 4  tests — lock timeout, shared compatibility, WAL recovery, abort
```

All four suites should print `All * tests passed.` with 27 `[PASS]` lines total.

### Run the Benchmark

```bash
# From the build/ directory (rebuild with -O2 for meaningful numbers):
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) bench_execution
./bench_execution
```

Expected output:

```
Inserting 100000 tuples...
Insert complete.

=== Execution Benchmark: 100000 tuples, batch_size=100 ===

Executor                Time (ms)   Throughput (t/s)
SeqScanExecutor              2.84         35,257,823
BatchSeqScanExecutor         0.27        366,413,109

Speedup: 10.39x
```

### Project Layout

```
Team_SoloEngine/
├── src/
│   ├── storage.{h,cpp}       DiskManager — raw page I/O
│   ├── buffer_pool.{h,cpp}   BufferPoolManager — LRU frame management
│   ├── btree.{h,cpp}         B+ Tree index (order 254/338)
│   ├── table.{h,cpp}         TableHeap — heap file + Tuple/RID types
│   ├── execution.{h,cpp}     Executor tree (Volcano + vectorized)
│   ├── optimizer.{h,cpp}     Cost-based query optimizer (selectivity + join ordering)
│   ├── transaction.{h,cpp}   Strict 2PL LockManager + Transaction
│   └── recovery.{h,cpp}      WAL LogManager + RecoveryManager
├── tests/
│   ├── test_storage.cpp
│   ├── test_btree.cpp
│   ├── test_execution.cpp
│   └── test_transactions.cpp
├── benchmarks/
│   └── bench_execution.cpp   Volcano vs. vectorized throughput benchmark
└── CMakeLists.txt
```
