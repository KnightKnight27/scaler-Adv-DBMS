# PostgreSQL Internals — Buffer Manager, B-Trees, MVCC, and WAL

**Name:** Parth Sankhla
**Roll Number:** 24BCS10229

I picked this topic partly because, without realizing it, I'd already built two pieces of it by hand in the labs. The clock-sweep cache from Lab 3 turned out to be more or less how Postgres manages its buffer pool, and the B-trees from Lab 5 and Lab 6 are the same family of structure Postgres uses for its indexes. So this write-up is me connecting the toy versions I wrote to the real thing, and filling in the two parts I hadn't touched: MVCC and WAL.

The four pieces fit together as a story: the **buffer manager** decides what's in memory, the **B-tree** decides how indexes are searched, **MVCC** decides who sees which version of a row, and **WAL** decides how all of it survives a crash.

---

## 1. Problem Background

A database has one uncomfortable fact to deal with: memory is fast and small, disk is slow and durable, and a transactional system needs both at once. It needs the speed of memory to actually run queries, and the durability of disk so a power cut doesn't erase a committed order.

Postgres's internals are basically four answers to that tension:

- It can't keep everything in RAM, so it needs a **cache with an eviction policy** (the buffer manager).
- It can't scan whole tables for every lookup, so it needs **indexes** that find rows in a few page reads (nbtree).
- It wants many users reading and writing at once without blocking each other, so it needs **versioning** instead of locking-everywhere (MVCC).
- It must survive a crash mid-write without corrupting data, so it needs a **log written before the data** (WAL).

Each one is a classic systems trade-off, and seeing them in one engine is what made the course click for me.

---

## 2. Architecture Overview

Here's how a query touches each subsystem:

```
   SQL query
      |
      v
   planner  --uses--> pg_statistic (row counts, distributions)
      |
      v
   executor
      |
      |   needs a page?
      v
   +-----------------------------+
   |     Buffer Manager          |   shared_buffers (8KB frames)
   |  page in pool? -> use it    |
   |  not present? -> evict via   |
   |  clock sweep, read from disk |
   +--------------+--------------+
                  |
   index search   |   row visibility
   via nbtree     |   via MVCC (xmin/xmax + snapshot)
                  v
            heap pages on disk
                  ^
                  |  every change logged first
            +-----+------+
            |    WAL      |  --> checkpointer flushes dirty
            +------------+        buffers; crash recovery replays
```

The thing I want to stress: the executor almost never talks to the disk directly. It asks the buffer manager for a page. Whether that's a cheap memory hit or an expensive disk read is the buffer manager's call, and that single layer is where a lot of performance lives.

---

## 3. Internal Design

### Buffer Manager (this is my Lab 3 clock sweep, grown up)

Postgres keeps a pool of 8 KB frames in shared memory, sized by `shared_buffers` (128 MB in my Lab 2 cluster). When a query needs a page, the manager checks if it's already in a frame. If yes, hit. If not, it has to evict something to make room — and *how* it chooses is exactly the algorithm I implemented in Lab 3.

In Lab 3 I wrote a `ClockSweep<T>` cache: every frame has a reference bit, accessing a frame sets the bit, and when I need to evict, a "hand" sweeps around the frames — if the bit is 1 it clears it (a second chance) and moves on; if the bit is 0, that frame gets evicted. Postgres does the same thing with a `usage_count` instead of a single bit, decrementing it as the clock hand passes and evicting when it hits zero. Pages also get **pinned** while in use so they can't be evicted mid-read, and **dirty** pages have to be written out before their frame is reused.

Writing the toy version first was the best possible preparation — I already understood *why* there's a hand, *why* there's a reference bit, and *why* "second chance" beats plain FIFO. The real one just adds pinning, dirty tracking, and concurrency.

### B-Tree (nbtree) — the same trees from Lab 5/6, tuned for concurrency

My Lab 5 red-black tree and Lab 6 B-tree taught me the balancing logic. Postgres's index is a B-tree too, but it's a specific variant (a Lehman-and-Yao style B-link tree) designed so that many backends can search and even split pages concurrently with very little locking. Each page keeps a link to its right sibling, so a reader that arrives during a split can still follow the link and find its key instead of getting lost.

The index doesn't store rows — it stores keys plus a pointer (ctid) to where the row physically lives in the heap. That's why an index lookup costs "a few B-tree levels plus one heap fetch," which I later confirmed exactly in the experiments.

### MVCC — the part I hadn't built before

This was the genuinely new idea for me. In Postgres an `UPDATE` does **not** overwrite the row in place. Every row version (tuple) carries two hidden columns, `xmin` (the transaction that created it) and `xmax` (the transaction that deleted/superseded it). An update writes a *new* tuple with a fresh `xmin` and stamps the old one's `xmax`.

Whether a transaction *sees* a given tuple depends on its **snapshot** — essentially the set of transactions that had committed when it started. A reader looks at `xmin`/`xmax`, compares against its snapshot, and decides visibility. The payoff is huge: readers never lock rows and never block writers, because they're just reading the version that was valid for them.

The catch is that old, no-longer-visible tuples pile up. That's the dead weight **VACUUM** exists to clear. VACUUM also "freezes" very old transaction IDs so the 32-bit xid counter can't wrap around and make old rows appear to be from the future.

### WAL (Write-Ahead Logging)

The rule is in the name: before Postgres modifies a data page, it writes a record describing that change to the WAL and flushes *that* to disk. Only later does the actual data page get written. So if the machine dies, recovery replays the WAL from the last **checkpoint** forward and reconstructs every committed change. The **checkpointer** periodically flushes dirty buffers and records a safe restart point; the **bgwriter** trickles dirty pages out so checkpoints aren't one giant stall. The same WAL stream is also what physical replication ships to standbys.

---

## 4. Design Trade-Offs

- **Buffer pool sizing.** Bigger `shared_buffers` means more hits, but Postgres also leans on the OS page cache, so doubling it isn't free and can even hurt by double-buffering. The clock sweep itself is a trade-off: it's cheaper than true LRU and "good enough," accepting slightly worse choices for far less bookkeeping — the exact reason I used it in Lab 3.
- **MVCC's price is bloat.** No reader-writer blocking is wonderful, but you pay for it with dead tuples and the constant background cost of VACUUM. Postgres chose "writers create new versions" and then has to clean up; that's a deliberate trade, and the contrast with InnoDB's undo-log approach (which I covered in the MySQL write-up) made the choice much clearer.
- **WAL is overhead you can't skip.** Every change is essentially written twice (once to the log, once to the data files). That's pure cost on paper, but it's what turns "we hope the data is safe" into "the data is safe," and it's what makes replication possible. Cheap insurance for an expensive problem.
- **Plans depend on statistics.** The planner's choices are only as good as `pg_statistic`. Stale stats lead to bad plans, which is why ANALYZE matters — the planner is guessing, just with good data.

---

## 5. Experiments / Observations

From Lab 2, running `EXPLAIN (ANALYZE, BUFFERS)` on the `users` table (200,000 rows) is where the theory turned into numbers I could point at.

**Full scan** — `SELECT COUNT(*), SUM(LENGTH(bio)) FROM users`:

```
Finalize Aggregate  (actual time=37.877..41.058 rows=1)
  Buffers: shared hit=3364
  ->  Gather  Workers Planned: 2  Workers Launched: 2
        ->  Partial Aggregate
              ->  Parallel Seq Scan on users (rows=66666 loops=3)
Execution Time: 41.211 ms
```

The scan read `Buffers: shared hit=3364`, and that number is exactly the relation's `relpages` (3364 × 8 KiB = 26 MB). So the buffer manager really did touch every page of the heap once — the abstraction lined up with the count perfectly. I also saw the planner spin up two parallel workers on its own.

**Indexed lookup** — `WHERE email = '...'`:

```
Index Scan using idx_users_email on users
  (actual time=0.023..0.024 rows=1)
  Buffers: shared hit=4
Execution Time: 0.105 ms
```

Four buffers: three for the B-tree descent and one for the heap page the ctid pointed at. That's the nbtree-plus-heap-fetch model from section 3, made literal. And the execution time was **0.105 ms** — the disciplined index work — even though the round-trip from `psql` was ~40 ms warm, almost all of which was protocol and IPC rather than the database engine.

I didn't get to run a clean crash-recovery WAL experiment, but the buffer-hit counts and the visible background processes (checkpointer, walwriter, bgwriter in `ps`) were enough to connect the WAL/checkpoint story to something I could observe.

---

## 6. Key Learnings

- **I'd already built the hardest-to-picture part.** Postgres's buffer manager is my Lab 3 clock sweep with pinning and dirty-page handling bolted on. Implementing the toy first made the real design feel obvious instead of intimidating.
- **An index lookup is a tiny, countable thing.** `Buffers: shared hit=4` for a single-row lookup — three B-tree levels and one heap page — was the moment nbtree stopped being abstract.
- **MVCC explains VACUUM.** Once I understood that updates create new tuple versions keyed by `xmin`/`xmax`, the existence of dead tuples, VACUUM, and even xid freezing all followed naturally. It's not a bug, it's the bill for non-blocking reads.
- **WAL is the quiet backbone.** Log-before-data plus checkpoints is what makes durability and replication possible, and it's running constantly even though I never see it directly.
- **The planner is only as smart as its stats.** Good plans come from good `pg_statistic`, which reframed query tuning for me as "help the planner estimate" rather than "outsmart the planner."
