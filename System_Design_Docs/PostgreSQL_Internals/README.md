# PostgreSQL Internal Architecture

## 1. Problem Background

PostgreSQL is built to be a serious, production-grade relational database. But what does that actually mean under the hood?

As databases grow — both in data size and number of concurrent users — a bunch of really hard problems come up. You can't just read from disk every time someone runs a query; that'd be way too slow. You need to cache pages in memory intelligently. You also can't just lock the whole table whenever someone writes to it, or reads would stall. And you absolutely need to survive crashes without losing committed data.

So PostgreSQL's internals are basically a collection of subsystems each designed to tackle one of these problems: the Buffer Manager handles caching, MVCC handles concurrency, B-Trees handle fast lookups, and WAL handles crash recovery. What's cool is how they all interact — the WAL records B-Tree page splits, the buffer manager serves pages that MVCC checks for visibility, and so on. It's all interconnected.

## 2. Architecture Overview

At a high level, here's how the pieces fit together:

- A **client** sends a query to a **backend process**
- The backend runs it through the **query planner** (which decides *how* to execute the query — hash join vs nested loop, which indexes to use, etc.)
- The **executor** actually runs the plan, pulling pages through the **Buffer Manager**
- Writes get logged in the **WAL** before data pages are modified on disk
- The **MVCC engine** tracks which row versions each transaction can see
- In the background, the **checkpointer** and **background writer** flush dirty pages to disk

```
   Client
     │
  Backend Process
     │
  Query Planner ──► decides join strategies, index usage
     │
  Executor
     ├── reads/writes pages via Buffer Manager
     ├── checks tuple visibility via MVCC (xmin/xmax)
     └── logs changes to WAL
               │
          WAL on disk (sequential writes)
               │
     Background Writer + Checkpointer
               │
          Data files on disk
```

## 3. Internal Design

### Buffer Manager (src/backend/storage/buffer/)

The buffer manager maintains a pool of **shared buffers** — basically a big cache of 8KB pages in memory.

When a query needs a page:
1. First check if it's already in the buffer pool (cache hit → fast)
2. If not, read the page from disk into a free buffer slot
3. If there's no free slot, evict something

For eviction, PostgreSQL uses a **clock-sweep algorithm**, which is a variant of LRU. Each buffer has a "usage count" that gets bumped when accessed. The clock sweep goes around the pool decrementing usage counts, and evicts the first buffer it finds with a count of zero. It's not as fancy as some other algorithms, but it's simple and works well in practice.

Pages that have been modified (dirty pages) need to eventually make it to disk. The background writer handles this — it continuously flushes dirty pages so the buffer pool doesn't get clogged with unwritten data.

### B-Tree Index Implementation (nbtree)

B-Trees are the default index type in PostgreSQL, and they're used for most `WHERE` clause lookups.

The structure is what you'd expect from a textbook B-Tree:
- **Leaf nodes** hold the actual index entries, which include the key value and a TID (Tuple ID — basically a pointer to where the row lives in the heap)
- **Internal nodes** guide the search from root to leaf
- **Search** works by walking from root → internal pages → leaf page, comparing keys at each level

The interesting part is **page splits during inserts**. When you try to insert a key into a leaf page that's already full:
1. Postgres allocates a new page
2. Moves roughly half the keys to the new page
3. Updates the parent node with a pointer to the new page and a new separator key

All of this gets logged to WAL, which is critical. If the system crashes in the middle of a page split (which involves modifying multiple pages), the WAL ensures the tree can be restored to a consistent state on recovery.

### MVCC (Multi-Version Concurrency Control)

This is probably the most important concept in PostgreSQL's concurrency model.

Instead of locking rows when they're being read or written, PostgreSQL keeps **multiple versions** of each row. Every row (tuple) has two hidden columns:
- **xmin**: the transaction ID that created this version
- **xmax**: the transaction ID that deleted/updated this version (0 if still live)

When a transaction starts, it takes a **snapshot** — basically a record of which transactions are currently in progress. Then, when it reads a row, it uses visibility rules:
- Is `xmin` committed and older than my snapshot? → I can see this row
- Is `xmax` set to a committed transaction that finished before my snapshot? → This row has been deleted/updated, I shouldn't see it

This is how PostgreSQL achieves snapshot isolation without readers blocking writers or vice versa.

**The VACUUM problem:** Since updates create new tuple versions and deletes just set xmax, old dead tuples pile up in the heap over time. They're invisible to all current transactions but still taking up space. `VACUUM` is the process that scans tables and reclaims this wasted space. If VACUUM doesn't keep up (especially on tables with heavy write traffic), you get "table bloat" — the table file keeps growing even though the logical data hasn't. This is probably the most-complained-about operational aspect of PostgreSQL.

### WAL (Write-Ahead Logging)

The core guarantee: before a modified data page is flushed to disk, the WAL record describing that modification must be flushed first.

Why? Because if the system crashes:
1. The buffer pool (in-memory) is gone
2. Some dirty pages might not have made it to disk
3. But the WAL (on disk) has a complete sequential record of every change
4. On restart, PostgreSQL replays the WAL from the last checkpoint forward, reapplying changes

The **checkpointer** periodically forces all dirty pages to disk and records a checkpoint in the WAL. This way, crash recovery only needs to replay from the most recent checkpoint, not from the beginning of time.

WAL writes are sequential (just appending to a file), which is much faster than the random I/O that data file updates would require. This is a big part of why WAL works so well.

## 4. Design Trade-Offs

**What works well:**
- MVCC is fantastic for read-heavy concurrent workloads. Readers literally never block writers, and writers never block readers. This is a huge deal for web applications where you have tons of reads happening alongside writes.
- The buffer manager + OS page cache combination means that hot data stays in memory without much manual tuning.

**What's painful:**
- **Write amplification**: updating a single column can trigger writing a new heap tuple, WAL records, and potentially updating multiple index entries. PostgreSQL has HOT (Heap-Only Tuple) optimization to avoid index updates when you're only changing non-indexed columns and the new tuple fits on the same page, but it doesn't always kick in.
- **VACUUM/table bloat**: this is a real operational headache. Unlike InnoDB (which does in-place updates with undo logs), PostgreSQL's MVCC creates dead tuples in the main data files. If autovacuum isn't tuned properly or can't keep up with the write rate, tables can balloon in size. I've read about production databases where tables were 10x their logical size due to bloat.

## 5. Experiments / Observations

### EXPLAIN ANALYZE on a Multi-Table Join

Here's a typical query you might analyze:
```sql
EXPLAIN ANALYZE
SELECT o.order_id, c.customer_name, sum(i.price)
FROM orders o
JOIN customers c ON o.customer_id = c.customer_id
JOIN order_items i ON o.order_id = i.order_id
GROUP BY o.order_id, c.customer_name;
```

What I'd look for in the output:

- **Join strategy**: Did the planner pick Hash Join, Merge Join, or Nested Loop? For large tables, Hash Join is usually the go-to. Nested Loop is fine when one side is small or there's a highly selective index. If you see a Nested Loop over millions of rows, something's probably wrong.

- **Estimated vs actual rows**: The output shows `rows=X` (estimate) next to `actual rows=Y`. If these numbers are way off, the planner is making bad decisions because its statistics are stale. I've seen cases where the planner estimated 100 rows but the actual count was 500,000 — it chose a Nested Loop when it should have done a Hash Join, and the query went from milliseconds to minutes.

- **pg_statistic dependency**: All of the planner's row estimates come from statistics stored in `pg_statistic` (which you can read through the `pg_stats` view). These include histograms of column value distributions, most common values, null fractions, etc. If `ANALYZE` hasn't been run recently (or autovacuum hasn't triggered it), these stats go stale and the planner essentially flies blind.

The takeaway is that query planning is an estimation problem. The planner doesn't actually look at the data — it looks at statistics *about* the data. If those statistics are wrong, the plan is wrong.

## 6. Key Learnings

- **WAL is the real safety net.** Everything important gets logged there first — not just row changes, but B-Tree page splits, clog updates, all of it. The design principle of "sequential I/O for safety, random I/O can wait" is at the heart of PostgreSQL's durability story.

- **MVCC is powerful but messy.** The elegance of "never block readers" comes at the cost of accumulating dead tuples everywhere. It's a "pay later" model — you get great concurrency now, but you need VACUUM to come clean up after you. In contrast, InnoDB's approach (in-place updates with undo logs) is more of a "pay as you go" model.

- **Query planning lives and dies by statistics.** After studying this, I realized that a slow query in PostgreSQL is often not about missing indexes — it's about stale statistics causing the planner to make terrible choices. Running `ANALYZE` regularly (or having autovacuum do it) is genuinely critical.
