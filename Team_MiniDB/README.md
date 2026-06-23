# MiniDB

A relational database engine built from scratch in C++17 for the Advanced
DBMS capstone: page-based storage, a disk-backed B+Tree index, a SQL
parser/optimizer/executor, Strict Two-Phase Locking with deadlock
detection, WAL-based crash recovery, and a primary-replica replication
extension (Track D).

## Team

**Team members:**

| Name | Git username | Email | Primary module |
|---|---|---|---|
| Keshav Makkad | KeshavMakkad | keshavmakkad10@gmail.com | Query engine, optimizer, executor, catalog, SQL parser, CLI integration |
| Manav Pratap Singh | ManavPratapSingh | manav3407.gb@gmail.com | Transactions, 2PL locking, WAL + recovery, Track D replication |
| Piyush | PKG-boii | Piyush.24bcs10332@sst.scaler.com | B+Tree primary index |
| Syed Ayaan Ali | SyedAyaanAli6786 | aliaayan6786@gmail.com | Storage engine (pages/disk manager/buffer pool), benchmarks |

*(Roll numbers to be added before submission.)*

## 1. Project Overview

**Problem statement.** Implement a working relational database engine
that integrates storage, indexing, query processing, transactions, and
recovery - the components built individually across the course's lab
modules - into one coherent system, and pick one extension track to go
deeper on.

**Goals.** Correctness and architectural clarity over feature count:
every required component (storage engine, B+Tree index, SQL execution
with joins, cost-based optimizer, Strict 2PL transactions, WAL/recovery)
works end-to-end and is small enough for every team member to explain in
the viva.

**Chosen extension track: Track D - Distributed Systems (replication).**
A primary-replica setup over TCP: the primary broadcasts every successful
write statement to connected replicas, which apply it to their own
independent storage stack and serve read-only queries. See [Section 9](#9-extension-track-d---replication).

## 2. System Architecture

```
                 ┌─────────────────────────────────────────┐
                 │                  CLI (main.cpp)          │
                 │   BEGIN/COMMIT/ABORT, EXPLAIN, CRASH      │
                 └───────────────┬───────────────────────────┘
                                  │
        ┌─────────────────────────┼─────────────────────────┐
        │                         │                         │
 ┌──────▼──────┐          ┌───────▼────────┐        ┌────────▼────────┐
 │ TxnManager  │          │   Executor      │        │  WalManager /   │
 │ LockManager │◄─────────┤  (parses via    │───────►│ RecoveryManager │
 │ (Strict 2PL,│          │   sql/lexer +   │        │ (ARIES-lite)    │
 │  deadlock   │          │   sql/parser)   │        └────────┬────────┘
 │  detection) │          └───────┬─────────┘                 │
 └─────────────┘                  │                            │
                          ┌────────▼─────────┐                  │
                          │    Optimizer      │                  │
                          │ (access path +    │                  │
                          │  join order/algo) │                  │
                          └────────┬─────────┘                  │
                                   │                            │
                          ┌────────▼─────────┐                  │
                          │   Plan operators  │                  │
                          │ SeqScan/IndexScan/ │                  │
                          │ Filter/(Index)NLJ/ │                  │
                          │ Project            │                  │
                          └────────┬─────────┘                  │
                                   │                            │
                 ┌─────────────────▼────────────────────────────▼──┐
                 │                    Catalog                       │
                 │     (table -> Schema, HeapFile, BPlusTree)        │
                 └─────────────────┬──────────────────────┬─────────┘
                                   │                      │
                          ┌────────▼────────┐    ┌────────▼────────┐
                          │    HeapFile      │    │    BPlusTree     │
                          │ (slotted pages)  │    │ (leaf/internal   │
                          └────────┬────────┘    │  pages)          │
                                   │              └────────┬────────┘
                                   └──────────┬─────────────┘
                                              │
                                     ┌────────▼─────────┐
                                     │    BufferPool     │
                                     │ (pin/unpin, LRU)  │
                                     └────────┬─────────┘
                                              │
                                     ┌────────▼─────────┐
                                     │   DiskManager      │
                                     │ (.db pages, .wal)  │
                                     └────────────────────┘
```

**Data flow for a SELECT:** CLI -> `sql::Lexer`/`Parser` -> AST -> 
`Optimizer::PlanSelect` builds a `PlanNode` tree -> the CLI drives
`Open()`/`Next()`/`Close()` on it -> each scan node reads through
`Catalog` -> `HeapFile`/`BPlusTree` -> `BufferPool` -> `DiskManager`.

**Data flow for INSERT/DELETE inside a transaction:** CLI acquires a
table lock via `LockManager` -> `Executor` mutates the heap/index ->
`WalManager` appends a log record (before this is reported back to the
user) -> on `COMMIT`, locks are released; on `ABORT`/crash,
`RecoveryManager` undoes the logged operations and locks are released.

## 3. Storage Layer

Files: [src/storage/](src/storage/)

- **Page format** ([page.h](src/storage/page.h)): fixed 4096-byte frames.
  Tuple pages use a **slotted-page** layout
  ([heap_page.h](src/storage/heap_page.h)):
  `[num_slots | free_end | next_page_id | slot[0] | slot[1] | ...]` then
  free space, then tuple bytes growing backward from the end of the page.
  Each slot is `{offset, length}`; `length == -1` marks a tombstoned
  (deleted) slot so RIDs stay stable. `next_page_id` lives in the header
  itself (not a side table) so a heap file's page chain survives a
  restart.
- **Heap files** ([heap_file.h](src/storage/heap_file.h)): `InsertTuple`
  walks the page chain looking for room, allocating and linking a new
  page when every existing page is full; `Scan` walks the same chain
  calling back with every live tuple's RID and bytes.
- **Disk manager** ([disk_manager.h](src/storage/disk_manager.h)): raw
  `ReadPage`/`WritePage` by page id over a `.db` file, page allocation
  with a free list for reclaimed pages, and the WAL's raw append/read
  primitives over a sibling `.wal` file.
- **Buffer pool** ([buffer_pool.h](src/storage/buffer_pool.h)): fixed
  number of frames, `FetchPage`/`NewPage`/`UnpinPage`/`FlushPage`, LRU
  eviction among currently-unpinned frames. Every page access in the
  system - heap, B+Tree, recovery redo/undo - goes through this pool;
  there is no other place pages are read from disk.

## 4. Indexing

Files: [src/index/](src/index/)

- **B+Tree** ([b_plus_tree.h](src/index/b_plus_tree.h),
  [b_plus_tree_page.h](src/index/b_plus_tree_page.h)): disk-backed,
  `int64` keys mapping to `RID`s. Leaf nodes are chained via
  `next_leaf_page_id`; internal nodes store `child[0], (key[0],
  child[1]), ...`. Fanout is deliberately small (`BTREE_LEAF_MAX =
  BTREE_INTERNAL_MAX = 4`) so a handful of inserts already produces a
  multi-level tree with real splits, instead of needing thousands of rows
  to see anything beyond a single leaf.
- **Node structure:** every node's header (`is_leaf`, `num_keys`,
  `parent_page_id`, `next_leaf_page_id`) lives at the start of its page,
  exactly like heap pages, so both can be inspected with the same mental
  model.
- **Search path:** `FindLeaf` descends from the root, at each internal
  node taking `ChildIndexFor(key)` (first index whose key is `>
  target`), until it reaches a leaf, then binary-searches the leaf with
  `LowerBound`.
- **Insert/split:** insert into the leaf; if it overflows, split it in
  half, push the new leaf's first key up via `InsertIntoParent`, which
  may recursively split internal nodes and eventually allocate a brand
  new root.
- **Delete:** removes the key from its leaf. **Documented limitation:**
  no merge/rebalance of underflowed siblings after deletion - correctness
  of search/insert/delete is preserved, but space utilization after heavy
  deletion is suboptimal. A production B+Tree would coalesce siblings.

## 5. Query Execution

Files: [src/sql/](src/sql/), [src/query/](src/query/)

- **Parser:** [lexer.h](src/sql/lexer.h)/[lexer.cpp](src/sql/lexer.cpp)
  tokenizes; [parser.h](src/sql/parser.h)/[parser.cpp](src/sql/parser.cpp)
  is a hand-written recursive-descent parser producing the AST in
  [ast.h](src/sql/ast.h). Supports `CREATE TABLE`, `INSERT`, `SELECT`
  (column list or `*`, one `INNER JOIN ... ON`, `WHERE` with `AND`-chained
  comparisons), and `DELETE ... WHERE`.
- **Query plan generation:** [optimizer.cpp](src/query/optimizer.cpp)
  turns a `SelectStmt` into a `PlanNode` tree (see Section 6).
- **Operator execution:** Volcano-style iterators in
  [plan_node.h](src/query/plan_node.h)/[plan_node.cpp](src/query/plan_node.cpp) -
  `SeqScanNode`, `IndexScanNode`, `FilterNode`, `NestedLoopJoinNode`,
  `IndexNestedLoopJoinNode`, `ProjectNode` - each implementing
  `Open()`/`Next()`/`Close()`. [executor.cpp](src/query/executor.cpp)
  drives `SELECT` plans and directly mutates the catalog/heap/index for
  `CREATE TABLE`/`INSERT`/`DELETE`.

## 6. Optimizer

File: [src/query/optimizer.cpp](src/query/optimizer.cpp)

- **Cost model:** abstract "page touch" units - `SeqScan ~ N` rows,
  `IndexScan ~ log2(N) + 1`, `NestedLoopJoin ~ outer_card * inner_cost`.
  There's no histogram, so **selectivity for non-PK predicates is a fixed
  constant** (0.1 for `=`, 0.9 for `!=`, 0.33 for range ops) - a
  documented simplifying assumption in place of real statistics.
- **Selectivity/cardinality estimation:** `TableInfo::num_rows` (tracked
  by `Catalog::NotifyRowInserted`/`NotifyRowDeleted`) times the
  selectivity of each remaining filter on that relation; an
  equality-on-PK access path collapses cardinality to 1 since the key is
  unique.
- **Access path selection:** `PlanBaseRelation` picks `IndexScanNode`
  whenever there's an equality predicate on the table's primary key,
  otherwise `SeqScanNode` - demonstrated directly by `EXPLAIN` in the CLI
  and by the [seqscan-vs-index benchmark](benchmarks/RESULTS.md). **This
  rule is unconditional and not actually cost-optimal at small table
  sizes**: the benchmark measured IndexScan as ~10% *slower* than SeqScan
  at 1,000 rows, because the deliberately tiny B+Tree fanout (Section 4)
  produces a tree deep enough that its per-level buffer-pool overhead
  outweighs a cheap small scan. The optimizer doesn't model this crossover
  - see RESULTS.md for the full numbers and why.
- **Join order selection:** for a two-table join, each side's
  post-filter cardinality is estimated, and the smaller side drives the
  loop (`left_card <= right_card` decides which is "outer").
- **Join algorithm selection:** if the join predicate equates the join
  column with the *other* side's primary key, the optimizer swaps to
  `IndexNestedLoopJoinNode` (probe the PK index per outer row, O(N log M))
  instead of `NestedLoopJoinNode` (rescans the inner side per outer row,
  O(N*M)).

## 7. Transactions & Concurrency

Files: [src/txn/](src/txn/)

- **Locking strategy:** table-granularity Strict Two-Phase Locking
  ([lock_manager.h](src/txn/lock_manager.h)) - shared locks for reads,
  exclusive for writes, held for the transaction's entire lifetime and
  released only at commit/abort (`LockManager::ReleaseAll`). The CLI
  acquires the right lock for every table a statement references before
  calling the executor.
- **Isolation guarantee:** Strict 2PL produces **Serializable**
  schedules - because no transaction releases any lock before it has
  acquired all of its locks (the `GROWING` phase strictly precedes
  `SHRINKING`), conflicting transactions can always be ordered by lock
  acquisition.
- **Deadlock handling:** a background thread
  (`LockManager::DeadlockDetectionLoop`) rebuilds a wait-for graph every
  100ms from currently-blocked lock requests, runs DFS cycle detection,
  and aborts the **youngest** transaction (highest id) in any cycle it
  finds - that transaction's blocked `LockShared`/`LockExclusive` call
  throws `TransactionAbortedException`, which the caller rolls back via
  `RecoveryManager::UndoTransaction`. Demonstrated by
  [tests/txn_test.cpp](tests/txn_test.cpp): two threads lock two
  resources in opposite order, and the test asserts exactly one aborts
  while the other commits.

## 8. Recovery

Files: [src/recovery/](src/recovery/)

- **WAL design:** [wal_manager.h](src/recovery/wal_manager.h) appends
  length-framed [log_record.h](src/recovery/log_record.h) records to a
  `.wal` file, flushed synchronously on every append (the WAL invariant
  this project relies on - a log record is durable before the operation
  it describes is considered done).
- **Log records:** `BEGIN`, `INSERT`, `DELETE`, `COMMIT`, `ABORT`.
  `INSERT`/`DELETE` carry the **full tuple bytes** (after-image for
  insert, before-image for delete) plus the exact `RID`, so both redo and
  undo are "apply this exact byte string at this RID" - no partial-update
  diffing.
- **Crash recovery procedure**
  ([recovery_manager.cpp](src/recovery/recovery_manager.cpp)), simplified
  by treating the B+Tree as a structure *derived* from the heap rather
  than something the WAL redoes/undoes directly:
  1. **Analysis** - scan the WAL once; transactions with a `BEGIN` but no
     `COMMIT`/`ABORT` are "losers."
  2. **Redo** - reapply every `INSERT`/`DELETE` in log order,
     idempotently (skipped if the target slot already reflects it, so it
     is safe whether or not the page had reached disk before the crash).
  3. **Undo** - walk each loser's ops in reverse, undoing inserts
     (tombstone the row) and deletes (restore the row).
  4. **Rebuild every primary-key B+Tree from scratch** by scanning the
     now-correct heap file - this is what lets steps 2-3 skip index pages
     entirely. The WAL/catalog's known page ids are also used to
     fast-forward the disk allocator past anything referenced but never
     flushed, so a freshly allocated index page can't collide with an
     unflushed heap page (a real bug caught while building this - see the
     `fix(recovery)` note in `git log`).
  5. The log is truncated after a successful recovery (a checkpoint), so
     future restarts don't replay history that's already durable.
  Live `ABORT`/`ROLLBACK` (no crash involved) uses the same idea via
  `RecoveryManager::UndoTransaction`.
- Demonstrated end-to-end in
  [tests/recovery_test.cpp](tests/recovery_test.cpp) and interactively via
  the CLI's `CRASH` command (see Section 12) - committed transactions
  survive, in-flight ones are undone.
- **Documented limitation:** index pages allocated before a crash/abort
  are abandoned rather than reclaimed (a production system would either
  WAL-log index mutations too, or garbage-collect the page file).

## 9. Extension Track: D - Replication

Files: [src/replication/](src/replication/)

**Motivation.** Of the four tracks, replication maps most directly onto
concepts already built for this project (the executor/catalog/storage
stack can be reused verbatim on a second node) while still requiring a
genuinely new piece of infrastructure: a network protocol and a second,
independently-running process.

**Design.** Two executables, `minidb_primary` and `minidb_replica`:

- `minidb_primary` runs the same interactive SQL REPL as `minidb`, plus a
  TCP listener ([network_util.h](src/replication/network_util.h)). Every
  DDL/DML statement that succeeds is broadcast, in commit order, to every
  connected replica (`PrimaryReplicator::Broadcast` in
  [replication.h](src/replication/replication.h)).
- `minidb_replica` connects to the primary and runs a background thread
  (`ReplicaApplier::RunLoop`) that parses and applies every received
  statement against its own independent `Catalog`/`Executor`, while its
  foreground thread serves **read-only** queries against that
  (eventually-consistent) local state.

This is **logical/statement-based replication**, not physical WAL
shipping - a deliberate scope simplification: it reuses the exact same
parser/executor on both ends instead of needing to serialize/replay raw
page mutations across a network, at the cost of being less data-format-
robust than real physical replication (and not catching up a replica that
was disconnected when statements were broadcast - no snapshot/backfill).

**Results** (see [tests/replication_test.cpp](tests/replication_test.cpp)
and [benchmarks/bench_replication_lag.cpp](benchmarks/bench_replication_lag.cpp)):

- **Replication flow:** primary accepts a connection, broadcasts
  `CREATE TABLE`/`INSERT` statements, replica applies them and a `SELECT`
  against the replica returns the same rows.
- **Read consistency:** demonstrated as eventual consistency - the test
  polls `ReplicaApplier::AppliedCount()` until it matches the primary's
  write count rather than assuming an instant mirror; the benchmark
  measures this lag directly (**~1.3ms on localhost for 500 writes**).
- **Failure scenario:** stopping the primary's listener closes the
  replica's connection; `ReplicaApplier::IsConnected()` flips to false and
  the replica keeps answering `SELECT`s from its last-known state instead
  of crashing or blocking.

## 10. Benchmarks

Full results and analysis: [benchmarks/RESULTS.md](benchmarks/RESULTS.md).

Summary:

| Benchmark | Headline result |
|---|---|
| IndexScan vs. SeqScan | **Loses** at 1k rows (0.9x) due to the deliberately tiny B+Tree fanout's per-level overhead, then wins decisively as N grows: 3.1x at 5k, 10.2x at 20k rows - see RESULTS.md for why the optimizer's blanket preference isn't cost-optimal below the crossover point |
| Buffer pool size (4/64/1024 frames) | 57.93ms / 33.86ms / 34.82ms for 20 full scans (modest gap above 4 frames - OS page cache absorbs most of the difference on this hardware) |
| Replication lag (500 writes) | ~1.26ms after the primary's last write; primary throughput ~7,372 writes/sec |

## 11. Limitations

- **B+Tree deletion** doesn't rebalance/merge underflowed nodes (Section 4).
- **The optimizer always prefers IndexScan over SeqScan for a PK equality
  predicate, even when that's not actually cheaper** - measured to lose
  by ~10% at 1,000 rows because of the tiny fixed fanout's per-level
  overhead (Section 6, `benchmarks/RESULTS.md`). A cost model that
  accounted for buffer-pool round-trip cost per tree level, not just
  abstract page counts, would catch this.
- **Heap insert is O(pages)**: `HeapFile::InsertTuple` re-walks the page
  chain from the first page every time looking for free space, instead of
  remembering the last page with room. Fine at the scale this project
  targets; would need a free-space map to scale further.
- **No secondary indexes** - only the primary key is indexed (the spec
  marks secondary indexes optional).
- **PRIMARY KEY must be INTEGER** - the B+Tree only supports `int64` keys
  (Section 4), so `CREATE TABLE` rejects a VARCHAR/other-typed primary key
  immediately with a clear error rather than letting it fail confusingly
  later at INSERT time.
- **Single-join SQL** - the parser supports exactly one `JOIN` per query,
  not arbitrary join graphs; the optimizer's "join order selection" is
  therefore a binary choice (which side drives), not a full join-order
  search over more than two relations.
- **Table-granularity locking**, not row-level - simpler to reason about
  and sufficient to demonstrate Strict 2PL/deadlocks, but coarser than a
  production system (less concurrency between transactions touching
  disjoint rows of the same table).
- **Replication is logical/statement-based**, not physical WAL shipping,
  and has no snapshot/backfill for a replica that reconnects after
  missing statements (Section 9).
- **Index pages aren't reclaimed** after a crash/abort - the old pages
  are simply abandoned in favor of a freshly rebuilt index (Section 8).
- **Catalog metadata isn't WAL-logged** - it's persisted directly and
  immediately to a sidecar text file on every DDL/stat change, separate
  from the data WAL. Simpler, and sufficient since DDL is rare relative
  to DML, but means a crash mid-`CREATE TABLE` isn't covered by the same
  recovery protocol as row data.

## 12. How to Run

**Dependencies:** CMake >= 3.16, a C++17 compiler (tested with AppleClang
17 / Clang on macOS; any C++17-conformant GCC/Clang on Linux should work),
POSIX sockets (replication binaries use `<sys/socket.h>` etc., so Track D
is Linux/macOS only - not Windows without WSL).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure   # 8 tests, all should pass
```

**Interactive single-node CLI:**

```bash
./build/minidb mydb.db
minidb> CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR)
minidb> INSERT INTO users VALUES (1, 'alice')
minidb> EXPLAIN SELECT * FROM users WHERE id = 1
minidb> BEGIN
minidb*> INSERT INTO users VALUES (2, 'bob')
minidb*> ABORT
minidb> CRASH        # simulate an unclean crash; restart to see recovery run
```

**Two-node replication demo** (two terminals):

```bash
# Terminal 1 - primary
./build/minidb_primary primary.db 6000

# Terminal 2 - replica
./build/minidb_replica replica.db 127.0.0.1 6000
```

Type writes on the primary; `SELECT`s on the replica reflect them shortly
after. Kill the primary (Ctrl-C) and the replica keeps answering `SELECT`s
from its last-known state (`status` command shows connectivity).

**Benchmarks:**

```bash
./build/benchmarks/bench_seqscan_vs_index
./build/benchmarks/bench_buffer_pool
./build/benchmarks/bench_replication_lag
```
