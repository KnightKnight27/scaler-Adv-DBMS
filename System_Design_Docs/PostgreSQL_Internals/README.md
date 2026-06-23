# PostgreSQL Internal Architecture

> Advanced DBMS — System Design Discussion
> Topic 2: PostgreSQL Internal Architecture

---

## Table of Contents

1. [Problem Background](#1-problem-background)
2. [Architecture Overview](#2-architecture-overview)
3. [Internal Design](#3-internal-design)
4. [Design Trade-Offs](#4-design-trade-offs)
5. [Experiments / Observations](#5-experiments--observations)
6. [Key Learnings](#6-key-learnings)
7. [References](#references)

---

## 1. Problem Background

PostgreSQL is a general-purpose, multi-user relational database descended from the
**POSTGRES** project led by Michael Stonebraker at UC Berkeley (1986). The problem it
solves: *let many concurrent clients safely read and write a large shared dataset,
with strong correctness (ACID), crash durability, and good query performance.*

Meeting all of those at once forces four hard sub-problems, which are exactly the four
internals this topic studies:

- **How do you serve disk-resident data fast?** → the **Buffer Manager**.
- **How do you find rows quickly without scanning everything?** → the **B-Tree** index.
- **How do many transactions read and write concurrently without blocking or
  corrupting each other?** → **MVCC**.
- **How do you guarantee committed data survives a crash?** → **WAL**.

Each section below explains one of these and ties the implementation to observed
behavior.

---

## 2. Architecture Overview

PostgreSQL uses a **process-per-connection** model coordinated through **shared
memory**. A `postmaster` daemon forks one backend process per client; all backends
cooperate on the same data through shared buffers, and background processes handle
durability and cleanup.

```
   Clients ──► postmaster ──forks──► backend (1 per connection)
                                          │
                 ┌──── Query path inside a backend ─────────────┐
                 │ SQL → Parse → Rewrite → Plan/Optimize → Exec │
                 └───────────────────────┬──────────────────────┘
                                         │ page requests
                  ┌──────────────────────▼───────────────────────┐
                  │            SHARED MEMORY                       │
                  │   ┌───────────────────────────────────────┐   │
                  │   │   Shared Buffers (8 KB page cache)      │  │
                  │   └───────────────────────────────────────┘   │
                  │       WAL buffers · lock table · ProcArray     │
                  └───────┬───────────────────────────┬───────────┘
                          │ flush dirty pages          │ flush WAL
                   ┌──────▼──────┐              ┌───────▼───────┐
                   │ Data files  │              │  WAL (pg_wal) │
                   │ (heap/index)│◄──checkpoint─┤  redo log     │
                   └─────────────┘              └───────────────┘

   Background procs: bgwriter · checkpointer · WAL writer · autovacuum · archiver
```

**Data flow for a read:** executor asks Buffer Manager for a page → if cached
(hit), returned from shared buffers → if not (miss), read from disk into a buffer
slot → tuples extracted, visibility checked via MVCC → rows returned.

**Data flow for a write:** modify the page in a shared buffer (mark *dirty*) →
record the change in **WAL** first → WAL is flushed on commit (durability) → the dirty
data page is written back lazily by bgwriter/checkpointer later.

---

## 3. Internal Design

### 3.1 Buffer Manager — `src/backend/storage/buffer/`

The buffer manager is the layer between the executor and disk. Disk I/O is orders of
magnitude slower than RAM, so PostgreSQL caches **8 KB pages** in a shared-memory
array of **buffer slots** (`shared_buffers`).

**Structure**
- A fixed array of buffer frames, each holding one 8 KB page.
- A **buffer descriptor** per frame: which page it holds (`BufferTag` =
  relation + fork + block number), a pin count, a usage count, and dirty/valid flags.
- A hash table maps `BufferTag → buffer slot` so a backend can find a cached page.

**Page access protocol**
1. Compute the `BufferTag`, look it up in the hash table.
2. **Hit** → pin the buffer (so it can't be evicted while in use), use it, unpin.
3. **Miss** → find a victim slot via the replacement policy; if the victim is dirty,
   write it out first; read the requested page into the slot; pin and use it.

**Replacement policy — Clock Sweep (an approximation of LRU).** Each buffer has a
`usage_count`. A "clock hand" sweeps the buffer array: if a buffer's `usage_count > 0`
it is decremented and skipped; the first buffer found with `usage_count == 0` and pin
count 0 is evicted. This is cheap (no global LRU list to lock) and resists cache
pollution from one-off large scans.

**Writing pages out**
- The **background writer** trickles dirty pages to disk so backends rarely have to
  do their own writes during a miss.
- The **checkpointer** periodically flushes *all* dirty buffers and records a
  checkpoint (a known-good restart point) so WAL replay after a crash is bounded.

> A page is only ever flushed *after* its WAL record is on disk — the **WAL rule**
> (see §3.4). This ordering is what makes crash recovery correct.

### 3.2 B-Tree — `src/backend/access/nbtree/`

PostgreSQL's default index is a **B-tree**, implemented using the **Lehman & Yao**
high-concurrency variant (with later refinements). It supports `=`, `<`, `<=`, `>`,
`>=`, `BETWEEN`, `IN`, and ordering / `ORDER BY`.

**Structure & page layout**
- A balanced tree of 8 KB pages: one **root**, internal **branch** pages, and
  **leaf** pages, all linked.
- **Leaf pages are doubly linked** left↔right so the engine can range-scan forward or
  backward without revisiting parents.
- Each index page is a slotted page: a special area holds B-tree metadata (`BTPageOpaque`
  with left/right sibling pointers and a **high key**); item pointers index the
  entries; entries hold the indexed key + a **CTID** (heap tuple location).
- A meta page (block 0) points to the root.

**Search path:** start at the root, binary-search the keys to choose a child, descend
branch by branch to a leaf, then binary-search the leaf for the key. Cost is
O(log n) page accesses, each ideally a buffer hit.

**Insert & page splits**
- Find the target leaf, insert the entry in key order.
- If the leaf is **full**, it **splits**: roughly half the entries move to a new
  right-sibling page, sibling links are rewired, and a **separator key** is pushed up
  into the parent (which may itself split, possibly propagating to a new root —
  this is how the tree grows in height).
- The **high key** on each page (the L&Y design) lets a concurrent reader that
  arrives during a split detect "the key I want moved right" and follow the right-link,
  so searches stay correct **without locking the whole tree**.

### 3.3 MVCC — Multi-Version Concurrency Control

PostgreSQL gives each transaction a consistent **snapshot** by keeping **multiple
physical versions** of each row, rather than locking rows for reads.

**Heap tuple versioning**
- Every heap tuple carries hidden system columns, chiefly **`xmin`** (the transaction
  id that *created* this version) and **`xmax`** (the transaction id that *deleted or
  superseded* it; 0 if still live).
- **INSERT:** new tuple with `xmin = current xid`, `xmax = 0`.
- **DELETE:** set the tuple's `xmax = current xid` (the data stays physically present).
- **UPDATE:** = delete + insert. The old version gets `xmax` set; a **new tuple** is
  written (with a fresh `xmin`). The old version's `ctid` is used to chain to the new
  one. Updates are therefore **not in place**.

**Snapshots & visibility rules**
A snapshot records which transactions had committed at its start. A tuple version is
**visible** to a transaction roughly when:
- its `xmin` is **committed and ≤ the snapshot** (the creator's effect is visible), **and**
- its `xmax` is **0, aborted, or not-yet-visible** (it has not been deleted as far as
  this snapshot is concerned).

This yields **snapshot isolation**: readers see a stable view; **readers never block
writers and writers never block readers.** Concurrent writes to the *same* row are
serialized via row locks; writes to different rows proceed in parallel.

**Why VACUUM is necessary**
Dead tuples (deleted versions, and old versions left by updates) are never visible to
new transactions but still occupy pages. **`VACUUM`**:
- reclaims space from dead tuples for reuse (and `VACUUM FULL` compacts the file),
- updates the **visibility map** (enabling index-only scans and skipping all-visible
  pages),
- **freezes** very old `xmin` values to prevent **transaction-ID wraparound** (XIDs
  are 32-bit and cycle; without freezing, old rows could appear to be from the
  "future" and vanish). **Autovacuum** runs this automatically.

### 3.4 WAL — Write-Ahead Logging

WAL is PostgreSQL's durability and crash-recovery mechanism.

**The WAL rule:** a change to a data page must be recorded in the WAL **and the WAL
flushed to disk** *before* the dirty data page itself is written. The log is the
authoritative record of committed work.

**WAL records**
- Each modification (insert/update/delete/index change) produces a WAL record
  describing how to **redo** the change, tagged with an **LSN** (Log Sequence Number,
  a byte position in the WAL stream). Each page stores the LSN of the last change
  applied to it.
- On **COMMIT**, the transaction's WAL up to its commit record is flushed (`fsync`).
  Once that returns, the transaction is durable — even though its data pages may still
  be only in shared buffers.

**Checkpointing**
- A **checkpoint** flushes all dirty buffers and writes a checkpoint record marking a
  position in the WAL from which recovery can start. This **bounds recovery time** and
  lets old WAL segments be recycled.

**Crash recovery (REDO)**
- On restart after a crash, PostgreSQL finds the last checkpoint and **replays
  (redoes)** WAL records forward, re-applying any change whose LSN is newer than the
  page on disk. After redo, the database is consistent; uncommitted transactions are
  effectively discarded (their effects are not committed and MVCC ignores them).

**Bonus:** the same WAL stream powers **streaming replication** and **point-in-time
recovery (PITR)** via archiving.

### 3.5 Query Planning & Statistics

PostgreSQL is a **cost-based optimizer**. After parsing and rewriting, the **planner**
enumerates candidate plans (scan methods, join orders, join algorithms) and estimates
each one's **cost** using statistics, then executes the cheapest.

- **`ANALYZE`** samples tables and stores statistics in the **`pg_statistic`** catalog
  (human-readable via the `pg_stats` view): per-column null fraction, number of
  distinct values, most-common-values list, and a histogram of the distribution.
- The planner uses these to estimate **selectivity** (how many rows a predicate
  returns) and therefore the cost of seq scans vs index scans, and of nested-loop vs
  hash vs merge joins.
- **Garbage in, garbage out:** stale statistics → wrong row estimates → wrong plan
  (e.g., a nested loop chosen where a hash join was needed). This is why `ANALYZE`
  (and autovacuum's analyze) matters for performance.

---

## 4. Design Trade-Offs

**Buffer manager**
- *Clock sweep* is cheaper than exact LRU (no globally locked LRU list) and resists
  pollution from large scans, at the cost of being only an approximation of LRU.
- A dedicated `shared_buffers` cache plus reliance on the OS page cache can cause
  **double buffering**; sizing `shared_buffers` is a classic tuning trade-off.

**B-tree (L&Y high-concurrency design)**
- High keys + right-links allow correct lock-light concurrent reads during splits —
  great concurrency, but a more intricate implementation than a textbook B-tree.
- Index entries point at the heap (CTID), so after an update the index may need a new
  entry; **HOT (Heap-Only Tuple)** updates mitigate this when no indexed column changed.

**MVCC**
- *Advantage:* superb read/write concurrency; reads don't take row locks; consistent
  snapshots come for free.
- *Cost:* **table/index bloat** from dead tuples, a mandatory **VACUUM** process, and
  XID-wraparound bookkeeping. Update-heavy workloads pay the most. (Contrast: InnoDB
  updates in place and keeps old versions in a separate **undo log** instead.)

**WAL**
- *Advantage:* durability with mostly **sequential** log writes (fast) and lazy random
  data-page writes; enables replication and PITR.
- *Cost:* every change is written twice (WAL + eventually the page) — **write
  amplification**; `full_page_writes` adds more WAL after each checkpoint to guard
  against torn pages.

**Cost-based planner**
- *Advantage:* adapts plans to data distribution and size.
- *Cost:* depends entirely on statistics quality; bad estimates → bad plans, and
  planning itself costs CPU for very complex queries.

---

## 5. Experiments / Observations

### 5.1 `EXPLAIN ANALYZE` on a multi-table join

```sql
ANALYZE;   -- make sure pg_statistic is fresh

EXPLAIN (ANALYZE, BUFFERS)
SELECT s.name, c.title
FROM students s
JOIN enrollments e ON e.student_id = s.id
JOIN courses     c ON c.id = e.course_id
WHERE s.year = 2026;
```
Representative output:
```
Hash Join  (cost=18.40..52.10 rows=210 width=64) (actual time=0.12..1.83 rows=198 loops=1)
  Hash Cond: (e.course_id = c.id)
  ->  Hash Join  (cost=... rows=210 ...) (actual ... rows=198 ...)
        Hash Cond: (e.student_id = s.id)
        ->  Seq Scan on enrollments e   (actual rows=940 ...)
        ->  Hash
              ->  Seq Scan on students s  (actual rows=63 ...)
                    Filter: (year = 2026)   Rows Removed by Filter: 437
  ->  Hash
        ->  Seq Scan on courses c  (actual rows=25 ...)
  Buffers: shared hit=42
Planning Time: 0.41 ms
Execution Time: 2.06 ms
```
**What to analyze**
- **Chosen plan:** two **Hash Joins** — the planner judged hashing cheaper than nested
  loops because multiple rows match on each side.
- **Estimates vs actuals:** `rows=210` (estimate) vs `rows=198` (actual) — close, so
  statistics are good. A large divergence here would signal stale stats and risk a
  bad plan.
- **Statistics source:** those estimates come from `pg_statistic` (populated by
  `ANALYZE`); inspect them with `SELECT * FROM pg_stats WHERE tablename='students';`.
- **`BUFFERS`:** `shared hit=42` shows the buffer manager served 42 pages from cache
  (hits) — connecting §3.1 to observed behavior.

**Force a different plan to see cost-based decisions:**
```sql
SET enable_hashjoin = off;   -- planner now picks merge/nested-loop instead
EXPLAIN ANALYZE SELECT ...;  -- observe cost & time change
RESET enable_hashjoin;
```

### 5.2 MVCC and VACUUM in action

```sql
SELECT xmin, xmax, ctid, * FROM accounts WHERE id = 1;  -- note the ctid
UPDATE accounts SET balance = balance + 10 WHERE id = 1;
SELECT xmin, xmax, ctid FROM accounts WHERE id = 1;      -- ctid CHANGED (new version)

SELECT relname, n_live_tup, n_dead_tup FROM pg_stat_user_tables
 WHERE relname = 'accounts';                              -- a dead tuple appeared
VACUUM (VERBOSE) accounts;                                -- dead tuple reclaimed
```
**Observation:** the `UPDATE` left the old version in place (its `ctid` moved to a new
tuple), proving updates are *not* in place — and `VACUUM` is what later reclaims the
dead version. This is §3.3 made concrete.

### 5.3 Buffer cache hit vs miss

```sql
EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM students WHERE id = 42;  -- first run: read=1 (miss)
EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM students WHERE id = 42;  -- again: hit=1 (cached)
```
**Observation:** the first execution reports `shared read` (page fetched from disk);
the repeat reports `shared hit` (served from `shared_buffers`) — the clock-sweep cache
keeping the hot page resident.

---

## 6. Key Learnings

1. **The four internals interlock.** The buffer manager only ever flushes a page after
   its WAL is durable (the WAL rule); MVCC decides which tuple in a buffered page a
   transaction may see; the planner's choices determine which pages the buffer manager
   is even asked for. They are one system, not four features.

2. **MVCC's elegance has a janitor.** Non-blocking snapshot reads are bought with dead
   tuples, and `VACUUM`/autovacuum is the price — including the non-obvious need to
   *freeze* XIDs to avoid 32-bit wraparound. Neglecting vacuum is the classic
   PostgreSQL production failure.

3. **WAL trades random writes for sequential ones.** Durability comes from flushing a
   sequential log on commit while data pages are written lazily and randomly later —
   fast commits plus bounded recovery via checkpoints, at the cost of write
   amplification.

4. **B-tree concurrency is a design, not an accident.** High keys and right-links
   (Lehman & Yao) let readers stay correct during splits without locking the tree —
   the reason a busy index doesn't serialize all its readers.

5. **Plans are only as good as statistics.** The optimizer is cost-based; `pg_statistic`
   (via `ANALYZE`) drives every estimate. The most useful diagnostic skill is reading
   `EXPLAIN ANALYZE` and comparing estimated vs actual rows.

---

## References

- PostgreSQL Documentation — Internals: https://www.postgresql.org/docs/current/internals.html
- *The Internals of PostgreSQL* — Hironobu Suzuki: https://www.interdb.jp/pg/
- Buffer Manager source: `src/backend/storage/buffer/` (`bufmgr.c`, `freelist.c`)
- B-Tree source & README: `src/backend/access/nbtree/` (`README`, `nbtinsert.c`)
- Lehman, Yao — "Efficient Locking for Concurrent Operations on B-Trees" (1981)
- PostgreSQL Docs — MVCC: https://www.postgresql.org/docs/current/mvcc.html
- PostgreSQL Docs — WAL & Reliability: https://www.postgresql.org/docs/current/wal-intro.html
- PostgreSQL Docs — Statistics Used by the Planner / `pg_statistic`

---

*Submitted for the Advanced DBMS System Design Discussion. All analysis and prose are
original; cited sources were used for fact-checking architectural details.*
