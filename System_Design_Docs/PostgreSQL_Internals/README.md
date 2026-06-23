# PostgreSQL Internal Architecture

**Advanced DBMS — System Design (Topic 2) | Archisman Midya (Roll 10027)**

A study of how PostgreSQL is built internally: its process model, its buffer
manager and clock-sweep replacement, the nbtree B-tree, MVCC, and the
write-ahead log with recovery and checkpointing — followed by the trade-offs
these designs imply and a worked `EXPLAIN ANALYZE` example.

---

## 1. Problem Background

A relational DBMS must turn a declarative SQL query into correct, durable,
concurrent access to data far larger than RAM, while many clients read and write
at once. That forces a small set of hard sub-problems:

- **Storage & caching:** data lives in fixed-size blocks on disk but must be
  operated on in memory; the system needs a cache and a replacement policy that
  behaves well under concurrency.
- **Indexing:** point and range lookups must be sub-linear and stay balanced
  under concurrent inserts/deletes.
- **Concurrency:** readers and writers must not corrupt each other or block
  excessively, yet results must be consistent (isolation).
- **Durability & recovery:** committed work must survive crashes; partial work
  must not.

PostgreSQL is a mature, open-source object-relational DBMS (descended from
Berkeley POSTGRES) whose answers to these problems — a process-per-connection
server, a clock-sweep buffer manager, Lehman-Yao B-link trees, multi-version
concurrency control, and ARIES-style WAL — are representative of production
relational systems and are the subject of this document. The general framing of
these components follows Hellerstein, Stonebraker & Hamilton's *Architecture of a
Database System* [1]; storage/index/recovery details follow Petrov's *Database
Internals* [2], the ARIES paper [3], and the PostgreSQL documentation and source
[4].

## 2. Architecture Overview

PostgreSQL uses a **client–server, process-per-connection** model. A supervisor
process (the *postmaster*) listens for connections and forks a dedicated
*backend* process for each client. Backends share data through a large region of
shared memory (chiefly the **shared buffer pool**) and coordinate via locks and
the WAL. Auxiliary background processes handle checkpointing, background writing,
autovacuum, and WAL archiving.

```
   Client apps (psql, JDBC, libpq)
            │  (TCP / Unix socket, FE/BE protocol)
            ▼
     ┌──────────────┐   fork()    ┌─────────────────────────────┐
     │  postmaster   │───────────►│  backend process (per client)│
     │  (listener)   │            │  parser → rewriter → planner  │
     └──────────────┘            │  → executor                   │
                                  └──────────────┬───────────────┘
                                                 │ read/write pages
                       ┌─────────────────────────▼─────────────────────┐
                       │            SHARED MEMORY                        │
                       │  shared buffer pool │ WAL buffers │ lock tables │
                       └─────────┬───────────────────┬──────────────────┘
                                 │ flush dirty pages  │ flush WAL records
                                 ▼                    ▼
              ┌───────────────────────┐     ┌────────────────────┐
              │ data files (heap, idx) │     │  WAL segment files  │
              │ base/<db>/<relfilenode>│     │  pg_wal/*           │
              └───────────────────────┘     └────────────────────┘
        Background procs: checkpointer, bgwriter, autovacuum, WAL writer, archiver
```

A query's life inside a backend: **parse** (SQL → parse tree) → **analyze/rewrite**
(apply rules/views) → **plan/optimize** (cost-based search for a plan tree) →
**execute** (a tree of iterator nodes, the Volcano model) that reads/writes tuples
through the buffer manager [1].

## 3. Internal Design

### 3.1 Buffer Manager and clock-sweep replacement

All heap and index pages are 8 KB blocks. To read a page a backend asks the
buffer manager for it: a hash table maps `(relation, block#)` to a buffer frame.
On a hit the frame is **pinned** (its `refcount` incremented) so it cannot be
evicted while in use; on a miss the manager selects a victim frame, writes it
back if dirty, and reads the requested block into it.

Replacement is **clock-sweep**, an approximation of LRU that is cheap under
concurrency (true LRU would require updating a global list on every access). Each
buffer has a small `usage_count`. A circular "clock hand" sweeps the buffers:

```
   for each buffer the hand passes:
       if pinned (refcount > 0):      skip
       elif usage_count > 0:          usage_count -= 1 ; advance   (second chance)
       else:                          choose as victim
   (a buffer's usage_count is bumped, up to a cap, each time it is pinned)
```

Hot pages keep their `usage_count` above zero and survive sweeps; cold pages
decay to zero and are evicted. The background *bgwriter* proactively cleans dirty
buffers so backends rarely have to write during eviction [2][4].

### 3.2 nbtree — the B-tree index

PostgreSQL's default index (`nbtree`) is a **B+-tree** based on the Lehman & Yao
**B-link tree**, which adds a right-link and a "high key" to every page so that
concurrent searches can proceed correctly during splits with minimal locking [2].

Page layout (every index/heap page shares this structure):

```
 ┌────────┬───────────────┬───────────────┬───────────────┐
 │ Page   │ line pointers │  free space   │   tuples       │
 │ header │ (ItemId[])    │               │ (grow upward)  │
 └────────┴───────────────┴───────────────┴───────────────┘
            ↑ grow downward                ↑
   Each ItemId points to a tuple; a leaf index tuple holds the key + a TID
   (heap tuple id = block#, offset). Internal pages hold separator keys + child
   pointers; the rightmost "high key" bounds the page's key range.
```

- **Search** descends from the root following separator keys to a leaf; the
  right-links let a search that arrives mid-split follow `next` to find a key that
  just moved.
- **Insert** places the entry in the correct leaf; if the page is full it
  **splits** into two, inserts a separator (the new right page's low key) into the
  parent, and propagates splits up — a root split adds a level. Deletes mark
  entries dead; empty pages are recycled by VACUUM.

### 3.3 MVCC — multi-version concurrency control

PostgreSQL gives readers a consistent view without blocking writers by keeping
**multiple versions of each row**. Every heap tuple carries system columns,
notably **`xmin`** (the transaction id that created it) and **`xmax`** (the
transaction id that deleted/updated it, or 0):

```
 INSERT row → (xmin = T_create, xmax = 0)
 UPDATE row → mark old version xmax = T_update ; INSERT new version xmin = T_update
 DELETE row → mark version     xmax = T_delete
```

An UPDATE is therefore a delete + insert: the old version stays until no
snapshot can still see it. A **snapshot** captures which transactions had
committed (and which were in progress) at its start. A tuple is **visible** to a
snapshot roughly when its `xmin` is committed and visible, and its `xmax` is 0 or
belongs to a transaction not visible to the snapshot:

```
 visible(tuple, snap) ≈ committed_and_visible(xmin, snap)
                        AND (xmax == 0 OR NOT committed_and_visible(xmax, snap))
```

`READ COMMITTED` takes a fresh snapshot per statement; `REPEATABLE READ` /
`SERIALIZABLE` take one snapshot per transaction. The Commit Log (`pg_xact`/clog)
records each transaction's commit/abort status; a hint-bit cache avoids repeated
clog lookups [2][4]. Because dead versions accumulate, they must be reclaimed (see
VACUUM, §4).

### 3.4 WAL — logging, recovery, and checkpoints

Durability uses **write-ahead logging** following ARIES principles [3]: a change
is described by a WAL record that is flushed to durable storage **before** the
modified data page is written back. Each WAL record has a monotonically
increasing **LSN** (log sequence number); each page stores the LSN of the last
change applied to it (`pd_lsn`).

- **WAL rule:** to evict/flush a dirty page, first ensure all WAL up to that
  page's `pd_lsn` is on disk. At **commit**, the transaction's commit record is
  flushed (`fsync`) — the durability point.
- **Checkpoint:** periodically the checkpointer flushes all dirty buffers and
  writes a checkpoint record recording a "redo point". Recovery can then start
  from the last checkpoint rather than the beginning of the log.
- **Crash recovery (REDO + UNDO conceptually):** on restart PostgreSQL replays
  WAL from the redo point forward, re-applying any change whose `record.LSN >
  page.pd_lsn` (idempotent redo). Because PostgreSQL keeps uncommitted versions
  in the heap and resolves them via MVCC/clog (the aborted xids simply never
  become visible), it does not need classical per-record UNDO at restart — aborted
  work is later reclaimed by VACUUM.

```
 write path:   change tuple in buffer ──► append WAL record (LSN) ──► (later)
               flush page only after its LSN is durable
 commit:       append COMMIT record ──► fsync WAL  (now durable)
 recovery:     find last checkpoint → redo forward (LSN > pd_lsn) → reach
               consistent state; uncommitted xids resolved by clog/MVCC
```

## 4. Design Trade-Offs

- **Heap storage + VACUUM.** Append-friendly heap tuples and in-place version
  marking make writes cheap, but dead versions (from updates/deletes) accumulate
  as **bloat**. `VACUUM` (and autovacuum) reclaim space and freeze old xids to
  prevent transaction-id wraparound. Trade-off: no in-line garbage collection
  cost, paid later by background maintenance and tunable bloat.
- **MVCC vs. locking.** Readers never block writers and vice versa, which is
  excellent for read-heavy mixed workloads — at the cost of storing multiple
  versions (space) and needing visibility checks + VACUUM. Index entries point to
  every version, so updates can also cause index bloat (mitigated by HOT updates
  when no indexed column changes).
- **Process-per-connection.** Strong isolation and crash containment (a crashed
  backend doesn't corrupt others) and simple use of OS scheduling — but each
  connection is a heavyweight process, so thousands of connections need external
  pooling (e.g. PgBouncer). This is a classic robustness-vs-footprint choice [1].
- **Clock-sweep vs. exact LRU.** Cheaper and more concurrency-friendly than
  maintaining a global LRU order, at the cost of being only an approximation of
  recency.
- **Single-node scalability.** Vertical scaling and read replicas (via WAL
  shipping / streaming replication) are first-class; horizontal write scaling
  (sharding) is not built into core PostgreSQL and is handled by extensions/forks.
  WAL is also the foundation of replication, not just recovery.

## 5. Experiments / Observations

> **Note:** No local PostgreSQL server was installable in this environment
> (`psql`/`postgres` unavailable), so this section is a **clearly-labeled worked
> example** of `EXPLAIN ANALYZE` on a multi-table join, interpreting the plan,
> the estimate-vs-actual rows, and the role of `pg_statistic`, rather than a live
> capture.

Schema and query (a 3-table join with a selective filter):

```sql
-- users(id PK, city), orders(id PK, user_id FK, total), items(id PK, order_id FK)
EXPLAIN ANALYZE
SELECT u.id, count(*)
FROM users u
JOIN orders o ON o.user_id = u.id
JOIN items  i ON i.order_id = o.id
WHERE u.city = 'London'
GROUP BY u.id;
```

Representative annotated plan (illustrative numbers):

```
 HashAggregate  (cost=2450.0..2460.0 rows=1000 width=12)
                (actual time=18.2..18.9 rows=970 loops=1)
   Group Key: u.id
   ->  Hash Join  (cost=820.0..2300.0 rows=12000 width=4)
                  (actual time=4.1..15.0 rows=11800 loops=1)
         Hash Cond: (i.order_id = o.id)
         ->  Seq Scan on items i   (rows=50000) (actual rows=50000)
         ->  Hash  (rows=3000) (actual rows=2950)
               ->  Hash Join  (cost=120.0..700.0 rows=3000 width=8)
                              (actual rows=2950)
                     Hash Cond: (o.user_id = u.id)
                     ->  Seq Scan on orders o  (rows=20000) (actual rows=20000)
                     ->  Hash  (rows=1000)
                           ->  Index Scan using users_city_idx on users u
                                 Index Cond: (city = 'London')
                                 (cost=0.4..110.0 rows=1000) (actual rows=970)
 Planning Time: 0.6 ms
 Execution Time: 19.4 ms
```

**Interpretation.**
- **Access-path choice:** the planner used an *Index Scan* on `users` because the
  `city = 'London'` predicate is selective (≈1000 of N rows), but *Seq Scans* on
  `orders` and `items` because the joins consume most of those tables — the same
  selectivity-vs-cost reasoning MiniDB's optimizer performs.
- **Join method/order:** *Hash Joins* are chosen for these equi-joins on large
  inputs; the smaller, filtered `users` relation is built into a hash table
  (build side) and probed by `orders`, matching the "smaller relation first"
  heuristic.
- **Estimate vs. actual:** estimated `rows=1000` vs actual `rows=970` on the
  `users` filter — a close estimate driven by column statistics. Large divergence
  here would signal stale statistics; `ANALYZE` refreshes them.
- **`pg_statistic`:** these row estimates come from per-column stats (null
  fraction, n_distinct, most-common-values, and a histogram) gathered by `ANALYZE`
  and exposed via the `pg_stats` view. Selectivity of `city='London'` is estimated
  from the MCV list / histogram, exactly the kind of `ndv`-based estimate MiniDB
  approximates with `1/ndv`.

To reproduce on a real server: `createdb demo`, create the tables, load data,
run `ANALYZE`, then run the query above with and without
`SET enable_indexscan = off` to watch the plan and timings change.

## 6. Key Learnings

- A DBMS is a **layered** system: a process/connection model on top of a buffer
  manager on top of page-based storage, with indexing, concurrency, and recovery
  as cross-cutting concerns — the same decomposition MiniDB implements.
- **Clock-sweep** shows that the *right* engineering answer is often a cheap
  approximation (of LRU) that behaves well under concurrency, not the textbook
  ideal.
- **MVCC** is a powerful way to make readers and writers coexist, but it trades
  blocking for *space and maintenance* (versions, visibility checks, VACUUM) — no
  free lunch.
- **WAL + LSNs + checkpoints** turn durability and recovery into a disciplined
  ordering rule ("log before data, fsync at commit") plus idempotent redo — the
  single most important idea for surviving crashes, and the one MiniDB models most
  directly.
- Studying PostgreSQL clarified *why* MiniDB made the simplifying choices it did
  (table-level locking instead of MVCC, WAL-as-truth replay instead of ARIES redo
  from checkpoints, an in-memory B-tree instead of a concurrent B-link tree) and
  what it would take to close each gap.

## Sources

1. J. M. Hellerstein, M. Stonebraker, J. Hamilton. *Architecture of a Database
   System.* Foundations and Trends in Databases, 2007. (`fntdb07-architecture.pdf`)
2. A. Petrov. *Database Internals.* O'Reilly, 2019. (`Database Internals.pdf`)
3. C. Mohan et al. *ARIES: A Transaction Recovery Method Supporting
   Fine-Granularity Locking and Partial Rollbacks Using Write-Ahead Logging.*
   ACM TODS, 1992. (`aries.pdf`)
4. The PostgreSQL Global Development Group. *PostgreSQL Documentation* — Chapters
   on Storage, Indexes (nbtree), Concurrency Control (MVCC), and Reliability
   (WAL), and the `src/backend/storage/buffer`, `access/nbtree`, and `access/transam`
   source trees. https://www.postgresql.org/docs/current/
