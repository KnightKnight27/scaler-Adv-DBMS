# PostgreSQL Internal Architecture

**Author:** Shubham Shah · **Roll No:** 24BCS10316 · **Course:** Advanced DBMS, Scaler School of Technology
**Topic 2: PostgreSQL Internals** Buffer Manager · B-Tree · MVCC · WAL

---

## 1. Problem Background

PostgreSQL descends from the **POSTGRES** project led by Michael Stonebraker at UC
Berkeley (1986), whose goal was to fix the limitations of the earlier Ingres
relational system, chiefly the inability to support complex data types, rules,
and *no-overwrite storage*. The "no-overwrite" idea (never destroying an old row
on update, but writing a new version beside it) is the seed that grew into
PostgreSQL's modern **MVCC** model.

The problems PostgreSQL is engineered to solve:

- **Many concurrent users must not block each other.** A long analytics query
  should not freeze writers; a writer should not freeze readers. This demands a
  concurrency model richer than a single global lock.
- **Committed data must survive a crash.** A power loss one millisecond after
  `COMMIT` returns must not lose the transaction. This demands **durability**
  independent of when dirty pages reach disk.
- **Correctness under partial writes.** A row, an index entry, and the
  free-space map must stay mutually consistent even if the OS dies mid-update.

PostgreSQL answers these with four cooperating subsystems studied below: the
**Buffer Manager** (who owns pages in RAM), the **B-Tree** (how indexed lookups
stay logarithmic), **MVCC** (how readers and writers avoid blocking), and **WAL**
(how durability and crash recovery are guaranteed).

---

## 2. Architecture Overview

PostgreSQL is a **process-per-connection** client–server system. The `postmaster`
parent process listens for connections; each client gets a dedicated `backend`
process. Backends share memory (most importantly the **shared buffer pool**)
and cooperate through background workers.

```
   clients (psql, apps)
        │  TCP / Unix socket
        ▼
  ┌───────────┐  forks one backend per connection
  │ postmaster│──────────────┬──────────────┬───────────────┐
  └───────────┘              ▼              ▼               ▼
                        ┌─────────┐    ┌─────────┐    ┌─────────┐
                        │ backend │    │ backend │    │ backend │
                        └────┬────┘    └────┬────┘    └────┬────┘
                             │   parse→plan→execute        │
                             ▼                             ▼
   ┌─────────────────────── SHARED MEMORY ───────────────────────┐
   │   ┌─────────────────┐      ┌──────────────────────────┐     │
   │   │  Shared Buffers │      │  WAL buffers             │     │
   │   │  (page cache)   │      │  (redo log, in RAM)      │     │
   │   └────────┬────────┘      └────────────┬─────────────┘     │
   └────────────┼────────────────────────────┼───────────────────┘
                │ dirty pages                 │ WAL records
                ▼ (later, by bgwriter/        ▼ (at commit, by walwriter)
         ┌──────────────┐              ┌──────────────┐
         │ data files   │              │  pg_wal/     │
         │ base/*/*     │              │  (WAL segs)  │
         └──────────────┘              └──────────────┘
  background workers: bgwriter · walwriter · checkpointer · autovacuum
```

**Data flow for a query.** SQL text → **parser** (→ parse tree) → **rewriter**
(applies rules/views) → **planner/optimizer** (cost-based plan using statistics
from `pg_statistic`) → **executor** (pulls tuples through the plan tree, reading
pages via the buffer manager). Any modification is first recorded in **WAL**,
then applied to the shared buffer page; the data file on disk is updated lazily.

---

## 3. Internal Design

### 3.1 Buffer Manager: `src/backend/storage/buffer/`

All table and index data lives in **8 KB pages** (blocks). The buffer manager is
the layer that caches these pages in a shared-memory array of **buffer frames**
(`shared_buffers`, default 128 MB). A backend never reads a data file directly.
It asks the buffer manager for a page by `(relation, fork, block#)`.

**Lookup.** A shared hash table maps a `BufferTag` (relation + block number) to a
frame index. Hit → return the frame; miss → choose a victim frame, evict it
(writing it back first if dirty), read the requested block into it.

**Pinning & reference counts.** Before using a page a backend **pins** it
(`PinBuffer`), incrementing a pin count so the replacer cannot steal it mid-use.
Each buffer also has a `usage_count`.

**Replacement: Clock Sweep.** PostgreSQL uses a **clock (second-chance)**
algorithm rather than strict LRU, because true LRU needs a global lock on every
access and does not scale to many cores. A "clock hand" cycles through frames:

```
   for each frame the hand passes:
       if pinned            → skip (in use)
       else if usage_count>0 → usage_count--, give a second chance, move on
       else                  → this is the victim; evict it
```

Each access bumps `usage_count` (capped, e.g. at 5); the sweep decrements it.
Frequently-touched pages keep getting a second chance and survive; cold pages
decay to 0 and are evicted. This approximates LRU with only a local atomic, so it
scales across dozens of backends.

**Writing back.** A dirty victim must be flushed before reuse, but PostgreSQL
tries to keep clean victims available so backends rarely wait:
- **bgwriter** trickles dirty pages to the OS ahead of need.
- **checkpointer** periodically flushes *all* dirty buffers so recovery has a
  bounded starting point (see WAL).
Crucially, **WAL for a change is flushed before the data page is**. This is the
**WAL-before-data (write-ahead)** rule.

### 3.2 B-Tree Index (`nbtree`)

PostgreSQL's default index is a **Lehman & Yao B⁺-tree** variant. Keys live in
leaf pages; internal pages hold only separator keys + child pointers; leaves form
a doubly-linked chain enabling efficient range scans and ordered retrieval.

```
                       [ • 40 • 80 • ]            ← root (internal)
                       /     |       \
              [•10•25•]   [•55•70•]   [•90• ]      ← internal
              /  |   \      ...           ...
        leaf→leaf→leaf→leaf→leaf→ ...              ← leaves, linked L→R
        (each leaf: sorted keys → heap TIDs)
```

- **Search path.** Descend root→leaf following separators; cost = tree height
  (typically 3–4 levels for millions of rows), so every lookup is O(log n) with a
  small constant.
- **Index entries point to the heap.** A leaf entry is `(key, TID)` where the
  **TID = (block#, item#)** locates the actual row version in the table heap. The
  index does **not** store the row. PostgreSQL indexes are *secondary* even for
  the primary key (no clustered index; contrast InnoDB).
- **Insert & page splits.** Insert finds the target leaf; if full, the leaf
  **splits** into two and the split key is **copied up** into the parent
  (internal splits *move* the key up). The Lehman-Yao design adds a **high key**
  and **right-link** per page so a concurrent reader that arrives during a split
  can follow the right-link instead of taking heavy locks. This is what lets
  index reads proceed with minimal blocking.
- **Visibility & the heap.** Because indexes don't carry MVCC visibility info, an
  index match must usually be re-checked against the heap tuple's `xmin/xmax`.
  PostgreSQL optimizes this with the **visibility map** (an *index-only scan* can
  skip the heap fetch when the page is known all-visible).

### 3.3 MVCC: Multi-Version Concurrency Control

PostgreSQL's defining trait: **readers never block writers and writers never
block readers.** It achieves this by keeping **multiple physical versions** of a
row and showing each transaction the version consistent with its **snapshot**.

Every heap tuple carries hidden system columns:

| Field   | Meaning                                                        |
|---------|---------------------------------------------------------------|
| `xmin`  | XID of the transaction that **inserted** (created) this version |
| `xmax`  | XID that **deleted/updated** it (0 = still live)              |
| `ctid`  | physical location; on UPDATE, old tuple's ctid → points to new version |

**Update = insert + mark-old-dead (no overwrite).** An `UPDATE` does **not**
modify the row in place. It writes a **new tuple version** and sets the old
version's `xmax` to the updating XID. Both versions coexist on the heap.

```
   UPDATE accounts SET bal=900 WHERE id=1;     (txn 105)

   before:  (id=1, bal=1000)  xmin=100  xmax=0      ← visible to old snapshots
   after :  (id=1, bal=1000)  xmin=100  xmax=105    ← old version, now "dead" to new
            (id=1, bal= 900)  xmin=105  xmax=0      ← new version
```

**Visibility rule (simplified).** A tuple is visible to my snapshot if its `xmin`
committed *before my snapshot* **and** its `xmax` is 0 / not-yet-committed /
aborted / after my snapshot. A snapshot records the set of in-progress XIDs at its
start, so a transaction sees a frozen, consistent view. This is **snapshot
isolation** (PostgreSQL's `READ COMMITTED` takes a fresh snapshot per statement;
`REPEATABLE READ` keeps one snapshot for the whole transaction).

**Why VACUUM is necessary.** Dead tuples (old versions whose `xmax` is committed
and invisible to *all* live snapshots) accumulate and waste space, "**bloat**".
`VACUUM` reclaims them, updates the **free space map** and **visibility map**, and
prevents **transaction-ID wraparound** by *freezing* very old `xmin` values (XIDs
are 32-bit and cyclic, so old rows must be marked permanently visible before the
counter laps). **autovacuum** runs this automatically. This is the price
PostgreSQL pays for its no-overwrite MVCC: cheap, non-blocking versions in
exchange for background cleanup.

### 3.4 WAL: Write-Ahead Logging

WAL is the durability and recovery backbone. **Rule: a change must be recorded in
the log on disk *before* the corresponding data page is written.** Because the WAL
is a sequential append, fsync-ing it is far cheaper than randomly flushing every
touched data page at commit.

- **WAL record.** Each modification (heap insert/update/delete, index change, etc.)
  emits a record with an **LSN (Log Sequence Number)**, a monotonic byte offset
  into the log stream. The page header stores the LSN of the last change to that
  page.
- **Durability at commit.** On `COMMIT`, the backend forces WAL up to its commit
  record to disk (fsync). Once that returns, the transaction is durable *even
  though its data pages may still be dirty in shared buffers*.
- **Checkpointing.** Periodically the **checkpointer** flushes all dirty buffers
  and writes a **checkpoint record**. Recovery only needs to replay WAL from the
  last checkpoint, bounding recovery time. More frequent checkpoints = faster
  recovery but more I/O; a classic tunable trade-off (`checkpoint_timeout`,
  `max_wal_size`).
- **Crash recovery (REDO).** On restart PostgreSQL finds the last checkpoint and
  **replays** WAL forward, re-applying each record to any page whose stored LSN is
  older than the record's LSN (idempotent). Uncommitted transactions leave no
  trace because their effects are simply never marked visible (MVCC). There is no
  separate UNDO log as in InnoDB; aborted/old versions are cleaned by VACUUM
  instead.
- **Beyond recovery.** The same WAL stream powers **streaming replication**
  (replicas replay the primary's WAL) and **point-in-time recovery (PITR)**.

---

## 4. Design Trade-Offs

| Decision | Advantage | Cost / Limitation |
|----------|-----------|-------------------|
| **No-overwrite MVCC** (new version per update) | Readers never block writers; trivial snapshot isolation; cheap rollback (just don't show the new version) | Table **bloat**; needs VACUUM; updates are write-amplifying; every index must be updated to point at the new tuple |
| **Heap + secondary indexes** (no clustered index) | All indexes are uniform; cheap secondary indexes (store TID only) | Index lookups need a second hop to the heap; no automatic physical clustering by PK |
| **Clock-sweep buffer replacement** | Scales to many cores (local atomics, no global LRU lock) | Only *approximates* LRU; a large sequential scan can still pollute the cache (mitigated by ring buffers) |
| **WAL (redo-only) + VACUUM** | Sequential, fast commits; powers replication & PITR; no in-line UNDO cost | Background VACUUM load; long-running transactions hold back cleanup and inflate bloat |
| **Process-per-connection** | Strong isolation/robustness (one crash ≠ all crash); simple memory model | High per-connection memory; thousands of connections need a pooler (PgBouncer) |
| **32-bit XIDs** | Compact tuple headers | Wraparound risk → mandatory freezing by VACUUM |

**Key contrast with InnoDB:** PostgreSQL stores old versions *in the table itself*
and cleans later (VACUUM); InnoDB stores old versions in a separate *undo log* and
updates rows in place. PostgreSQL trades bloat for non-blocking simplicity; InnoDB
trades undo-log management for compact, clustered tables. (Expanded in the InnoDB
topic.)

---

## 5. Experiments / Observations

**Recommended exercise: `EXPLAIN ANALYZE` on a multi-table join.**

```sql
EXPLAIN (ANALYZE, BUFFERS)
SELECT c.name, COUNT(o.id)
FROM customers c
JOIN orders o ON o.customer_id = c.id
WHERE c.country = 'IN'
GROUP BY c.name;
```

Illustrative output and how to read it:

```
HashAggregate  (cost=2456.10..2461.40 rows=530 width=40)
              (actual time=18.3..18.9 rows=512 loops=1)
  Group Key: c.name
  ->  Hash Join  (cost=412.0..2310.5 rows=4120 width=36)
                (actual time=4.1..15.2 rows=4096 loops=1)
        Hash Cond: (o.customer_id = c.id)
        ->  Seq Scan on orders o      (actual rows=20000 loops=1)
        ->  Hash  (actual rows=512 loops=1)   Buffers: shared hit=33
              ->  Index Scan using customers_country_idx on customers c
                    Index Cond: (country = 'IN')   (actual rows=512)
  Planning Time: 0.42 ms
  Execution Time: 19.6 ms
```

What to analyze and connect back to internals:

- **Chosen plan.** The planner picked a **Hash Join** (build a hash on the smaller
  filtered `customers` side, probe with `orders`) over nested-loop/merge, a
  *cost-based* choice, not a fixed rule.
- **Estimate vs actual.** `rows=4120` (estimate) vs `rows=4096` (actual). Large
  divergence here is the #1 cause of bad plans and signals **stale statistics** →
  run `ANALYZE`.
- **Statistics & `pg_statistic`.** The planner's row estimates come from sampled
  stats in `pg_statistic` (readable via the `pg_stats` view): per-column
  `null_frac`, `n_distinct`, most-common-values, and a histogram. The `country='IN'`
  selectivity estimate is derived from the MCV list / histogram for that column.
- **`BUFFERS`.** `shared hit=33` shows pages served from the **buffer manager**
  cache (vs `read=` for misses), a direct window into §3.1.

Other quick observations to try:

- `SHOW shared_buffers;` and `SHOW block_size;` → confirm 8 KB pages.
- Repeatedly `UPDATE` one row, then `SELECT pg_relation_size('t')` before/after
  `VACUUM` → watch **bloat** appear and be reclaimed (§3.3).
- Inspect `xmin`/`xmax`: `SELECT xmin, xmax, ctid, * FROM t;` → see version
  bookkeeping directly.

---

## 6. Key Learnings

- **One idea ties it together: never overwrite, never block.** MVCC's
  no-overwrite heap is *why* readers and writers don't block, and *why* VACUUM
  exists. Almost every PostgreSQL quirk traces back to this single decision.
- **The buffer manager is the universe.** Every read and write passes through
  shared buffers; clock-sweep keeps it scalable; WAL-before-data keeps it safe.
- **WAL decouples durability from data placement.** Commit speed depends on a
  sequential log fsync, not on flushing scattered data pages, and the same log
  unlocks replication and PITR almost for free.
- **The planner is only as good as its statistics.** `EXPLAIN ANALYZE` exposes
  the gap between estimate and reality; that gap (and `pg_statistic`) is where
  query performance is won or lost.
- **Surprising takeaway:** PostgreSQL has **no UNDO log**: rollback is "free"
  (just don't show the new version), but the bill arrives later as VACUUM. It is a
  textbook case of an engineering trade-off moved in *time* rather than removed.
