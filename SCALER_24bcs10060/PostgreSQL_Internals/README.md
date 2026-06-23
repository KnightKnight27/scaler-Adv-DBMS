# PostgreSQL Internal Architecture

> Advanced DBMS — System Design Discussion
> Topic 2: PostgreSQL Internals

For this topic I tried to look *inside* PostgreSQL — how it actually stores pages
in memory, how its B-tree index works, how MVCC lets many people use it at once,
and how WAL keeps data safe after a crash. I'm still learning, so I kept the
explanations simple and used small diagrams to help myself understand.

---

## 1. Problem Background

A database server has to do a few hard things at the same time:
1. Be **fast** — disk is slow, so it must cache data in memory.
2. Let **many users** read and write at once without corrupting data.
3. **Never lose committed data**, even if the power cuts out mid-write.

PostgreSQL's internal components each solve one of these:
- **Buffer Manager** → speed (caching pages in memory).
- **B-Tree** → fast lookups instead of scanning whole tables.
- **MVCC** → many users at once.
- **WAL** → durability and crash recovery.

I'll go through each one.

---

## 2. Architecture Overview

High-level flow of a query and where each component sits:

```
   SQL query
      |
      v
  [ Parser ] -> [ Planner/Optimizer ] -> [ Executor ]
                       ^                       |
                       |                       v
                 pg_statistic           [ Buffer Manager ]
                 (table stats)            (shared buffers cache)
                                               |
                                  (page in cache?) -- yes --> use it
                                               | no
                                               v
                                        read 8KB page from disk
                                               |
                                               v
                                          DATA FILES (heap + indexes)

  Every change also writes a record to:  [ WAL ]  -> wal files on disk
  (WAL is flushed on COMMIT to guarantee durability)
```

Main components I'm studying:
- **Buffer Manager** (`src/backend/storage/buffer/`)
- **B-Tree index** (`nbtree`)
- **MVCC** (heap tuples with `xmin`/`xmax`)
- **WAL** (write-ahead log)

---

## 3. Internal Design

### 3.1 Buffer Manager (shared buffers)

Disk is slow, RAM is fast. So PostgreSQL keeps a big chunk of shared memory called
**shared buffers**, which holds copies of recently used **8 KB pages**.

How a page is read:

```
Executor needs page X
        |
        v
  Is page X already in shared buffers?
   |                       |
  YES                      NO
   |                       |
   v                       v
 use the cached copy   find a free buffer slot
 (a "buffer hit")      (or evict an old page to make room)
                          |
                          v
                       read page X from disk into the slot
```

- Each page in a buffer slot has a **pin count** (how many backends are using it
  right now) and a **dirty flag** (has it been changed since loaded?).
- **Buffer replacement:** when the cache is full and a new page is needed,
  PostgreSQL must evict one. It uses a **clock-sweep** algorithm (a simple,
  approximate "least recently used" idea). Each buffer has a usage counter; the
  sweep goes around decreasing counters and evicts one that reaches zero.
- **Dirty pages** (changed in memory) are **not** written to disk immediately.
  They get written later by the background writer or at a **checkpoint**. This is
  safe because WAL already recorded the change (see WAL section).

So pages move like: `disk -> shared buffers -> (changed) -> dirty -> flushed back to disk`.

### 3.2 B-Tree index (nbtree)

Without an index, finding one row means scanning the whole table. A **B-tree**
index keeps keys **sorted** in a tree so lookups are fast (logarithmic).

```
                 [ root page ]
                /     |      \
        [internal] [internal] [internal]      <- guide pages (just keys + pointers)
         /    \       ...
   [leaf]----[leaf]----[leaf]----[leaf]        <- leaves hold keys + pointer (TID) to heap row
     ^_________doubly linked list_________^     (so range scans walk sideways)
```

- **Search:** start at the root, compare the search key, follow the right child
  pointer down until you reach a leaf. The leaf has the key and a **TID** (block +
  item number) pointing to the actual row in the heap.
- **Index page layout:** like other pages, each B-tree page has a header, an array
  of item pointers, and the actual index entries. Leaf pages are linked left↔right
  so a range query (`WHERE age BETWEEN 20 AND 30`) can just walk along the leaves.
- **Insert:** find the correct leaf, put the new key in sorted order.
- **Page split:** if the leaf is full, it **splits** into two pages and the middle
  key is pushed up to the parent. If the parent is also full, it splits too — this
  can go all the way up and grow the tree taller. This keeps the tree balanced.

One thing I learned: in PostgreSQL the index entry points to the heap, but the
heap row might have *multiple versions* (because of MVCC), so the index alone
can't always tell which version is visible — the executor still has to check
visibility in the heap.

### 3.3 MVCC (Multi-Version Concurrency Control)

This is PostgreSQL's way of letting many transactions run at once. Instead of
locking rows when reading, it keeps **multiple versions** of each row.

Every heap row (tuple) has two hidden system columns:
- **`xmin`** = the transaction ID that **created** this row version.
- **`xmax`** = the transaction ID that **deleted/replaced** this row version
  (0/empty if the row is still live).

**UPDATE does not overwrite in place.** It marks the old version as deleted
(sets its `xmax`) and inserts a **brand new row version** with a new `xmin`.

```
Before UPDATE:   row A  (xmin=100, xmax=0)        <- live

After UPDATE:    row A  (xmin=100, xmax=105)      <- old version, now "dead"
                 row A' (xmin=105, xmax=0)        <- new version, live
```

**Visibility rules (simplified):** a transaction sees a row version if:
- its `xmin` is from a transaction that already **committed** before this one's
  snapshot, AND
- its `xmax` is empty, or the deleting transaction had **not** committed yet at
  snapshot time.

**Snapshot isolation:** when a transaction (or statement) starts, it takes a
**snapshot** = the set of transactions that were committed at that moment. It then
only sees row versions consistent with that snapshot. This is why two users can
read and write at the same time and each sees a stable view.

**Why VACUUM is necessary:** all those old "dead" row versions don't disappear by
themselves. Over time they pile up and waste space and slow down scans.
**VACUUM** is the cleanup process that removes dead tuples and frees space for
reuse. **Autovacuum** runs this automatically in the background. So MVCC's price
is that you must keep vacuuming.

### 3.4 WAL (Write-Ahead Logging)

The rule is in the name: **write the log before the data.** Before PostgreSQL
changes a data page on disk, it first writes a small **WAL record** describing the
change, and makes sure that record is safely on disk.

```
Transaction changes a row
        |
        v
1. Write a WAL record describing the change (to WAL buffer)
        |
        v
2. On COMMIT -> flush WAL to disk (fsync). Now it is durable.
        |
        v
3. The actual data page can be written to disk later (lazily).
```

**Durability guarantee:** once COMMIT returns, the change is in the WAL on disk.
Even if the data files weren't updated yet and the server crashes, the change is
not lost — it can be replayed.

**Crash recovery:** when PostgreSQL restarts after a crash, it **replays the WAL**
from the last checkpoint forward, re-applying all changes that were committed but
maybe not yet written to the data files. This brings the database back to a
consistent state.

**Checkpointing:** a **checkpoint** is a point where PostgreSQL flushes all dirty
pages to disk and records "everything up to here is safely on disk." This means
recovery only needs to replay WAL *from the last checkpoint*, not from the
beginning of time. Checkpoints happen periodically (time / WAL size based).

So WAL gives durability *and* lets the buffer manager be lazy about flushing data
pages, which is good for performance.

---

## 4. Design Trade-Offs

- **Buffer Manager:** caching gives huge speed-ups, but the cache is limited, so
  the replacement policy (clock-sweep) matters. Too small `shared_buffers` →
  more disk reads.
- **MVCC vs locking:** MVCC means readers don't block writers (great for
  concurrency), but the cost is **bloat** (dead tuples) and the need for VACUUM.
  A lock-based system avoids bloat but makes readers and writers wait on each
  other.
- **WAL:** gives durability and faster commits (sequential log write instead of
  random data writes), but it means **every change is written twice** (once to
  WAL, once to the data file later) — this is *write amplification*. Worth it for
  safety.
- **B-tree:** great for equality and range queries and keeps data sorted, but
  inserts can cause page splits, and indexes need maintenance and take extra disk
  space.

---

## 5. Experiments / Observations

### EXPLAIN ANALYZE on a multi-table join (recommended exercise)

I made two small tables and ran a join:

```sql
CREATE TABLE students(id SERIAL PRIMARY KEY, name TEXT, dept_id INT);
CREATE TABLE depts(id SERIAL PRIMARY KEY, dname TEXT);
-- insert some rows ...
ANALYZE;   -- collect statistics first!

EXPLAIN ANALYZE
SELECT s.name, d.dname
FROM students s
JOIN depts d ON s.dept_id = d.id
WHERE d.dname = 'CS';
```

A simplified version of the output looked like:

```
Hash Join  (cost=1.09..2.20 rows=4 width=64) (actual time=0.030..0.045 rows=5 loops=1)
  Hash Cond: (s.dept_id = d.id)
  ->  Seq Scan on students s   (cost=... rows=20 ...) (actual ... rows=20 ...)
  ->  Hash
        ->  Seq Scan on depts d (cost=... rows=1 ...) (actual ... rows=1 ...)
              Filter: (dname = 'CS')
Planning Time: 0.2 ms
Execution Time: 0.1 ms
```

What I learned to read from this:
- **Chosen plan:** PostgreSQL picked a **Hash Join** (it builds a hash table on
  the smaller `depts` table, then probes it with `students`). On tiny tables it
  used **Seq Scan** (full scan) because that's cheaper than using an index when
  the table is small.
- **Planner estimates vs actual:** `cost=... rows=4` is the planner's *estimate*;
  `actual time=... rows=5` is what really happened. If estimate and actual `rows`
  are very different, the stats are stale and the plan may be bad.
- **Statistics:** the planner doesn't guess randomly — it uses statistics stored
  in **`pg_statistic`** (readable via the view `pg_stats`): things like how many
  rows a table has, how many distinct values a column has, most common values, etc.

```sql
SELECT tablename, attname, n_distinct, most_common_vals
FROM pg_stats WHERE tablename = 'students';
```

- Running **`ANALYZE`** refreshes these stats. Before running ANALYZE the row
  estimates were way off; after ANALYZE they matched reality much better. That
  showed me directly how planning *depends* on collected statistics.

### Seeing MVCC

```sql
SELECT xmin, xmax, * FROM students LIMIT 3;     -- hidden version columns
UPDATE students SET name = name WHERE id = 1;    -- creates a new version
SELECT xmin, xmax, * FROM students WHERE id = 1; -- xmin changed = new version
```

### Seeing VACUUM clean up

After many updates, `SELECT n_dead_tup FROM pg_stat_user_tables WHERE relname='students';`
showed dead tuples > 0, and after `VACUUM students;` it dropped. This made the
"why VACUUM is needed" point very concrete.

---

## 6. Key Learnings

- **Pages move** disk → shared buffers → get pinned/used → marked dirty when
  changed → flushed back to disk later (at checkpoint / by bg writer). The buffer
  manager is basically a smart cache.
- **MVCC** = keep multiple row versions tagged with `xmin`/`xmax`, and use a
  **snapshot** so each transaction sees a consistent view. This is what makes
  PostgreSQL good at concurrency.
- **VACUUM is necessary** because MVCC leaves behind dead row versions that must
  be cleaned up to reclaim space and keep things fast.
- **WAL guarantees durability** by writing the change to the log *before* the data
  file, flushing it on commit, and replaying it after a crash. Checkpoints limit
  how much WAL has to be replayed.
- **Query planning relies on statistics** in `pg_statistic` — if stats are stale,
  the planner makes bad guesses, so `ANALYZE`/autovacuum keeping stats fresh is
  important.
- Surprising bit for me: an `UPDATE` actually creates a *new* row internally
  instead of editing the old one. That single fact explains both MVCC *and* why
  VACUUM exists.
