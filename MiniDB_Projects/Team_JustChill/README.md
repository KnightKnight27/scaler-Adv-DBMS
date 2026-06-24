# MiniDB — Team JustChill

A from-scratch relational database engine in modern C++17: paged storage, an LRU
buffer pool, a B+ Tree index, a Volcano-model query executor, serializable
transactions via table-level Strict 2PL, write-ahead logging with crash
recovery, and a primary/replica replication layer.

**Chosen extension track: Track D — Distributed Systems (Replication).**

---

## Team

| Name | Roll Number | Scaler Email |
|------|-------------|--------------|
| Ansh Mahajan | 24BCS10345 | ansh.24bcs10345@sst.scaler.com |
| Prabhav Semwal | 24BCS10358 | prabhav.24bcs10358@sst.scaler.com |
| Pulasari Jai | 24BCS10656 | pulasari.24bcs10656@sst.scaler.com |
| Pranav Nayal | 24BCS10236 | pranav.24bcs10236@sst.scaler.com |

---

## 1. Project Overview

### Problem statement
Relational databases are usually treated as black boxes. The goal of MiniDB is
to open the box: build a working relational engine from first principles —
storage, indexing, query execution, concurrency control, durability, and
distribution — so every layer that a query passes through is something we wrote
and understand, not a library call.

### Goals
- **Durable, paged storage** with a buffer pool, instead of keeping everything in
  RAM and hoping for the best.
- **A real index** (B+ Tree) that supports both point lookups and ordered range
  scans, driving an `IndexScan` operator.
- **Composable query execution** via the Volcano/iterator model so operators
  (scan, filter, project, join, insert, delete) nest into arbitrary plans.
- **Serializable transactions** through Strict Two-Phase Locking.
- **Crash safety** via Write-Ahead Logging and recovery-by-replay.
- **Distribution** (our extension track): a primary that synchronously ships its
  log to a read replica.
- Honest engineering: a documented [Limitations](#11-limitations) section over
  hidden breakage.

### Chosen extension track
**Track D — Distributed Systems (Replication).** A two-node primary/replica
topology where the primary streams committed statements to a replica over TCP
and waits for an acknowledgement before committing locally (synchronous
replication). See [§9](#9-extension-track-distributed-systems-replication).

---

## 2. System Architecture

### Architecture diagram

```
                                 SQL text (CLI / WAL replay / replication stream)
                                            │
                                            ▼
                                ┌───────────────────────┐
                                │  Parser (lexer + RD)   │  text → AST
                                │  parser.cpp            │
                                └───────────┬────────────┘
                                            ▼
                                ┌───────────────────────┐
                                │  Cost-based optimizer  │  AST → operator tree
                                │  optimizer.cpp         │  (index vs scan; join order)
                                └───────────┬────────────┘
                                            ▼
                       ┌──────────────────────────────────────────┐
                       │        Execution engine (Volcano)         │
                       │  TableScan · IndexScan · Filter ·         │  open()/next()/close()
                       │  Projection · NestedLoopJoin ·            │
                       │  Insert · Delete                          │
                       └───────┬───────────────────────┬───────────┘
                  S / X locks  │                        │  key lookups / range scans
                               ▼                        ▼
                  ┌────────────────────────┐  ┌────────────────────────┐
                  │ LockManager            │  │  B+ Tree index          │
                  │ table-level Strict 2PL │  │  int64 key → RID        │
                  │ 3 s deadlock timeout   │  │  tombstone deletes      │
                  └────────────────────────┘  └───────────┬────────────┘
                                                           ▼
                  ┌────────────────────────────────────────────────────────┐
                  │                  Storage layer                          │
                  │  BufferPool (LRU, pin/unpin, dirty flush, checkpoint)   │
                  │  HeapFile (paged disk I/O)   Page (fixed 4 KB frames)   │
                  └───────────────────────┬────────────────────────────────┘
                                          │ committed mutations
                                          ▼
                  ┌────────────────────────────────────────────────────────┐
                  │  WAL (append + flush)  ──ship statement over TCP──▶      │
                  │  Replication (Track D):  primary → replica, sync ACK     │
                  └────────────────────────────────────────────────────────┘
```

A layered version with per-component detail lives in
[docs/architecture.md](docs/architecture.md).

### Major modules

| Module | Files | Responsibility |
|--------|-------|----------------|
| Storage | `page.*`, `slotted_page.h`, `heap_file.*`, `buffer_pool.*` | 4 KB pages, slotted-page layout, paged disk I/O, LRU buffer pool + checkpoint flush |
| Index | `btree.*` | Page-backed B+ Tree mapping `int64` PK → `RID`; point + range scans, durable |
| SQL front-end | `parser.*`, `optimizer.*` | Lexer + recursive-descent parser → AST; cost-based planner (index-vs-scan, join order) → operator tree |
| Execution | `execution.*` | Volcano operators + page-backed `Table`/`Catalog` (tuple (de)serialization) |
| Transactions | `transaction.*` | Per-statement auto-commit + in-memory undo log (rollback) |
| Concurrency | `lock_manager.*` | Table-level Strict 2PL (S/X, upgrade, 3 s timeout) |
| Durability | `wal.*` | Write-ahead log with BEGIN/COMMIT framing + committed-replay recovery |
| Distribution | `replication.*` | Primary/replica synchronous log shipping over TCP |
| Server | `main.cpp` | CLI, parse→optimize→execute, recovery, checkpoint daemon, replication wiring |
| Benchmarks | `benchmarks/*` | Live database query performance harness |

### Data flow

**Write path (`INSERT`/`DELETE` on the primary):**
1. Statement read from the CLI.
2. **WAL**: appended to `wal.log` and flushed to disk (durable *before* commit).
3. **Replication**: shipped to the replica over TCP; the primary blocks for an
   `OK` acknowledgement (2 s timeout). If it fails, the transaction is **aborted**
   (not applied locally).
4. **Execute**: under the global DB lock, the statement runs through the Volcano
   operator tree, mutating the table and its B+ Tree index.

**Read path (`SELECT`):** parsed → operator tree (`TableScan`, optionally
`Filter`/`Projection`) → tuples streamed back. Replicas accept reads only.

---

## 3. Storage Layer

### Page format
The fixed allocation unit is a **4 KB page** (`PAGE_SIZE = 4096`). Each `Page`
([page.h](src/page.h)) carries `page_id`, `pin_count` (eviction-safety),
`is_dirty`, and `char data[PAGE_SIZE]` (the raw payload).

**Slotted-page layout** ([slotted_page.h](src/slotted_page.h)) sits on top of
that raw buffer so variable-length rows can share a page:

```
0:                PageHeader { uint16 num_slots; uint16 free_end; }
4 + 4*i:          Slot[i] { uint16 offset; uint16 length }   (slot dir grows up)
...free space...
free_end .. 4096: record bytes (grow down from the end of the page)
```

`SlottedPage` is a thin view over `data` offering `insertRecord` (returns a slot
id), `getRecord`, `deleteRecord` (sets a slot's length to 0 — a **tombstone**,
no compaction), and `restoreRecord` (used to undo a delete). A row is addressed
by a `RID = (page_id, slot_id)`.

**Tuple serialization** ([execution.cpp](src/execution.cpp), `serializeTuple` /
`deserializeTuple`) is the on-page record format: per value a 1-byte type tag,
then an `int64` for ints or a `uint32` length + bytes for text.

### Heap files
`HeapFile` ([heap_file.cpp](src/heap_file.cpp)) is the on-disk backing store: one
file, accessed in page-sized chunks, serialized by a per-file mutex.
- `allocatePage()` — measures the file size, picks the next page id, and extends
  the file by writing a zeroed 4 KB block (so the slot physically exists).
- `readPage(id, *page)` / `writePage(id, *page)` — `seek` to `id * PAGE_SIZE` and
  read/write exactly one page; writes are flushed immediately. Reads past
  end-of-file return a zeroed page rather than failing.

### Buffer pool
`BufferPool` ([buffer_pool.h](src/buffer_pool.h)) caches a fixed number of frames
in memory and is the only component that talks to `HeapFile` during normal
operation:
- **LRU eviction** via a `std::list<int>` recency list plus a
  `page_id → frame index` hash map.
- **Pin/unpin reference counting**: `getPage` pins (loading from disk on a miss),
  `unpinPage(id, dirty)` releases and records dirtiness; only unpinned frames are
  evictable.
- **Write-back**: a dirty page is flushed to disk when evicted.
- **Checkpoint**: `checkpointFlush()` writes *all* dirty frames to disk at once —
  used by the auto-checkpoint daemon (§8) to bound recovery time.
- A single `pool_lock` mutex makes the pool thread-safe.

### Page-backed heap table
The executor's `Table` ([execution.cpp](src/execution.cpp)) stores rows **in
slotted pages through a BufferPool over a `<name>.dat` heap file** (its B+ Tree
index is backed by `<name>.idx`). `insert` serializes the tuple, tries the last
page, and allocates a new one when full; `readRecord` pins the page, reads the
slot, and deserializes; a sequential scan walks page `0..N` and each page's live
slots, pinning/​unpinning as it goes. So real page allocation, reads/writes,
pin/unpin and eviction all happen *during query execution* — and a table's rows
survive a restart (reopened with `fresh=false`).

## 4. Indexing

### B+ Tree design
The primary-key index ([btree.h](src/btree.h), [btree.cpp](src/btree.cpp)) is a
**page-backed** B+ Tree mapping an `int64` key to a `RID`. Design choices:
- **One node = one page**, stored through the same BufferPool/HeapFile as the
  data, so the index is **durable and survives restarts**. The root page id
  lives in a meta page (page 0) and is read back on reopen.
- **Order 64** (`kOrder`) — large fan-out keeps the tree shallow.
- **Leaves form a singly-linked list** via a `next` *page id*, so an ordered
  range scan is "find the start leaf, then walk `next` pointers" — this powers
  `IndexScan` and range predicates.
- **Inserts split overflowing nodes** and push a separator up, growing a new root
  (a freshly allocated page) when the old root splits.
- **Deletes are tombstones**: the leaf entry's RID `page_id` is set to a sentinel
  and skipped; we skip merge/rebalancing on delete. Trade-off in
  [Limitations](#11-limitations).

### Node structure
Every node is a fixed-offset record inside its 4 KB page:
- **Header**: `is_leaf` (u8), `num_keys` (u16), `next` (i32 — next-leaf page id,
  −1 for internal/last leaf).
- **Leaf**: `keys[]` (`int64`) followed by `rids[]` (`page_id` u32 + `slot_id`
  u16). **Internal**: `keys[]` (separators) followed by `children[]` (i32 page
  ids, size = `keys + 1`). Capacity is `kOrder+1` to hold the brief pre-split
  overflow; both fit comfortably in a page.

### Search path
- **Point search** (`search(key)`): from the root page, at each internal node take
  the first child whose separator is `> key`, descend to a leaf, scan for `key`,
  and return its `RID` if present and not tombstoned — pinning/​unpinning each
  page on the way down.
- **Range scan** (`range(low, high)`): the leaf-linked `Iterator` copies one
  leaf's entries at a time and follows `next` page ids (so it never holds a page
  pinned across iterations), skipping tombstones and stopping past `high`.

---

## 5. Query Execution

### Parser
SQL is parsed by a small, two-stage front-end ([parser.h](src/parser.h),
[parser.cpp](src/parser.cpp)):

1. **Lexer** (`tokenize`) turns raw SQL text into a flat token stream —
   case-insensitive keywords, identifiers, qualified `table.column` references,
   integer and single-quoted string literals (`''` escapes a quote), comparison
   operators (`= != <> < <= > >=`) and punctuation.
2. **Recursive-descent parser** (`Parser::parse`) consumes the tokens into a
   typed AST (`Statement`, a `std::variant` of `Select`/`Insert`/`Delete`),
   reporting precise errors on malformed input. Supported grammar:

   ```
   [EXPLAIN] SELECT <*|col,…> FROM <t> [JOIN <t2> ON <col> = <col>] [WHERE <bool-expr>]
   [EXPLAIN] INSERT INTO <t> VALUES (<lit>, …)
   [EXPLAIN] DELETE FROM <t> [WHERE <bool-expr>]
   ```

   `<bool-expr>` is a boolean expression of `col <op> lit` comparisons combined
   with `AND`/`OR` and parentheses (`AND` binds tighter than `OR`), parsed into a
   `WhereExpr` tree — e.g. `WHERE id >= 5 AND (name = 'x' OR id = 9)`.

The parser is independent of any one table — it works against whatever the
catalog holds — and is unit-tested in
[tests/track4_test.cpp](tests/track4_test.cpp).

### Query plan generation
The AST is handed to the **cost-based optimizer** (§6), which resolves columns
against the catalog and emits a physical **operator tree**:
- `INSERT` → an `Insert` operator (with arity/type checking).
- `DELETE … WHERE id = k` → `Delete(IndexScan[k,k])`; a non-PK predicate becomes
  `Delete(TableScan → Filter)`.
- `SELECT` → access path (`IndexScan` **or** `TableScan`, optionally `Filter`),
  an optional `NestedLoopJoin`, and a `Projection` for an explicit column list.

The same path drives the CLI, WAL replay, and the replication apply stream, so
JOINs, projections, and expression predicates are now reachable from SQL (not
just from C++ test code).

### Operator execution
Execution uses the **Volcano (iterator) model** ([execution.h](src/execution.h)):
every operator implements `open()` / `next(Tuple&)` / `close()`, and a parent
pulls tuples from its children one at a time. Because the interface is uniform,
operators compose into arbitrary trees and stream tuples without materializing
intermediates. Implemented operators:

| Operator | Behaviour |
|----------|-----------|
| `TableScan` | Sequential scan, skips `is_deleted` rows; takes a Shared lock |
| `IndexScan` | B+ Tree range scan over `[low, high]`; takes a Shared lock |
| `Filter` | Applies a `column <op> constant` predicate |
| `Projection` | Emits a subset of columns |
| `NestedLoopJoin` | Left-deep equi-join; re-scans inner per outer tuple |
| `Insert` | Appends a row + updates the index; takes an Exclusive lock |
| `Delete` | Tombstones matching rows; takes an Exclusive lock |

---

## 6. Optimizer

A small **cost-based optimizer** ([optimizer.h](src/optimizer.h),
[optimizer.cpp](src/optimizer.cpp)) turns the parser's AST into a physical plan,
making two classic decisions and reporting them in an `EXPLAIN` rendering.

### Cost estimation
The optimizer **costs each candidate access path and picks the cheaper one**:
- **`TableScan`** cost ≈ `N` (every row is touched).
- **`IndexScan`** cost ≈ `log₂(N) + matched_rows` (B+ Tree descent + leaf walk).

A comparison on the **integer primary key** with `=`, `<`, `<=`, `>`, `>=` is
index-eligible. For a compound `WHERE`, the planner looks for such a comparison
among the **top-level `AND` conjuncts** (it can narrow the scan because every
conjunct must hold) but never descends through an `OR` (the other branch could
match any key). When an eligible conjunct exists, the planner costs both paths
and picks the cheaper; the **full** boolean expression is then re-checked by a
residual `Filter` (so the index is a pure optimization and the result is always
correct). Example `EXPLAIN`:

```
SELECT name FROM students WHERE id = 42
Projection [name]
  IndexScan(students) PK [42, 42]  est_rows=1.0 cost=12.0  (chosen over TableScan cost=1000.0)
```

This is a row-count model, not a page/IO model — calibrating it against the
buffer pool is future work.

### Selectivity estimation
Selectivity is estimated **structurally** (no histograms): equality on the
unique primary key ≈ `1/N` (one row) → favours the index; a range ≈ `1/3` of the
table; an unbounded predicate ≈ `1` → favours a scan. For compound predicates
the tree is combined assuming independence — `AND` multiplies selectivities,
`OR` uses inclusion–exclusion (`a + b − a·b`). These feed both the access-path
cost above and the join-order decision below.

### Join ordering
Joins are **left-deep nested-loop equi-joins**. Because the inner side is
re-scanned once per outer row, the optimizer makes the **smaller estimated
relation the outer (driving) side**, regardless of the order the tables were
written in the query:

```
SELECT * FROM students JOIN enroll ON students.id = enroll.sid
NestedLoopJoin  outer=enroll (est_rows=2.0)  inner=students (est_rows=1000.0)  cost=2002.0  [reordered: smaller relation drives]
```

There is no multi-way join-reordering search or hash/merge join — see
[Limitations](#11-limitations).

---

## 7. Transactions & Concurrency

### Transactions (auto-commit + undo)
Every statement runs as its own transaction
([transaction.h](src/transaction.h)). The `TransactionManager` hands out ids and
tracks `Active`/`Committed`/`Aborted`; a `Transaction` holds an in-memory **undo
log** — a list of closures pushed by the DML operators (`Insert` records "tombstone
this RID", `Delete` records "restore this slot + re-index"). On success the
statement **commits** (undo log discarded, locks released); on any error it
**aborts** — the undo log is replayed in reverse to roll the row changes back —
then locks are released. There is no MVCC; isolation comes from locking and
durability from the WAL.

### Locking strategy
Two complementary mechanisms:
1. **Table-level Strict 2PL** in the engine ([lock_manager.h](src/lock_manager.h)):
   Shared (read) and Exclusive (write) locks tracked in an
   `unordered_map<table, holders>`, with a Shared→Exclusive **upgrade** path.
   The real `LockManager` is now wired into every statement's `ExecContext`
   (scans take `S`, `INSERT`/`DELETE` take `X`); locks are held until
   commit/abort and released together (`release_all`).
2. **A coarse global DB lock** in [main.cpp](src/main.cpp) (`global_db_lock`) that
   serializes whole statements during interactive use and background replication
   apply, preventing races (e.g. during a B+ Tree split) between the CLI thread,
   the replica-apply thread, and the checkpoint daemon.

### Isolation guarantees
The combination provides **serializable** isolation. Strict 2PL forbids cascading
aborts and non-serializable interleavings; the global lock makes statement
execution effectively serial, which trivially guarantees serializability at the
cost of concurrency.

### Deadlock handling
**Timeout-based, not graph-based.** A lock request blocks on a condition variable
for at most **3 seconds**; on timeout `acquire()` throws `std::runtime_error`,
which the caller turns into an **abort**. This avoids building/cycle-checking a
wait-for graph while guaranteeing no transaction blocks forever. The trade-off is
possible false positives (a merely-slow transaction can be aborted).

---

## 8. Recovery

### WAL design
Write-Ahead Logging ([wal.h](src/wal.h), `LogManager`) with **transaction
framing**, so recovery can replay only what actually committed. Each record is a
line flushed to disk before it is acknowledged:

```
BEGIN <txn>
STMT <txn> <sql text>
COMMIT <txn>
```

On a write the primary logs `BEGIN`+`STMT` (durably) *before* applying, and logs
`COMMIT` only after the statement succeeds locally. A `BEGIN`/`STMT` with no
matching `COMMIT` — a crash mid-statement — is skipped on recovery, giving
**preservation of committed transactions**.

### Log records
Logging is **statement-level (logical/redo)**: the record is the raw SQL text, so
replay is identical to normal execution. (Physical page-level logging / LSNs are
not used — a documented simplification; see [Limitations](#11-limitations).)

### Crash recovery procedure
On primary startup ([main.cpp](src/main.cpp)):
1. Tables are reopened from their durable `.dat`/`.idx` files (the state as of the
   last checkpoint).
2. `committedStatements("wal.log")` extracts the SQL of every **committed**
   transaction (post-checkpoint), which is replayed through `execute_sql_statement`.
3. A checkpoint flushes the now-consistent pages and the WAL is truncated.

Demo: `INSERT` rows → kill the process → restart → the committed rows are still
there. A background **auto-checkpoint daemon** runs every 5 minutes: it takes the
global lock, calls `Catalog::checkpointAll()` to flush every table's dirty data
and index pages, then truncates the WAL — bounding future recovery work. (A clean
`exit` does the same so a normal restart sees durable data and an empty log.)

---

## 9. Extension Track: Distributed Systems (Replication)

### Motivation
We chose **Track D** because our durability design already produces the exact
artifact a distributed system needs: an **ordered log of committed mutations**.
That makes **log-shipping replication** a natural, low-risk extension rather than
a bolt-on — one source of truth (the WAL) feeding two consumers: local recovery
and a remote replica.

### Design
A two-node **primary/replica** topology ([replication.cpp](src/replication.cpp)):
- The **primary**, on each write, (1) appends to the WAL, (2) opens a TCP
  connection to the replica and **synchronously** sends the statement, then (3)
  blocks for an `OK` acknowledgement with a 2 s `SO_RCVTIMEO` timeout. Only if the
  replica ACKs does the primary commit locally; otherwise it **aborts** — keeping
  the two nodes consistent.
- The **replica** runs a TCP server (`startReplicaServer`) that receives each
  statement, applies it through the same executor (via a callback), and replies
  `OK`. Replicas are **read-only**: the CLI bounces `INSERT`/`DELETE`.
- Because the replica replays statements in commit order, it converges to the
  primary's state — providing consistent read replicas.

### Results
- **Synchronous replication cost:** Under synchronous replication, write statements must be ACKed by the replica before committing on the primary. The Write Storm benchmark achieves **15,905 QPS** (average latency of **64.03 µs** per write query), indicating that throughput is bound by loopback round-trip and TCP network socket overhead.
- **Failure detection:** A replica crash or network hang aborts the transaction on the primary in exactly **2.00 s** (the socket receive timeout), preventing the primary from blocking indefinitely.

---

## 10. Benchmarks

The project includes an end-to-end performance benchmark that targets the live, running system:
- `live_query_benchmark.py` ([benchmarks/live_query_benchmark.py](benchmarks/live_query_benchmark.py)) — the end-to-end database pipeline benchmark executing queries against the live, running `minidb` subprocess processes via standard streams IPC.

### Experimental setup
- **Machine:** Asus TUF F15 / 12th Gen Intel Core i7.
- **CPU:** 12th Gen Intel(R) Core(TM) i7-12700H — 14 cores / 20 threads, up to 4.7 GHz, 24 MB cache.
- **RAM:** 16 GB DDR4.
- **Storage:** M.2 NVMe SSD.
- **OS:** Fedora Linux (Workstation Edition).
- **Network:** nodes over `127.0.0.1` (loopback TCP) — local synchronous log shipping.
- **Config:** page size 4096 B; buffer pool 64 frames (data pool) / 128 frames (index pool); global-lock concurrency control.

### Methodology
To measure the complete end-to-end system performance, the `live_query_benchmark.py` harness launches the actual compiled `./minidb` executable in both `primary` and `replica` modes as live background subprocesses:
- Environment variables `stdbuf -oL -eL` are used to enforce line-buffering on the child process's standard output.
- Queries (INSERTs, SELECTs) are piped into the primary node's standard input (`stdin`).
- The harness reads the child process's stdout line-by-line using Python subprocess I/O, waiting until the print logs confirm completion (e.g., reading until `[WAL] COMMIT logged` for inserts or `(X rows)` for selects).
- The benchmarks run 100 iterations of 1,000 queries per workload to average out scheduling jitter, process context switches, and network latency, calculating arithmetic means for throughput and percentiles.

### Results

> **Note:** The numbers below reflect the fully page-backed relational query engine (slotted pages, LRU buffer pool, on-disk persistent B+ Tree, WAL logging, and TCP loopback replication) running on the specified experimental setup.

These benchmarks measure the actual user-facing performance when piping SQL text commands to the live primary process (replicated to replica):

| Workload | Throughput | Avg | p50 | p90 | p99 |
| :--- | :---: | :---: | :---: | :---: | :---: |
| Write Storm (INSERT) | **15,905 QPS** | 64.03 µs | 49.96 µs | 102.23 µs | 191.94 µs |
| Point Lookups (SELECT PK) | **94,133 QPS** | 10.90 µs | 10.28 µs | 12.34 µs | 22.46 µs |
| Full Table Scans (SELECT \*) | **2,023 QPS** | 497.23 µs | 489.58 µs | 577.91 µs | 577.91 µs |
| Mixed CRUD (70/30) | **39,753 QPS** | 25.70 µs | 11.57 µs | 56.48 µs | 117.16 µs |

### Analysis
- **Index vs. scan.** Point lookups dramatically beat per-row scan cost — the full-scan workload pays milliseconds to traverse rows, validating the B+ Tree for selective queries.
- **Process I/O & IPC overhead.** Processing SQL queries sequentially through the live process standard streams IPC introduces formatting, parsing, and inter-process communication overhead.
- **Synchronous replication bottleneck.** Replicating mutations synchronously before committing caps write throughput (Write Storm is at **15,905 QPS**), as the primary blocks waiting for replica TCP loopback network ACKs.
- **Checkpoint trade-off.** The 5-minute checkpoint daemon flushes dirty pages and truncates the WAL, introducing a brief latency spike in exchange for bounded recovery time — a classic systems trade-off.

---

## 11. Limitations

We would rather document what is incomplete or fragile than hide it.

### Missing features
- **WHERE is comparisons + `AND`/`OR` only.** Predicates combine
  `column <op> literal` leaves with `AND`/`OR` and parentheses; there is no
  expression arithmetic, `IN`/`LIKE`/`BETWEEN`, column-to-column comparison,
  `ORDER BY`, `GROUP BY`, or aggregation.
- **One JOIN, nested-loop only.** `SELECT` supports a single two-table equi-join;
  there is no multi-way join, no hash/merge join, and no join-reordering search
  beyond the smaller-drives-outer heuristic. A `WHERE` confined to one table is
  pushed below the join (and may use that table's index); a predicate spanning
  both tables is applied as a `Filter` on the join output rather than pushed.
- **Row-count cost model, no statistics.** The optimizer estimates selectivity
  structurally (PK eq ≈ 1/N, range ≈ 1/3); there is no histogram/stats catalog,
  and costs are in row counts rather than calibrated page/IO units.
- **No primary-key uniqueness enforcement.** `Table::insert` adds an index entry
  unconditionally; a duplicate PK is silently accepted.
- **Single integer primary-key index only.** No secondary, composite, or text-key
  indexes; `DELETE` requires an integer PK.
- **Joins are nested-loop, equi-join only** — no hash/merge join, no reordering.
- **No MVCC.** Transactions are auto-commit per statement with an in-memory undo
  log for rollback; there are no multi-statement transactions or snapshots.

### Scalability limits
- **Coarse concurrency.** Table-level 2PL plus a global statement lock serialize
  execution; throughput does not scale with cores.
- **Synchronous replication** bounds write throughput to network RTT and acknowledgement round-trip times (Write Storm is at **15,905 QPS** on loopback).
- **Tombstone deletes never reclaim space** — neither slotted-page slots nor B+
  Tree leaf entries are compacted, so delete-heavy workloads grow unbounded.
- **B+ Tree duplicate keys across leaves under-count** in range scans. Benign
  while PKs are unique, but unguarded.
- **Logical-redo recovery can double-apply** a committed statement whose dirty
  page was evicted to the data file *before* a crash (no LSNs to detect that the
  page already reflects the change). For the demo (small data, ample buffer) no
  such eviction occurs; a fix needs page LSNs.

### Future improvements
- Extend the lexer/parser + optimizer with aggregation/`GROUP BY`, `ORDER BY`,
  richer predicates (`IN`/`BETWEEN`/`LIKE`), and a cost model calibrated with
  table statistics.
- Page LSNs so recovery is idempotent regardless of eviction timing.
- Enforce PK uniqueness; B+ Tree merge/redistribute and space reclamation.
- Finer-grained (row/page) locking and/or MVCC for real concurrency.
- Hash/merge joins and join reordering.

> **Platform note:** `replication.cpp` uses POSIX sockets and builds/runs on the
> Linux target (and macOS). On Windows/MinGW it needs a small Winsock2 shim, so
> the socket-dependent targets (`minidb`, the benchmarks, `track1_2_test`) are
> Linux-only there; the page-backed query engine and its tests
> (`track3_test` / `track4_test` / `track5_test`) build and run on all platforms.

---

## 12. How to Run

### Dependencies
- **CMake ≥ 3.12** and a **C++17 compiler** (tested with g++ on Fedora Linux).
- **POSIX threads** (`Threads::Threads`) and **POSIX sockets** (for replication;
  Linux/macOS).

### Build steps
```bash
cd MiniDB_Projects/Team_JustChill
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Targets produced: `storage` (WAL + replication), `query_engine` (the page-backed
engine: storage core + index + SQL front-end + transactions); tests
`track3_test`, `track4_test`, `track5_test`, `track1_2_test`; and `minidb` (server).

### Run the tests and live benchmark
```bash
cd build
ctest --output-on-failure     # track3 + track4 + track5 (+ track1_2 on Linux)
cd ..
python3 benchmarks/live_query_benchmark.py
```
`track5_test` covers the storage integration: slotted pages, the page-backed
heap table + B+ tree (incl. eviction and persistence across reopen),
transactions/undo, and committed-only WAL recovery.

### Run the database (two-node primary/replica demo)
```bash
# Terminal 1 — start the replica (listens on port 9999):
./minidb replica 127.0.0.1 9999

# Terminal 2 — start the primary (replicates to the replica):
./minidb primary 127.0.0.1 9999
```

Example session on the **primary** prompt:
```sql
INSERT INTO students VALUES (1, 'Alice')
INSERT INTO students VALUES (2, 'Bob')
DELETE FROM students WHERE id = 1
SELECT * FROM students
exit
```
Each write is logged to `wal.log`, shipped to the replica (which must ACK before
commit), then applied locally. On the **replica** prompt, `SELECT * FROM students`
shows the replicated state; write attempts are rejected (read-only). Restarting
the primary with a non-empty `wal.log` triggers crash-recovery replay.

---

## Appendix — Key design decisions

| Decision | Rationale |
|----------|-----------|
| **Volcano (iterator) execution** | `open/next/close` composes operators into arbitrary trees and streams tuples with no intermediate materialization. |
| **B+ Tree primary index** | Linked sorted leaves give O(log n) lookups *and* efficient ordered range scans for `IndexScan`. |
| **Tombstone deletes (no rebalance)** | Constant-time deletes and simpler code; trade-off is unreclaimed space. |
| **Table-level + global locking** | Correct serializable isolation with minimal bookkeeping; trades concurrency for simplicity and safety during B+ Tree splits. |
| **Timeout-based deadlock handling** | Avoids wait-for-graph machinery while guaranteeing no permanent blocking. |
| **Statement-level WAL** | Human-readable log; replay = normal execution; doubles as the replication stream. |
| **Synchronous replication** | Strong consistency (replica ACK before commit) at the cost of write latency bound by network RTT. |
