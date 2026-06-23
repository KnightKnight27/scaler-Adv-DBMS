# PostgreSQL Internal Architecture

**Name:** Om Malviya &nbsp;|&nbsp; **Roll Number:** 24BCS10448

> A deep dive into five PostgreSQL internals: the **buffer manager**, the **B-tree** access method, **MVCC** tuple versioning, **WAL**, and the **cost-based planner**, and how they fit together. Every claim below is backed by output captured from a live PostgreSQL **16.14** server using `pageinspect`, `pg_buffercache`, and `EXPLAIN ANALYZE`; commands and real output are quoted inline in [Section 5](#5-experiments--observations).

---

## 1. Problem Background

PostgreSQL is a client-server, disk-oriented, MVCC relational database. The central design problem it must solve repeatedly is: *data lives on slow disk, RAM is small and volatile, and many transactions run at once: how do we serve fast, correct, durable reads and writes anyway?*

The answer is a layered architecture, and each layer in this document exists to solve one piece:

- **Buffer manager**: bridges slow disk and fast RAM (caching + write-back).
- **B-tree**: turns O(n) scans into O(log n) lookups on disk pages.
- **MVCC**: lets readers and writers run concurrently without blocking each other.
- **WAL**: guarantees committed data survives a crash without fsyncing every page.
- **Planner + statistics**: chooses *how* to execute a query from many equivalent options.

These four goals are always in tension:

```
          Performance
               |
   Concurrency --- Durability
               |
           Correctness
```

Improving one typically hurts another. PostgreSQL does not eliminate these tensions: it manages them. Historically these choices trace back to the 1986 Berkeley POSTGRES project, whose "no-overwrite storage" idea directly produced today's append-style MVCC and the need for VACUUM.

---

## 2. Architecture Overview

```
   SQL query
      │
   Parser              → builds a syntax tree
      │
   Rewriter            → expands views / rules
      │
   Planner / Optimizer → picks cheapest plan  ◄─── pg_statistic
      │                                            (n_distinct, MCVs, histograms)
   Executor
      │  page requests
      ▼
 ┌──────────────────────────────────────────────────┐
 │  SHARED MEMORY                                     │
 │    Buffer Manager   (shared_buffers, clock-sweep)  │
 │    WAL buffers                                     │
 └───────┬───────────────────────────────┬────────────┘
         │ 8 KB pages                     │ change records
         ▼                                ▼
   Heap + B-tree files              pg_wal/  (16 MB segments)
   base/<db>/<relfilenode>          fsync on commit
         ▲
         │  flush dirty pages  /  reclaim dead tuples
   checkpointer  ·  background writer  ·  autovacuum
```

**Data flow of a read:** parser → planner (consults statistics) → executor → buffer manager. If the 8 KB page is already in `shared_buffers` it is a *hit*; otherwise it is read from the heap/index file into a buffer (a *miss*). **Data flow of a write:** the change is recorded in **WAL buffers** first (write-ahead), the in-memory page is modified and marked *dirty*, and only later does the checkpointer flush the dirty page to its data file.

### End-to-End: What Actually Happens on `UPDATE accounts SET balance = 900 WHERE id = 1`

```
Client sends query
  Parser      → parse tree built
  Analyzer    → table/column names validated
  Planner     → picks Index Scan on accounts_pkey (cheapest path)
  Executor    → starts running the plan
  B-Tree      → traverses accounts_pkey, finds tuple's physical location (ctid)
  Buffer Mgr  → loads that heap page into shared_buffers (or hits cache)
  MVCC check  → visibility of current row version confirmed
  heap_update → stamps xmax on old tuple, inserts new tuple with xmin=<current XID>
  WAL         → XLogInsert() records the change in WAL buffers
  COMMIT      → XLogFlush() fsyncs WAL to disk; client gets success
  Background  → Background Writer eventually flushes the dirty heap page to disk
  Autovacuum  → later reclaims the old dead tuple (VACUUM)
```

The philosophy: do the minimum to guarantee correctness at commit time, and push everything else to background processes.

---

## 3. Internal Design

### 3.1 Buffer Manager (`src/backend/storage/buffer/`)

`shared_buffers` is a fixed pool of 8 KB frames in shared memory, accessible by every backend. Pages are found via a hash table; a **clock-sweep** algorithm using a per-buffer `usagecount` chooses victims for eviction. Dirty pages are written back lazily by the background writer and at checkpoints, not synchronously on every change (that is WAL's job).

The **WAL Rule** is never violated: a data page containing changes at LSN X must never reach disk before the WAL record at LSN X is durable. Violating this means a crash could leave disk in a state that WAL cannot explain.

> Verified in [Experiment 3](#experiment-3--buffer-manager): after scanning `orders`, its pages sit in shared buffers with `usagecount ~5` and some marked dirty.

### 3.2 B-tree (`src/backend/access/nbtree/`)

PostgreSQL's default index is a **Lehman-Yao high-concurrency B-tree**. Key properties:
- A **meta page** (block 0) points at the current root.
- Internal (root and intermediate) pages hold *(key → downlink)* separators; leaf pages hold *(key → heap ctid)*. The heap is separate: PostgreSQL indexes are secondary, never clustered.
- **Search path:** meta → root → ... → leaf, binary-searching keys at each level.
- **Insert / page split:** when a leaf fills, it splits in two and a new separator is pushed up; if the root splits, tree height grows by one. The "high key" + right-link design lets searches proceed correctly even *during* a concurrent split.

A B-tree over millions of rows is only 3-4 levels deep because each 8 KB page stores hundreds of entries. That is why B-trees win over binary search trees for disk-based data: fewer disk reads per lookup.

> Verified in [Experiment 4](#experiment-4--b-tree-internals): `orders_pkey` is a height-1 tree with a root page (type `r`) holding 137 downlinks pointing to leaf pages.

### 3.3 MVCC: heap tuple versioning

PostgreSQL never updates a row in place. Every heap tuple carries hidden system columns:

| Column | Meaning |
|---|---|
| `xmin` | XID of the transaction that **created** this version |
| `xmax` | XID of the transaction that **deleted/superseded** it (0 = still live) |
| `ctid` | physical address `(page, offset)` of this tuple; on update, the old tuple's `ctid` points to the new version |

An **UPDATE** means: insert a new tuple version + set the old version's `xmax`. A snapshot records which XIDs were committed at statement/transaction start; the **visibility rule** is: *a tuple is visible if its `xmin` is committed-and-before-my-snapshot and its `xmax` is not*: giving each transaction a consistent point-in-time view (snapshot isolation).

> Verified in [Experiment 1](#experiment-1--mvcc-tuple-versioning): an UPDATE moved row `id=1` from `ctid (0,1)` to `(0,3)` with a new `xmin`, while the old version physically remained with `t_xmax` set to the updating XID.

### 3.4 Why VACUUM is necessary

Because old versions are left behind, dead tuples accumulate ("bloat") and consumed XIDs march toward the 32-bit **wraparound** limit. **VACUUM** reclaims dead tuples' space for reuse and **freezes** very old tuples to prevent wraparound; `autovacuum` does this automatically. This is the direct cost of PostgreSQL's no-overwrite MVCC. InnoDB avoids it by updating in place and keeping old versions in a separate undo log (see [MySQL/InnoDB](../MySQL_InnoDB/README.md)).

> Verified in [Experiment 2](#experiment-2--vacuum): 5 rounds of updates left 13 physical tuples on the page for 2 live rows; `VACUUM` reported `tuples: 11 removed, 2 remain`.

### 3.5 WAL: Write-Ahead Logging (`src/backend/access/transam/`)

The rule: **a change's WAL record must reach durable storage before the corresponding data page does.** WAL records are appended to 16 MB segment files in `pg_wal/`, ordered by a monotonically increasing **LSN** (Log Sequence Number). On commit, WAL up to that LSN is `fsync`-ed (`synchronous_commit=on`). A **checkpoint** flushes all dirty buffers and records a safe restart point; **crash recovery** replays WAL forward from the last checkpoint. `full_page_writes` guards against torn pages by logging a full image of a page the first time it is touched after a checkpoint.

> Verified in [Experiment 5](#experiment-5--wal): a single UPDATE advanced the WAL LSN, and a forced `CHECKPOINT` succeeded with `fsync=on, full_page_writes=on`.

### 3.6 Planner and Statistics (`src/backend/optimizer/`)

The planner is **cost-based**: it enumerates equivalent plans (scan methods, join methods, join orders) and picks the lowest estimated cost. Estimates come from statistics gathered by `ANALYZE` into **`pg_statistic`** (readable via the `pg_stats` view): per-column `n_distinct`, null fraction, **most-common-values (MCV)** + frequencies, and histograms. Selectivity of `WHERE status='X'`, for example, is read straight from the MCV frequency.

A query planner is only as good as its statistics. Let stats go stale and the same query can get a catastrophically bad plan.

> Verified in [Experiment 6](#experiment-6--planner--statistics): the planner estimated **8335 rows** for a join filter and the executor returned **exactly 8335**: because the MCV stats for the filtered column were accurate.

---

## 4. Design Trade-Offs

| Decision | Benefit | Cost / limitation |
|---|---|---|
| **No-overwrite MVCC** | Readers never block writers; cheap rollback (just do not commit) | Table bloat; **requires VACUUM**; XID wraparound risk |
| **Secondary (non-clustered) B-tree** | Cheap to add many indexes; heap order independent of any index | Index lookup needs a second hop to the heap (no clustering locality) |
| **Buffer manager (write-back)** | Absorbs repeated writes in RAM; sequential WAL instead of random page fsyncs | Needs checkpoints + recovery; tuning `shared_buffers` matters |
| **WAL** | Durability without fsync-per-page; enables replication + PITR | Write amplification (data written to WAL *and* heap); checkpoint I/O spikes |
| **Cost-based planner** | Adapts to data distribution; great on complex joins | Only as good as its statistics: stale stats lead to bad plans |

### PostgreSQL vs. InnoDB MVCC

| | PostgreSQL (append to heap) | InnoDB (undo log) |
|---|---|---|
| Reader path | Read heap tuple directly | May follow undo chain |
| Table bloat | Yes, requires VACUUM | Less bloat (old versions leave the table) |
| Long-running txn impact | Old versions stay in heap | Undo log grows |
| Implementation | Simpler visibility logic | More complex undo management |

Neither is strictly better: they are engineering choices with different performance profiles.

---

## 5. Experiments / Observations

All commands were run against database `advdbms` (the dataset from [Topic 1](../PostgreSQL_vs_SQLite/README.md)); outputs below are quoted directly from the live runs.

### Experiment 1: MVCC tuple versioning
```
After INSERT:        ctid (0,1) xmin 1473  alice 1000
After UPDATE bal=900: ctid (0,3) xmin 1474  alice 900     <- new version, new ctid

Raw heap page 0:
 lp | t_ctid | t_xmin | t_xmax
  1 | (0,3)  |  1473  |  1474   <- OLD alice: superseded by 1474, forward-pointer to (0,3)
  2 | (0,2)  |  1473  |     0   <- bob: untouched
  3 | (0,3)  |  1474  |     0   <- NEW alice: live
```
> **Insight:** an UPDATE is insert-new + mark-old. The old version does not disappear: which is exactly why VACUUM has to exist.

### Experiment 2: VACUUM
```
13 raw tuples on page 0   (for only 2 live rows)
VACUUM (VERBOSE): "tuples: 11 removed, 2 remain"
n_dead_tup after VACUUM: 0
```
> **Insight:** churn produced 11 dead versions; VACUUM reclaimed them. Without it the heap grows without bound even though logical row count is constant.

### Experiment 3: Buffer Manager
```
shared_buffers = 16384 * 8kB (128 MB),  block_size = 8192
After SELECT count(*) FROM orders:
  relname     | buffers | dirty | avg_usage
  order_items |   1278  |  30   |  5.00
  orders      |    349  |   0   |  4.99
  customers   |     77  |   0   |  4.99
```
> **Insight:** scanned pages are now resident with a high `usagecount` (clock-sweep keeps hot pages), and dirty pages await write-back: direct evidence of the caching layer.

### Experiment 4: B-tree internals
```
bt_metap(orders_pkey): root=block 3, level=1        <- height-1 tree
root page stats:        type=r, live_items=137, free_size=5416
root page items:        itemoffset 1 -> ctid (1,0); 2 -> (2,1); 3 -> (4,1) ...   (downlinks)
```
> **Insight:** the root holds 137 separator keys, each a downlink (`ctid`) to a child page: a real on-disk B-tree, not a hand-wave. With ~137 fan-out per level, two levels already index 50k rows.

### Experiment 5: WAL
```
pg_current_wal_lsn before: 0/119F70E8
... UPDATE accounts ...
pg_current_wal_lsn after : 0/119F7298     <- LSN advanced (WAL record appended)
CHECKPOINT  -> success
fsync=on, full_page_writes=on, synchronous_commit=on
```
> **Insight:** the write was logged (LSN moved) *before* the page needed flushing; the checkpoint establishes a recovery start point. This is durability without fsync-per-page.

### Experiment 6: Planner & Statistics
```
Hash Join (cost=135.88..1112.18 rows=8335) (actual ... rows=8335)   <- estimate == actual
  -> Seq Scan on orders   (rows=50000 actual 50000)
  -> Bitmap Index Scan on idx_customers_country (country='JP')

pg_stats orders.status: n_distinct=4,
  most_common_vals  = {CANCELLED,PLACED,SHIPPED,DELIVERED}
  most_common_freqs = {0.251, 0.251, 0.249, 0.248}
pg_statistic: starelid=orders, staattnum=3, stadistinct=4   (raw form behind pg_stats)
```
> **Insight:** the planner's row estimate (8335) matched reality exactly because the MCV frequencies are accurate. This is the whole game: good statistics lead to good estimates lead to good plans. Let stats go stale and the same query can get a bad plan.

---

## 6. Key Learnings

1. **The internals are deeply interconnected.** MVCC *causes* the need for VACUUM; the buffer manager's write-back *requires* WAL for durability; the planner is only as good as `ANALYZE`'s statistics. You cannot understand one in isolation.
2. **"No overwrite" is the keystone decision.** PostgreSQL's append-style MVCC gives lock-free reads but pays for it in bloat and VACUUM: a trade-off visible directly in Experiments 1 and 2.
3. **Estimates vs. actuals is the planner's report card.** Seeing `rows=8335 (actual rows=8335)` is the clearest proof that statistics drive planning; a large gap is the number one symptom to debug.
4. **Indexes are secondary here.** The B-tree leaf stores a `ctid` into a separate heap: unlike InnoDB's clustered index where the row lives in the leaf. This single fact explains many performance differences between the two engines.
5. **Surprising observation:** a height-1 B-tree with ~137-way fan-out already indexes 50,000 rows in two page reads: on-disk B-trees are extremely shallow, which is why they win over scans.
6. **Every optimization introduces a new problem.** Adding a buffer cache requires a replacement policy. The replacement policy introduces approximate eviction. Keeping tuple versions requires cleanup. Cleanup requires statistics on what is visible. The complexity is load-bearing.

---

## Source Code Map

| Subsystem | Location | Key Files / Functions |
|---|---|---|
| Buffer Manager | `src/backend/storage/buffer/` | `bufmgr.c`, `freelist.c`, `buf_internals.h` |
| Clock-Sweep | `freelist.c` | `StrategyGetBuffer()`, `nextVictimBuffer` |
| B-Tree Index | `src/backend/access/nbtree/` | `_bt_search()`, `_bt_doinsert()`, `_bt_split()` |
| MVCC / Heap | `src/backend/access/heap/` | `heap_update()`, `HeapTupleHeaderData`, `HeapTupleSatisfiesMVCC()` |
| WAL | `src/backend/access/transam/` | `XLogInsert()`, `XLogFlush()`, `xlog.c` |
| Query Optimizer | `src/backend/optimizer/` | `planner.c`, `costsize.c` |
| VACUUM | `src/backend/commands/vacuum.c` | `lazy_vacuum()`, visibility map, free space map |

---

## References
- PostgreSQL 16 Documentation: *Internals*: https://www.postgresql.org/docs/16/internals.html
  (Buffer Manager, `nbtree` README, MVCC/Concurrency Control, WAL, Planner/Statistics Used by the Planner)
- PostgreSQL source: `src/backend/storage/buffer/README`, `src/backend/access/nbtree/README`
- `pageinspect` and `pg_buffercache` contrib module docs
- Lehman and Yao, *"Efficient Locking for Concurrent Operations on B-Trees"* (1981)
- Hellerstein, Stonebraker, Hamilton: *Architecture of a Database System*

*All experiment outputs above were produced by running PostgreSQL 16.14 locally. Original work.*
