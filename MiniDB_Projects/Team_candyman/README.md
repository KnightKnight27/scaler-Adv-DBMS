# MiniDB

A small but real relational database engine written in Go for the Advanced DBMS
capstone. MiniDB integrates a page-based storage engine, a B+Tree index, a SQL
front end, a cost-based optimizer, two-phase-locking transactions, and
write-ahead-log crash recovery — and ships a second, pluggable **LSM-tree
storage engine** as its extension track.

Everything runs from a single CLI binary with an interactive SQL prompt; there is
no networking and no external database dependency (Go standard library only), so
every line can be explained and defended.

---

## Team

> PR title must be `TEAM_candyman`. Fill in each member's details before submission.

**Team name:** `candyman`

| Full name | Roll number | Scaler email |
|-----------|-------------|--------------|
| Vansh Malhotra | _SCALER..._ | _<scaler email>_ |
| Sarthak Arora  | 24BCS10150  | sarthak.24bcs10150@sst.scaler.com |

---

## 1. Project Overview

**Problem.** Build a working relational database from foundational components and
integrate them into one coherent system that can be demonstrated and defended,
rather than a large feature set that cannot be explained.

**Goals.**
- Page-based storage with a buffer pool and a B+Tree primary index.
- End-to-end SQL: `CREATE TABLE`, `INSERT`, `DELETE`, and `SELECT` with `WHERE`,
  `JOIN`, and aggregation.
- A cost-based optimizer that estimates selectivity, chooses index vs sequential
  scans, and orders joins.
- Serializable transactions via strict two-phase locking with deadlock
  detection.
- Durability via a write-ahead log and crash recovery.

**Chosen extension track: C — Modern Storage (LSM-tree).** A log-structured
merge-tree engine (MemTable + SSTables + compaction) implements the *same*
storage interface as the default heap engine, so it is a drop-in alternative
selected with `--engine lsm` and benchmarked head-to-head against the B+Tree
heap engine.

---

## 2. System Architecture

```
                         ┌──────────────────────────────┐
        SQL text ───────▶│  cmd/minidb  (REPL / scripts) │
                         └───────────────┬──────────────┘
                                         ▼
                         ┌──────────────────────────────┐
                         │  db.Session                  │  BEGIN/COMMIT/ROLLBACK,
                         │  - 2PL lock acquisition       │  autocommit, statement
                         │  - WAL logging + rollback     │  atomicity
                         └───────┬───────────────┬──────┘
                                 ▼               ▼
                  ┌────────────────────┐  ┌────────────────────┐
   sql (lexer/    │ planner            │  │ txn                │  lock manager,
   parser/AST) ──▶│  - selectivity     │  │  - 2PL S/X locks   │  wait-for-graph
                  │  - scan choice     │  │  - deadlock detect │  deadlock detection
                  │  - join ordering   │  └────────────────────┘
                  └─────────┬──────────┘
                            ▼
                  ┌────────────────────┐
                  │ executor (volcano) │  SeqScan, IndexScan, Filter,
                  │  pull-based ops    │  NestedLoopJoin, HashAgg, Project
                  └─────────┬──────────┘
                            ▼
        ┌───────────────────────────────────────────────┐
        │           storage.StorageEngine                │   ◀── the swap point
        ├───────────────────────┬───────────────────────┤
        │ engine.HeapEngine     │ lsm.Engine  (Track C)  │
        │  heap file + B+Tree   │  MemTable + SSTables    │
        │  via buffer pool      │  + compaction + bloom   │
        └───────────┬───────────┴───────────┬───────────┘
                    ▼                         ▼
        ┌────────────────────┐     ┌────────────────────┐
        │ storage            │     │ *.sst files        │
        │  page / diskmgr /  │     │ (sorted runs)      │
        │  bufferpool / heap │     └────────────────────┘
        └────────────────────┘
                    ▲
        ┌────────────────────┐
        │ recovery (WAL)     │  redo committed / undo losers on open
        └────────────────────┘
```

**Major modules** (`internal/`): `types` (values, rows, schemas, encoding),
`storage` (page, disk manager, buffer pool, heap file, the `StorageEngine`
interface), `index` (B+Tree), `engine` (heap engine), `lsm` (LSM engine),
`catalog` (schema persistence), `sql` (lexer/parser/AST), `planner` (optimizer),
`executor` (operators), `txn` (locking), `recovery` (WAL), `db` (session façade).

**Data flow.** REPL → lexer/parser → AST → planner+optimizer → physical operator
tree → executor pulls rows from the `StorageEngine`, all under transaction locks,
with every data change written to the WAL before commit.

---

## 3. Storage Layer

**Page format** (`internal/storage/page.go`). Fixed **4 KiB** slotted pages. A
16-byte header holds a page LSN, the next-page pointer (heap chaining), the slot
count, and the free-space pointer. The slot directory grows forward from the
header while records grow backward from the end of the page. Deletes set a
tombstone bit in the slot rather than compacting, so **record ids (RIDs) stay
stable** — important because the B+Tree maps keys to RIDs.

**Heap files** (`heapfile.go`). Each table is a singly-linked chain of pages.
`Insert` finds a page with room (or appends a new one), `Scan`/`Cursor` walk the
chain a page at a time, and `Delete` tombstones a slot. Scans defend against
torn/uninitialized pages left by a crash.

**Disk manager** (`diskmgr.go`). Reads/writes 4 KiB pages addressed by
`(FileID, PageID)` across two files: heap data and B+Tree index. Keeping them
separate lets recovery rebuild indexes by truncating the index file without
touching table data.

**Buffer pool** (`bufferpool.go`). Fixed-capacity frame cache with **LRU
eviction**, pin/unpin reference counting (pinned pages are never evicted), and
dirty write-back. It enforces the **write-ahead rule**: before any dirty data
page is flushed, the WAL is fsynced.

---

## 4. Indexing

**B+Tree** (`internal/index/btree.go`), disk-backed through the buffer pool, in
the index file.

- **Node structure.** One node per 4 KiB index page. A 7-byte header carries a
  leaf flag, the key count, and (for leaves) the next-leaf pointer for range
  scans. Leaves store `key → 6-byte RID`; internal nodes store `key → child
  page id`. Keys are length-prefixed, so fan-out adapts to key size; a node
  **splits when its serialized form would overflow a page**.
- **Search path.** Descend from the root, binary-searching each internal node to
  pick a child, until a leaf; binary-search the leaf for the key. Range scans
  find the start leaf and follow next-leaf pointers.
- **Keys** use an order-preserving encoding (`types.EncodeKey`): integers are
  sign-flipped big-endian so byte order matches numeric order; text is raw bytes.
  Hence `bytes.Compare` on encoded keys matches value order.
- **Deletes** are lazy (remove from the leaf, no merge/rebalance) — this avoids
  the bug-prone B+Tree merge logic. Indexes are rebuilt from the heap on startup,
  so under-full nodes never accumulate across runs.

The primary index is used during query execution: a primary-key equality
predicate is served by an `IndexScan` point lookup (see the optimizer).

---

## 5. Query Execution

**Parser** (`internal/sql`). A hand-written lexer feeds a recursive-descent
parser producing a small AST. Supported grammar: `CREATE TABLE` (with
`PRIMARY KEY`), `INSERT ... VALUES` (multi-row), `DELETE ... WHERE`,
`SELECT` with projection/`*`, `WHERE`, `JOIN ... ON`, comma joins, `GROUP BY`,
aggregates (`COUNT/SUM/AVG/MIN/MAX`), `EXPLAIN`, and `BEGIN/COMMIT/ROLLBACK`.

**Plan generation** (`internal/planner`). The planner classifies predicates into
single-table filters and join conditions, builds an access path per table,
orders the joins, and tops the tree with a filter and either a projection or a
hash aggregate.

**Operator execution** (`internal/executor`). Classic **volcano / pull-based**
iterators with `Open` / `Next` / `Close`:
- `SeqScan` — sequential heap/engine scan.
- `IndexScan` — primary-key point lookup via the engine's index.
- `Filter` — predicate evaluation.
- `NestedLoopJoin` — re-scans the inner operator per outer row.
- `HashAgg` — grouped/whole-table aggregation.
- `Project` — output expression evaluation.

---

## 6. Optimizer

A cost-based optimizer (`internal/planner/planner.go`):

- **Selectivity estimation.** Textbook defaults — equality `0.1`, inequality
  `0.9`, range `0.3` — combine to refine each relation's estimated cardinality
  from its base row count.
- **Scan choice.** An equality predicate on the primary key is turned into an
  `IndexScan` (estimated 1 row, ~`log N` cost); otherwise a `SeqScan` (estimated
  `N × selectivity`). The cheaper path wins.
- **Join ordering.** Relations are joined **smallest-estimated-cardinality
  first** in a greedy left-deep order, keeping the nested-loop inner side small.
- `EXPLAIN <query>` prints the chosen plan with the estimates that drove it.

Example (`EXPLAIN SELECT name FROM users WHERE id = 3`):

```
QUERY PLAN
-> Project
-> IndexScan users (pk = 3)  [est rows 1, of 4]
```

---

## 7. Transactions & Concurrency

**Locking strategy** (`internal/txn`). A lock manager with **shared (S)** and
**exclusive (X)** modes under **strict two-phase locking** — all locks are held
until commit/abort. The SQL layer locks at table granularity: `SELECT` takes S
locks on referenced tables, `INSERT`/`DELETE`/`CREATE` take X locks. (Per table
this means many readers or one writer; the lock manager itself supports
finer-grained resource names.)

**Isolation guarantees.** Strict 2PL provides **serializable** isolation: a
transaction never sees another's uncommitted writes (it blocks), and the order of
commits is a valid serial order.

**Deadlock handling.** The lock manager maintains a **wait-for graph**. When a
lock request would block, it adds the waits-for edges and runs a cycle check; if
a cycle is found the requesting transaction is chosen as the **victim**, aborted,
and its locks released so the other party proceeds.

Demonstrate it:

```bash
go run ./cmd/minidb demo locking    # a reader blocks behind an uncommitted writer
go run ./cmd/minidb demo deadlock   # two txns form a cycle; one is aborted
```

---

## 8. Recovery

**WAL design** (`internal/recovery`). An append-only, length-framed log of
logical, row-level records. MiniDB uses an immediate-apply (steal), no-force
buffer policy, so the log carries both redo and undo information.

**Log records.** `BEGIN`, `INSERT` (after-image), `DELETE` (before-image),
`COMMIT`, `ABORT`, `CHECKPOINT`. `COMMIT` fsyncs the log; a torn trailing record
from a crash mid-append is ignored on read.

**Crash recovery procedure** (on open, when the log is non-empty):
1. **Analysis** — a transaction is *committed* iff it has a `COMMIT` record.
2. **Redo** — re-apply, in log order, every change of a committed transaction.
3. **Undo** — reverse, in reverse log order, every change of a transaction that
   never committed (a *loser*).

Both phases are **idempotent** (Put is an upsert, Delete is a no-op when absent),
so recovery is safe to re-run after a crash *during* recovery. A clean shutdown
flushes the engine and resets the log, so no recovery is needed next time.
Indexes are derived data and are rebuilt from the recovered heap.

Demonstrate it:

```bash
go run ./cmd/minidb demo recovery   # commit data + leave an uncommitted txn,
                                     # "crash", reopen: committed redone, rest undone
```

---

## 9. Extension Track C — LSM-tree Storage

**Motivation.** The default heap+B+Tree engine does in-place, random-I/O writes.
A log-structured merge-tree turns writes into sequential appends, trading read
amplification for far higher write throughput — the canonical "modern storage"
design (LevelDB/RocksDB/Cassandra).

**Design** (`internal/lsm`).
- **MemTable** — an in-memory map; writes are O(1) (the write-throughput win),
  sorted only when flushed or scanned. Deletes write tombstones.
- **SSTables** — immutable sorted runs on disk. Each is opened with an in-memory
  sorted key index and a **Bloom filter**; a point lookup is a Bloom check, a
  binary search, then one seek+read.
- **Compaction** — the memtable flushes to a new L0 SSTable once it exceeds
  64 KiB; once ≥ 4 runs accumulate they are merged (newest value wins,
  tombstones dropped) into one — size-tiered compaction that reclaims space.
- **Reads** consult the memtable, then SSTables newest-to-oldest; the first hit
  (value or tombstone) wins.

It implements the same `storage.StorageEngine` interface, runs the same SQL, and
is durable via the shared WAL plus persisted SSTables.

**Results.** See §10 — at 200k rows the LSM engine showed ~**1.9× write
throughput** and a ~**27% smaller on-disk footprint** than the heap engine.

---

## 10. Benchmarks

**Experimental setup.** `go test -run Report -v ./benchmarks/...` and
`go test -run IndexVsSeqScan -v ./benchmarks/...`. Engine-level (no SQL/WAL
overhead), 200,000 rows of `(id INT PRIMARY KEY, payload TEXT)`, random point
reads. Numbers below are from a development laptop (Apple Silicon, Go 1.25);
absolute values vary by machine — the *ratios* are the point.

| Metric | Heap (B+Tree) | LSM-tree |
|--------|---------------|----------|
| Write throughput | ~13,970 ops/s | ~26,320 ops/s |
| Point-read latency | ~8.6 µs | ~1.6 µs |
| On-disk size | 14.48 MB | 10.60 MB |

**Optimizer — index vs sequential scan** (point lookup, 200k rows):

| Access path | Time per lookup |
|-------------|-----------------|
| IndexScan (B+Tree) | ~7.7 µs |
| SeqScan + Filter | ~14.6 ms |
| **Speedup** | **~1900×** |

**Analysis.**
- *Writes:* the LSM's append-only MemTable inserts beat the heap engine's
  in-place insert + B+Tree maintenance.
- *Reads:* at this scale the LSM's Bloom-filtered SSTable lookups are competitive
  with (here faster than) multi-level B+Tree traversal; with many un-compacted
  runs LSM read latency would rise (read amplification) — the classic trade-off.
- *Space:* the heap engine pays for slotted-page slack and a separate B+Tree
  index file; the LSM's packed sorted runs are more compact.
- *Optimizer:* turning a primary-key equality into a `log N` index lookup instead
  of an `O(N)` scan is ~1900× faster here — exactly why the optimizer prefers it.

To reproduce: `MINIDB_BENCH=1 go test -run 'Report|IndexVsSeqScan' -v ./benchmarks/...`.

---

## 11. Limitations

- **Types:** only `INT` (int64) and `TEXT`; no `UPDATE`, `ORDER BY`, `LIMIT`,
  subqueries, or `OR` in predicates.
- **Locking granularity:** table-level for SQL (correct, but lower concurrency
  than row-level, which the lock manager could support).
- **Recovery:** logical redo/undo assumes the heap *chain* on disk is intact;
  heap structural changes are not physically logged (no per-page LSN / ARIES
  CLRs). It holds for the demos and typical workloads but is a simplification
  versus full ARIES. No log truncation via periodic checkpoints (the log resets
  on clean shutdown / after recovery).
- **Indexes:** primary-key only (no secondary indexes); rebuilt from the heap on
  every open rather than persisted.
- **Optimizer:** greedy left-deep join ordering with fixed selectivity constants
  (no histograms); nested-loop joins only (no hash/merge join).
- **Scale:** single-process, single-node; `Scan`/`RangeScan` on the LSM engine
  materialize results.

Future work: secondary indexes, `UPDATE`/`ORDER BY`, row-level locking, ARIES
physical logging with checkpoints, hash joins, and histogram statistics.

---

## 12. How to Run

**Dependencies.** Go 1.25+ (standard library only).

**Build & test.**

```bash
go build ./...                                 # build everything
go test ./...                                  # unit tests
go test -race ./internal/txn/...               # concurrency tests under the race detector
MINIDB_BENCH=1 go test -run 'Report|IndexVsSeqScan' -v ./benchmarks/...   # benchmark report
```

**Interactive REPL.**

```bash
go run ./cmd/minidb --data ./data --engine heap     # B+Tree heap engine
go run ./cmd/minidb --data ./data2 --engine lsm     # LSM-tree engine
```

End statements with `;`. Meta-commands: `\dt` lists tables, `\q` quits.

**Run a script.**

```bash
go run ./cmd/minidb --data ./data --engine heap < demo/demo.sql
go run ./cmd/minidb --data ./data2 --engine lsm  < demo/demo.sql
```

**Demonstrations.**

```bash
go run ./cmd/minidb demo locking     # 2PL: reader blocks behind a writer
go run ./cmd/minidb demo deadlock    # deadlock detection + victim abort
go run ./cmd/minidb demo recovery    # WAL crash recovery
```

**Example session.**

```sql
CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT);
INSERT INTO users VALUES (1, 'alice', 30), (2, 'bob', 25);
SELECT name FROM users WHERE id = 1;        -- uses the index (see EXPLAIN)
SELECT COUNT(*), AVG(age) FROM users;
DELETE FROM users WHERE age < 28;
```

---

## Where does MiniDB store data?

All state lives in the directory passed via `--data` (default `./data`). The
files depend on the engine:

| File | Engine | Contents |
|------|--------|----------|
| `catalog.json` | both | Table names, schemas, and heap roots (human-readable). |
| `minidb.db` | heap | 4 KiB heap (table) pages. |
| `minidb.idx` | heap | 4 KiB B+Tree index pages (rebuilt on open). |
| `minidb.wal` | both | Write-ahead log; non-empty only after an unclean shutdown. |
| `lsm_<table>_<n>.sst` | lsm | Immutable sorted SSTable runs per table. |

A clean shutdown (exit the REPL with `\q`) flushes committed data and resets the
WAL, so the next open needs no recovery. If the process is killed, the WAL is
replayed on the next open to restore committed transactions.

> The two engines use different on-disk formats; point `--engine heap` and
> `--engine lsm` at **different** `--data` directories.

---

## Repository layout

```
cmd/minidb/        CLI: REPL, script runner, demo subcommands
internal/
  types/           value system, rows, schemas, encoding
  storage/         page, disk manager, buffer pool, heap file, StorageEngine iface
  index/           disk-backed B+Tree
  engine/          heap storage engine (heap file + B+Tree)
  lsm/             LSM-tree storage engine (Track C)
  catalog/         schema persistence
  sql/             lexer, parser, AST
  planner/         cost-based optimizer
  executor/        volcano operators + expression evaluation
  txn/             2PL lock manager, deadlock detection
  recovery/        write-ahead log + crash recovery
  db/              session façade tying it together
benchmarks/        engine comparison + optimizer benchmarks
demo/              demo.sql feature tour
docs/              design notes
```
