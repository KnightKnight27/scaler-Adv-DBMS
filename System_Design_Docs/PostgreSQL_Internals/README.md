# PostgreSQL Internal Architecture

> Advanced DBMS — System Design Discussion  
> Roll No: 10075 | Nase Anishka

---

## 1. Problem Background

PostgreSQL solves a hard problem: how do you let many users read and write the same data at the same time, without corrupting it, and without losing anything if the server crashes at the worst possible moment?

Most early databases solved concurrency by just locking things — lock the table, do the write, unlock. Simple, but terrible for performance. If one user is writing, everyone reading has to wait. PostgreSQL chose a different approach: **MVCC** (Multi-Version Concurrency Control), where readers never block writers and writers never block readers. Each transaction sees a consistent snapshot of the database as it existed at a point in time.

For crash recovery, PostgreSQL uses **WAL** (Write-Ahead Logging) — changes are written to a log before they're applied to the actual data files. If the machine loses power, the log is replayed on startup to restore committed state.

For fast query execution, it uses a **cost-based query planner** that looks at statistics about your data and chooses the best execution plan.

These three things — MVCC, WAL, and the planner — are the core of PostgreSQL. Everything else is built around them.

---

## 2. Architecture Overview

```
  Client (psql / app / ORM)
         |
         | TCP or Unix socket
         v
  +------------------+
  |    Postmaster     |   supervisor process
  |                   |   forks one backend per connection
  +------------------+
     |      |      |
     v      v      v
  backend  backend  backend   (each is a separate OS process)
     |      |      |
     +------+------+
            |
            v
  +-----------------------------+
  |       Shared Memory          |
  |                              |
  |  shared_buffers (page cache) |  ← 8KB pages, shared across all backends
  |  WAL buffers                 |
  |  lock tables                 |
  +-----------------------------+
            |
      +-----+-----+
      |           |
  Data files    pg_wal/
  (heap +       (write-ahead log)
   indexes)

  Background: bgwriter, checkpointer, walwriter, autovacuum
```

Each client gets its own backend process — not a thread, a full OS process. This is unusual but intentional: if one backend crashes, it can't corrupt shared memory. All backends share one page cache (`shared_buffers`). Every change is logged to WAL before COMMIT returns, so a crash can always be recovered by replaying the log from the last checkpoint.

---

## 3. Internal Design

### Buffer Manager

The buffer manager is PostgreSQL's page cache. It holds 8KB pages in RAM so queries don't have to go to disk every time.

When a backend needs a page, it asks the buffer manager. If the page is in `shared_buffers`, great — it's returned immediately. If not, the buffer manager fetches it from disk and puts it in a free slot.

Page eviction uses **clock-sweep** — simpler than LRU but works well in practice. Each page has a small usage counter. When the buffer manager needs a free slot, it walks through the pages like a clock hand. If a page's counter is above 0, it decrements it and moves on. If it's 0 and the page isn't pinned (being actively used), it evicts it. Pages that are accessed frequently accumulate higher counters and survive longer.

### How rows are stored (Heap + MVCC)

A PostgreSQL table is called a **heap** — rows are stored wherever there's free space, with no particular ordering. Each page is 8KB and contains a header, an array of item pointers (each pointing to a tuple), and the actual tuples packed from the other end.

Every row (called a tuple) carries two important hidden fields:
- `xmin` — the transaction ID that created this row
- `xmax` — the transaction ID that deleted this row (0 means it's still live)

This is the foundation of MVCC. When you run `UPDATE`, PostgreSQL doesn't overwrite the row. It inserts a new version of the row and sets `xmax` on the old one. Both versions exist in the heap simultaneously. Each transaction, based on its snapshot, decides which version is "visible" to it.

Example:
```
Snapshot taken when txn IDs 1-10 are committed, 11 is active

Row version A: xmin=5, xmax=11  → not visible (deleted by active txn 11)
Row version B: xmin=11, xmax=0  → not visible (created by active txn 11)
Row version C: xmin=7, xmax=0   → visible (created by committed txn 7, still live)
```

The old row versions ("dead tuples") pile up in the heap. VACUUM comes along and removes them once no snapshot can possibly need them anymore.

### B-Tree Indexes

PostgreSQL uses B-trees for most indexes. What makes PostgreSQL's B-tree interesting is that it supports **concurrent inserts without locking the whole tree**.

The trick is sibling links — every page at a given level is connected to its right neighbor:

```
        [ 40 | 80 ]
       /     |     \
  [10-30] [50-70] [90-100]
     ←→       ←→       ←→   (doubly linked at each level)
```

When a page splits, the new right page is linked in before the parent pointer is updated. A concurrent reader that arrives "just after" the split can follow the sibling link to find the right page, instead of going back up the tree. This means inserts and reads on the same index can proceed in parallel with only fine-grained page-level locking.

**Index-only scans:** if every column a query needs is already in the index, PostgreSQL can answer the query without ever touching the heap. This is called an index-only scan. It checks the Visibility Map (a bitmap per page) to confirm all tuples on the relevant pages are visible before trusting the index without a heap fetch.

### WAL (Write-Ahead Logging)

Before any change is written to a data page, the change must be recorded in the WAL. The WAL is a sequential append-only log identified by LSNs (Log Sequence Numbers).

At COMMIT, the WAL buffer is flushed to disk. Only then does COMMIT return to the client. The data page can be flushed later in the background. This means if the machine crashes after COMMIT returns, the data is safe in the WAL even if the data page wasn't written yet.

Crash recovery:
1. Find the last checkpoint (stored in `pg_control`)
2. Replay all WAL records after that checkpoint, in order
3. Any transaction that was active at crash time (no COMMIT in the log) is rolled back

The setting `synchronous_commit` controls how aggressive this is. At `on` (default), WAL is fsynced at every COMMIT — fully safe. At `off`, commits return immediately but you can lose the last ~200ms of committed transactions on a hard crash. It's a durability/performance dial.

### Query Planner and Statistics

PostgreSQL's planner is **cost-based** — it doesn't just pick the first plan it finds, it estimates the cost of multiple options and picks the cheapest.

To estimate costs, it needs statistics about your data. That's what `ANALYZE` does — it scans a sample of the table and populates `pg_statistic` with things like:
- How many distinct values does this column have?
- What are the most common values?
- What does the distribution look like (histogram)?

The planner uses these stats to estimate how many rows a condition will return. If it thinks a WHERE clause filters down to 100 rows, it might use an index. If it thinks it'll return 80% of the table, it'll probably do a sequential scan instead.

If statistics are out of date, the planner makes bad estimates, which means bad plans, which means slow queries. This is one of the most common causes of unexpected performance problems in PostgreSQL.

---

## 4. Design Trade-Offs

**MVCC is great for concurrency, but creates garbage.**  
Readers and writers never block each other — that's a big win. But every UPDATE leaves a dead old version in the heap. If VACUUM can't keep up (especially if a long-running transaction pins old snapshots), tables bloat with dead tuples that slow down scans. This is a real operational concern in high-write workloads.

**One process per connection is safe but expensive.**  
Each backend is a full OS process. A crash in one session can't affect others. But each process costs 5–10 MB of RAM, and forking is not free. At hundreds of simultaneous connections, this adds up. That's why connection poolers like PgBouncer are standard in production — they multiplex many clients onto a smaller number of actual backend processes.

**The heap gives fast inserts but slow unsorted range scans.**  
Since rows go wherever there's free space, inserts are cheap — no tree to maintain. But a range scan on a non-indexed column has to read every row in the table. InnoDB (MySQL) stores rows in primary-key order (clustered index), which makes PK range scans much faster, but random inserts cause page splits.

**Plan quality depends entirely on statistics freshness.**  
A stale `pg_statistic` means the planner is guessing based on old data. It might pick a sequential scan when an index would be 50× faster, or vice versa. `autovacuum` runs `ANALYZE` regularly, but on rapidly changing tables, statistics lag behind. Manually running `ANALYZE` after bulk loads is often necessary.

---

## 5. Experiments / Observations

**Dead tuples are visible in `pg_stat_user_tables`.**  
After running a bunch of updates, `n_dead_tup` in `pg_stat_user_tables` climbs noticeably. Running `VACUUM` on the table brings it back down to near zero. This makes the MVCC "garbage accumulation" problem very concrete and measurable.

**EXPLAIN ANALYZE shows the planner in action.**  
Running `EXPLAIN ANALYZE SELECT ...` on a multi-table join shows: the join type chosen (hash join vs nested loop), the estimated vs actual row counts, and which indexes were used. When estimated rows are far off from actual rows, it's usually a sign that statistics need updating.

**Index-only scans need VACUUM too.**  
On a freshly loaded and vacuumed table, `EXPLAIN (ANALYZE, BUFFERS)` for a covering-index query shows `Heap Fetches: 0`. After inserting new rows (which mark some pages as "not all-visible"), the same query starts showing non-zero heap fetches because PostgreSQL can't trust the index without checking the heap for those pages.

**WAL generates full page images after checkpoints.**  
After a checkpoint, the first write to any page generates a Full Page Image in WAL — the entire 8KB page. This protects against partial page writes on crash. This is why WAL size can spike significantly right after a checkpoint, even for small changes.

**Statistics matter more than you'd expect.**  
Creating an index on a column and then running a query that should use it — but seeing a sequential scan in EXPLAIN — is often just stale statistics. Running `ANALYZE` and re-checking usually switches the plan to an index scan immediately.

---

## 6. Key Learnings

1. **MVCC, WAL, and the buffer manager are tightly connected.** MVCC stores old row versions in the heap. WAL records changes before they hit data pages. The buffer manager holds dirty pages in RAM until they're flushed. Removing any one of these breaks the ACID guarantees that the other two are trying to provide.

2. **MVCC shifts the cost from blocking to cleanup.** The benefit is that reads and writes never block each other. The cost is that old row versions accumulate and must be cleaned by VACUUM. Long-running transactions make this worse because they prevent VACUUM from reclaiming old versions that are still "potentially visible."

3. **The planner's quality depends on statistics quality.** Having the right indexes isn't enough — the planner needs accurate statistics to decide whether to use them. After bulk loads or major changes, running ANALYZE is as important as having the index in the first place.

4. **WAL is the real source of truth after a crash.** The heap files may be in an inconsistent state after a crash. The WAL has the complete, ordered record of what was committed. Streaming replication, point-in-time recovery, and logical decoding all work by consuming the WAL stream.

5. **The process-per-connection model is a deliberate safety choice.** It uses more memory than threads, but one crashing backend can't corrupt what other backends are doing. In practice, connection pooling handles the overhead, so the safety benefit is kept while the cost is managed.

---

## References

- PostgreSQL Documentation: [MVCC](https://www.postgresql.org/docs/current/mvcc.html), [WAL](https://www.postgresql.org/docs/current/wal.html), [Buffer Manager](https://www.postgresql.org/docs/current/runtime-config-resource.html), [B-Tree](https://www.postgresql.org/docs/current/btree.html), [pg_statistic](https://www.postgresql.org/docs/current/catalog-pg-statistic.html)
- PostgreSQL source: `src/backend/storage/buffer/`, `src/backend/access/nbtree/`, `src/backend/access/transam/`
