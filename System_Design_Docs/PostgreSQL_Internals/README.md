# PostgreSQL Internal Architecture

*An Advanced DBMS course technical document examining how PostgreSQL is built from the inside.*

---

## 1. Problem Background

PostgreSQL is the direct descendant of the **POSTGRES** project led by Michael Stonebraker at UC Berkeley, started in 1986. POSTGRES itself was the successor to **Ingres** (1970s), one of the first relational systems. The lineage matters because the design goals carry forward:

- **Ingres** proved the relational model could be implemented efficiently and introduced QUEL and early query optimization ideas.
- **POSTGRES** ("post-Ingres") was a research vehicle to fix what Stonebraker saw as the limits of pure relational systems: it added an **extensible type system**, **user-defined functions/operators**, **rules**, and—critically—a **no-overwrite storage manager**. Instead of updating rows in place, POSTGRES kept old versions, which gave it time-travel queries and laid the groundwork for what became MVCC.
- In 1994 two Berkeley grad students replaced QUEL with SQL, producing **Postgres95**, renamed **PostgreSQL** in 1996 when it became an open-source community project.

**What the internal design solves.** The core problems PostgreSQL's architecture targets are:

1. **Concurrency without readers blocking writers.** Traditional lock-based systems force readers and writers to contend. POSTGRES's no-overwrite philosophy evolved into **Multi-Version Concurrency Control (MVCC)**, where each transaction sees a consistent snapshot and readers never block writers.
2. **Durability and crash recovery.** A database must survive power loss without corruption. **Write-Ahead Logging (WAL)** provides this guarantee while keeping the common path fast.
3. **Performance under a memory hierarchy.** Disk is slow, RAM is fast and finite. The **buffer manager** decides what lives in memory and orchestrates page traffic.
4. **Fast lookups over large datasets.** B-tree indexes (a concurrent **Lehman-Yao B-link** variant) make point and range queries logarithmic while remaining safe under concurrent modification.
5. **Extensibility.** Custom types, index methods (GiST, GIN, BRIN), and functions are first-class, a direct inheritance from POSTGRES research.

The result is a process-per-connection, ACID-compliant, MVCC-based relational engine whose internals are unusually transparent and well-documented in source.

---

## 2. Architecture Overview

PostgreSQL uses a **multi-process** model (not threads). The `postmaster` accepts connections and forks a dedicated **backend** process per client. Background processes handle shared duties.

```
                        ┌──────────────────────────────────────────┐
   Client (psql/libpq)  │                POSTMASTER                  │
        │   TCP/socket   │   (listener; forks one backend per conn)  │
        └───────────────▶│                                            │
                         └───────────────┬────────────────────────────┘
                                         │ fork()
                                         ▼
        ┌────────────────────────────────────────────────────────────┐
        │                     BACKEND PROCESS                          │
        │  Parser ─▶ Analyzer/Rewriter ─▶ Planner/Optimizer ─▶ Executor│
        └───────────────────────────────┬──────────────────────────────┘
                                         │ read/write pages
                                         ▼
   ┌──────────────────────── SHARED MEMORY ───────────────────────────┐
   │   shared_buffers (page cache)   │   WAL buffers   │  lock tables  │
   └──────────┬───────────────────────────────┬───────────────────────┘
              │ flush dirty pages              │ flush WAL records
              ▼                                ▼
        ┌──────────────┐                 ┌──────────────┐
        │  Data files  │                 │  WAL segments│
        │ (heap+index) │◀── recovery ────│  (pg_wal/)   │
        └──────────────┘                 └──────────────┘

   Background helpers: BgWriter, WAL Writer, Checkpointer,
                       Autovacuum launcher/workers, Stats collector
```

**Data flow for a query (SELECT … JOIN …):**

1. **Parse** — raw SQL → parse tree (syntax only).
2. **Analyze & Rewrite** — resolve names/types against the catalog; apply rules and view expansion → query tree.
3. **Plan/Optimize** — the cost-based planner enumerates join orders and access paths, using statistics from `pg_statistic`, and emits the cheapest plan tree.
4. **Execute** — the executor walks the plan tree (Volcano-style iterators), requesting tuples. Each table/index access asks the **buffer manager** for the relevant 8 KB page.
5. **Storage** — pages come from `shared_buffers` (hit) or are read from disk into a buffer (miss). Modifications first generate **WAL records**, then dirty the in-memory page; the page itself is written back lazily.

The 8 KB **page** is the universal unit of I/O for both heap and index data.

---

## 3. Internal Design

### 3.1 Buffer Manager (`src/backend/storage/buffer/`)

All data and index pages reach the executor through the shared buffer pool sized by **`shared_buffers`**. The pool is an array of fixed 8 KB **buffer frames**, each described by a `BufferDesc` (in `buf_internals.h`).

A `BufferDesc` holds the **buffer tag** (relation + fork + block number identifying which page occupies the frame), a packed atomic state word containing **`refcount`** (pin count), **`usage_count`**, and dirty/valid flags, plus a content lock. A shared hash table maps a buffer tag → frame index, so a backend can ask "is block 42 of this relation in memory?" in O(1).

**Pinning.** Before touching a page a backend **pins** it (`ReadBuffer` / `PinBuffer`), incrementing `refcount`. A pinned buffer cannot be evicted. After use it is **unpinned**. Pinning is concurrency-cheap and orthogonal to the **content lock** (shared/exclusive) that protects the page's bytes.

**How pages move in and out — clock-sweep:**

```
   Eviction victim search (clock-sweep)
   ┌────────────────────────────────────────────┐
   │  nextVictimBuffer points into the ring      │
   │                                             │
   │   [B0]→[B1]→[B2]→...→[Bn]→ back to B0       │
   │     │                                       │
   │  visit a buffer:                            │
   │    if pinned (refcount>0)  → skip           │
   │    else if usage_count>0   → usage_count--, │
   │                              advance         │
   │    else (usage_count==0)   → EVICT this one  │
   └────────────────────────────────────────────┘
```

PostgreSQL does **not** use strict LRU. It uses a **clock-sweep** approximation (`StrategyGetBuffer` / `freelist.c`). A shared `nextVictimBuffer` cursor sweeps circularly. Each visited buffer's `usage_count` (capped, typically at 5) is decremented; a buffer is chosen as victim only when its `usage_count` hits 0 **and** it is unpinned. Frequently-touched (hot) pages keep getting their count bumped on access, so they survive; cold pages decay to 0 and get reclaimed. This avoids LRU's locking bottleneck while approximating its behavior.

**Page read (cache miss):** find a victim via clock-sweep → if the victim is **dirty**, first write it out (and ensure WAL up to that page's LSN is flushed — *WAL-before-data*) → read the requested block from disk into the frame → update the buffer tag and hash table → pin and return it.

**Page write:** modifications mark the buffer **dirty** but do **not** write to disk immediately (write-back, not write-through). Dirty pages are flushed later by:
- the **background writer (bgwriter)**, which trickles dirty pages out to smooth I/O,
- the **checkpointer**, which flushes all dirty buffers at checkpoint time,
- or a backend itself when it must evict a dirty victim.

This separation is what lets the hot working set stay in RAM and turns many logical writes into far fewer physical writes.

### 3.2 B-Tree Implementation (`nbtree`)

PostgreSQL's default index is a B-tree implementing the **Lehman-Yao B-link tree**, chosen specifically because it allows searches to proceed **without locking the entire path**, enabling high concurrency.

**Page layout.** Every index page is an 8 KB page with a header, an array of `ItemId` line pointers, and the index tuples. Logically the tree has a **meta page** (block 0, points to the root), internal pages, and leaf pages.

Two features define the B-link design:

- **Right-links:** every page stores a pointer to its **right sibling** at the same level. Levels are doubly reachable left-to-right.
- **High key:** each page stores a **high key**, an upper bound on the keys that legitimately belong on that page.

```
        ┌─────────────[ Internal/root ]─────────────┐
        │   sep keys → child pointers + highkey      │
        └───────┬───────────────────┬────────────────┘
                ▼                   ▼
   ┌──────[Leaf A]──────┐ →right→ ┌──────[Leaf B]──────┐ →right→ ...
   │ hk=50 | 10 25 40   │         │ hk=90 | 55 70 88   │
   └────────────────────┘         └────────────────────┘
```

**Search path.** Descend from the root following separator keys to a leaf. The trick that makes it lock-light: if, after a concurrent split, the key you seek is **greater than the page's high key**, the value has migrated rightward — you simply **follow the right-link** to the sibling instead of restarting from the root. A reader holding only a pin/short lock can therefore tolerate a concurrent split.

**Insert.** Find the target leaf, then insert in key order. If the page has room, done. If it is **full**, the page **splits**: roughly half the entries move to a new right page; the original page's right-link now points to the new page; the new page's high key and right-link are set; and a **new separator (downlink)** is propagated to the parent. The split is done in two atomic steps — the leaf-level link is established *before* the parent is updated. During that window, a searcher that lands on the left page and finds its key beyond the high key just follows the right-link, which is why the index remains correct mid-split without global locking.

PostgreSQL B-trees also support **deduplication** (storing one key with a posting list of TIDs) to reduce bloat for low-cardinality indexes, and **index-only scans** when a `VACUUM`-maintained visibility map confirms all tuples on a heap page are visible.

### 3.3 MVCC — Multi-Version Concurrency Control

PostgreSQL implements MVCC by **storing multiple physical versions of a row directly in the heap**. There is no separate undo/rollback segment (unlike Oracle/InnoDB); old versions live alongside new ones in the same table.

**Heap tuple header.** Each tuple carries:

| Field | Meaning |
|-------|---------|
| `t_xmin` | XID of the transaction that **inserted** (created) this version |
| `t_xmax` | XID of the transaction that **deleted/updated** it (0 if live) |
| `t_cid` | command id (intra-transaction ordering) |
| `t_ctid` | TID pointer to the **newer version** of this row (self-pointing if latest) |
| `t_infomask` / `t_infomask2` | flag bits: committed/aborted hints, HOT, etc. |

**How a write works.**
- **INSERT** creates a tuple with `xmin = currentXID`, `xmax = 0`.
- **DELETE** does not erase anything; it sets `xmax = currentXID` on the existing version.
- **UPDATE** = delete + insert: the old version gets `xmax = currentXID`; a brand-new tuple is written with `xmin = currentXID`, and the old tuple's `t_ctid` points to it.

**Visibility rules.** A transaction sees a tuple version if, simplified:
- its `xmin` is **committed and visible** to this transaction's snapshot, **and**
- its `xmax` is **invalid, aborted, or not yet visible** (i.e., it has not been deleted as far as this snapshot is concerned).

A **snapshot** records which XIDs had committed when the statement/transaction began (the set of in-progress XIDs and the XID horizon). Combined with the `t_infomask` commit hint bits (cached commit status to avoid repeatedly consulting the commit log `pg_xact`/CLOG), this lets each backend filter visible versions cheaply. This is **snapshot isolation**: readers see a frozen-in-time view and never block writers; writers conflict only when modifying the same row.

**HOT (Heap-Only Tuple) updates.** When an UPDATE changes only columns that are **not indexed** and the new version fits on the **same page**, PostgreSQL performs a HOT update: the new version is chained via `t_ctid` on the same page and **no new index entries are created**. Index pointers still aim at the original tuple; a small **HOT chain** is walked at read time. This dramatically reduces index churn and write amplification for update-heavy workloads.

**Why VACUUM is necessary.** Because deletes/updates leave old versions behind, tables accumulate **dead tuples** — versions no longer visible to any running transaction. They consume space (**bloat**) and slow scans. `VACUUM`:
- reclaims space from dead tuples (regular VACUUM marks it reusable; `VACUUM FULL` rewrites/compacts),
- removes dead index entries,
- updates the **visibility map** and **free space map**,
- and performs **freezing**: rewriting very old `xmin` values to a special FrozenXID. Freezing is essential to prevent **transaction ID wraparound** — XIDs are 32-bit and cycle; without freezing, ancient committed rows could appear to be from the "future" and become invisible. **Autovacuum** runs this automatically based on dead-tuple thresholds. VACUUM is thus the price MVCC pays for never blocking readers.

### 3.4 Write-Ahead Logging (WAL)

WAL is PostgreSQL's durability and crash-recovery mechanism. The rule (the **WAL principle**) is: **a change must be recorded in the log on stable storage before the corresponding data page is allowed to reach disk.**

**WAL records.** Every modification (insert/update/delete, index change, page split, etc.) produces a WAL record describing how to **redo** the change. Records are appended to **WAL buffers** in shared memory, then to sequential **WAL segment files** in `pg_wal/`.

**LSN (Log Sequence Number).** Each WAL record has a unique, monotonically increasing **LSN** — effectively a byte offset in the WAL stream. Crucially, **every data page stores the LSN of the last WAL record that modified it** (`pd_lsn` in the page header). Before flushing a dirty page, the buffer manager ensures WAL has been flushed up to that page's LSN. This is exactly how WAL-before-data is enforced page by page.

**Durability guarantee.** On `COMMIT`, the backend forces (`fsync`) the WAL up to the commit record's LSN to disk before reporting success. Once that `fsync` returns, the transaction is durable: even if the server crashes before the actual data pages are written, the committed change is recoverable from the log. (Group commit batches multiple backends' fsyncs; `synchronous_commit` can trade durability for latency.)

**Checkpoints.** A **checkpoint** flushes all currently-dirty buffers to disk and records a checkpoint record in WAL. It establishes a **redo point**: WAL written before it is no longer needed for crash recovery (it can be recycled/archived). Checkpoints bound recovery time but cause an I/O surge, so the checkpointer spreads the work.

**Full-page writes.** A crash mid-write can leave a page **torn** (some 8 KB sectors old, some new). To defend against this, the **first modification of a page after a checkpoint** writes the *entire page image* into WAL (a **full-page write / FPI**). During recovery this complete image replaces any torn on-disk page, after which incremental WAL records apply safely.

**Crash recovery (redo):**

```
   Crash → restart:
   1. Read last checkpoint record → find REDO point (start LSN).
   2. Replay WAL forward from REDO point:
        for each record:
          if page's pd_lsn >= record LSN  → already applied, skip
          else                            → REDO the change
          (a full-page-write record overwrites the whole page)
   3. Reach end of WAL → database is consistent.
   4. Roll-forward only; uncommitted txns are simply never made visible
      (their tuples' xmin XIDs are aborted per CLOG).
```

This is a **physical/physiological redo-only** scheme. Because MVCC keeps old versions and aborted transactions are filtered by visibility rules, PostgreSQL needs no separate undo pass during recovery.

---

## 4. Design Trade-Offs

| Decision | Advantage | Cost / Limitation |
|----------|-----------|-------------------|
| **MVCC in-heap (no undo log)** | Readers never block writers; simple, fast rollback (just mark aborted); redo-only recovery | Dead-tuple **bloat**; mandatory **VACUUM**; **write amplification** (every UPDATE writes a full new tuple) |
| **Clock-sweep buffer eviction** | Low contention vs. true LRU; approximates LRU well | Can occasionally evict a soon-needed page; tuning `shared_buffers` is workload-dependent |
| **WAL + full-page writes** | Strong durability; fast commit (sequential log fsync) vs. random data writes | FPIs inflate WAL volume right after checkpoints; checkpoint I/O spikes |
| **Lehman-Yao B-link tree** | High index concurrency without path locking | Slightly more space (high keys, right-links); split logic complexity |
| **Process-per-connection** | Strong isolation; a crashed backend can't corrupt others' memory | High per-connection overhead → connection pooling (PgBouncer) often required |
| **Cost-based planner on statistics** | Adapts plan to data shape; great on well-analyzed data | Misestimates (stale stats, correlated columns) → bad plans |

**Performance implications.** Update-heavy and high-churn workloads stress MVCC: more dead tuples, more autovacuum, more WAL. Mitigations baked into the engine—**HOT updates**, **fillfactor** tuning (leaving page space so updates stay HOT/local), and **deduplication** in B-trees—are direct engineering responses to these trade-offs. The fundamental bargain is consistent: PostgreSQL optimizes for **concurrency, correctness, and crash safety**, accepting background maintenance (VACUUM) and extra writes as the price.

---

## 5. Experiments / Observations (Illustrative)

Consider a small schema and a two-table join. *(Output below is representative/illustrative, formatted as a real `EXPLAIN ANALYZE` would appear.)*

```sql
CREATE TABLE customers (
    id      int PRIMARY KEY,
    region  text
);                              -- ~10,000 rows
CREATE TABLE orders (
    id          int PRIMARY KEY,
    customer_id int REFERENCES customers(id),
    amount      numeric,
    created_at  date
);                              -- ~1,000,000 rows
CREATE INDEX ON orders(customer_id);
ANALYZE customers;
ANALYZE orders;

EXPLAIN ANALYZE
SELECT c.region, count(*), sum(o.amount)
FROM   customers c
JOIN   orders o ON o.customer_id = c.id
WHERE  c.region = 'APAC'
GROUP  BY c.region;
```

**Representative plan:**

```
 HashAggregate  (cost=27310.45..27310.55 rows=4 width=44)
                (actual time=210.331..210.334 rows=1 loops=1)
   Group Key: c.region
   ->  Hash Join  (cost=180.50..26810.45 rows=100000 width=18)
                  (actual time=2.114..180.992 rows=98214 loops=1)
         Hash Cond: (o.customer_id = c.id)
         ->  Seq Scan on orders o  (cost=0.00..18334.00 rows=1000000 width=14)
                                   (actual time=0.008..70.4 rows=1000000 loops=1)
         ->  Hash  (cost=155.00..155.00 rows=1000 width=8)
                   (actual time=2.05..2.05 rows=1000 loops=1)
               Buckets: 2048  Batches: 1  Memory Usage: 48kB
               ->  Seq Scan on customers c (cost=0.00..155.00 rows=1000 width=8)
                                           (actual time=0.01..1.7 rows=1000 loops=1)
                     Filter: (region = 'APAC')
                     Rows Removed by Filter: 9000
 Planning Time: 0.42 ms
 Execution Time: 210.51 ms
```

**Interpretation:**

- **Why a Hash Join, not a Nested Loop?** The planner estimated ~1,000 matching customers joining against 1,000,000 orders → ~100,000 result rows. A nested loop would do ~1,000 index probes into `orders(customer_id)`, but each probe returns many rows; the cost model judged that **building a hash table on the small (filtered) `customers` side and streaming `orders` past it** is cheaper than repeated index descents. Had `region='APAC'` matched only a handful of customers, the planner would likely have flipped to a **Nested Loop + Index Scan** on `orders(customer_id)`.
- **Estimates vs. actuals.** `rows=100000 (est) → 98214 (actual)` for the join — a very good estimate, which is *why* the chosen plan is sound. Big divergences here are the classic cause of bad plans (e.g., a nested loop chosen because the planner thought one side had 5 rows when it had 50,000).
- **Role of `pg_statistic` / `ANALYZE`.** The estimate `rows=1000` for `region='APAC'` comes from the **most-common-values list and histogram** stored in `pg_statistic` (surfaced via `pg_stats`) and the relation's `reltuples`/`relpages` in `pg_class`, all populated by **`ANALYZE`** (and autovacuum's analyze). The planner multiplies total rows by the column's **selectivity** to estimate filtered cardinality. Stale or missing stats → wrong selectivity → wrong cardinality → wrong join method.
- **Cost model.** Each `cost=startup..total` is in abstract units derived from `seq_page_cost`, `random_page_cost`, `cpu_tuple_cost`, `cpu_operator_cost`, etc., combined with the cardinality estimates. The planner enumerates candidate paths and picks the lowest **total cost**; `EXPLAIN ANALYZE` then reports the **actual** time/rows so you can audit the estimates.

**Observation:** the quality of the plan is downstream of statistics quality. Running `ANALYZE` after bulk loads, and extended statistics (`CREATE STATISTICS`) for correlated columns, are the practical levers.

---

## 6. Key Learnings

- **Pages are the currency, and the buffer manager is the economy.** Everything funnels through 8 KB pages in `shared_buffers`. Pages move in via clock-sweep eviction (decrement `usage_count`, evict at 0 if unpinned) and out lazily by bgwriter/checkpointer. Hot data staying resident is what makes the system fast; pinning and the buffer hash table make access safe and O(1).
- **MVCC is "keep the old versions" not "lock everything."** Visibility is computed from `xmin`/`xmax` against a snapshot, with `t_infomask` caching commit status. Readers and writers don't block each other — the central concurrency win — but the cost is dead tuples.
- **VACUUM is not optional; it is the other half of MVCC.** It reclaims dead-tuple space, prevents bloat, and—via freezing—prevents catastrophic XID wraparound. HOT updates and fillfactor are the levers that reduce how much work VACUUM must do.
- **WAL turns durability into a sequential-write problem.** Commit = fsync the log up to the commit LSN. The page-level `pd_lsn` enforces WAL-before-data; full-page writes defeat torn pages; checkpoints bound recovery; recovery is redo-only because MVCC handles aborts. This is why a hard crash recovers cleanly.
- **The planner is only as smart as its statistics.** Join method, scan choice, and resource use all hinge on cardinality estimates from `pg_statistic`. `EXPLAIN ANALYZE` exposing estimate-vs-actual gaps is the single most useful diagnostic for query performance.

**Practical takeaways:** size `shared_buffers` for the working set; let autovacuum keep up (and tune it for churny tables); keep statistics fresh with `ANALYZE`/extended statistics; watch WAL volume and checkpoint frequency; and prefer HOT-friendly schemas (indexes only where needed, sensible fillfactor) on update-heavy tables. PostgreSQL's internals are a coherent set of trade-offs in service of one goal: correct, durable, highly concurrent data access.
