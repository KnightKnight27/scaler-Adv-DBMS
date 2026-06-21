# PostgreSQL Internal Architecture

**Name:** Harshita Hirawat  
**Roll number:** 24BCS10044

## 1. Problem Background

PostgreSQL is designed to be a general-purpose, multi-user relational database.
Its architecture must solve several problems at once: execute declarative SQL,
share a cache across sessions, isolate concurrent transactions, survive crashes,
and make reasonable plans without knowing the future workload.

The server architecture is a deliberate boundary. Applications send requests;
the server owns the files and enforces one set of transaction, durability, and
security rules. PostgreSQL inherits the extensibility ideas of the POSTGRES
research project, which is visible today in its type system, access methods, and
planner.

## 2. Architecture Overview

```text
Client
  |
  v
Backend process -- parser -> rewriter -> planner -> executor
  |                                         |
  |                                         v
  |                                  access methods
  |                                  heap / B-tree
  |                                         |
  +---------------- shared memory ----------+
                    |          |
              shared buffers  lock/WAL state
                    |
          storage manager + OS cache
                    |
       relation files, WAL, catalogs

Background processes: WAL writer, checkpointer, background writer,
autovacuum workers and statistics-related maintenance
```

A backend does not normally read a table block straight into query memory. It
asks the buffer manager for the block. The buffer manager locates or loads the
page in shared buffers, pins it while in use, and coordinates dirty-page writes.

## 3. Internal Design

### 3.1 Buffer manager

`shared_buffers` is a fixed shared page cache. Conceptually, a page read follows
this path:

```text
(relation, fork, block number)
        -> buffer lookup
        -> hit: pin existing frame
        -> miss: select victim, flush if dirty, read 8 KB block
        -> return pinned buffer to executor
```

PostgreSQL uses usage counters and a clock-sweep style victim search rather than
maintaining an exact global LRU list. Exact LRU would require frequent updates
to a highly contended shared structure. Clock sweep accepts approximate recency
for less coordination. The implementation is under
[`src/backend/storage/buffer`](https://github.com/postgres/postgres/tree/master/src/backend/storage/buffer).

A dirty buffer is not the durability boundary. WAL for a change must reach
durable storage before the corresponding data page is allowed to reach disk.

### 3.2 Relations and pages

Tables and indexes are relations split into forks. The main fork stores data;
the free-space map helps locate pages with room; the visibility map records pages
known to contain tuples visible to all transactions. Large relations are split
into file segments.

A normal heap page contains:

```text
Page header | line pointers -> | free space | <- tuple data | special space
```

Line pointers give tuples stable item identifiers within a page. PostgreSQL's
[page layout](https://www.postgresql.org/docs/current/storage-page-layout.html)
also exposes tuple header fields such as `t_xmin`, `t_xmax`, and `t_ctid`.

### 3.3 B-tree indexes

A PostgreSQL B-tree contains a root, optional internal levels, and leaf pages.
Internal entries guide the search; leaf entries point to heap tuple identifiers.
When insertion cannot fit on a page, the page is split and a separator is
inserted into its parent. Searches remain logarithmic, but splits create extra
WAL and can reduce space utilization temporarily.

The implementation is under
[`src/backend/access/nbtree`](https://github.com/postgres/postgres/tree/master/src/backend/access/nbtree).
B-trees support equality and ordered comparisons; PostgreSQL documents the
operator behavior in [B-tree indexes](https://www.postgresql.org/docs/current/indexes-types.html#INDEXES-TYPES-BTREE).

### 3.4 MVCC and VACUUM

An update normally creates a new heap tuple version instead of overwriting the
old version. `xmin` identifies the creating transaction; `xmax` identifies a
deleting or superseding transaction. A snapshot decides which version is
visible. Readers can therefore continue using an older version while a writer
creates a newer one.

This design leaves dead tuples. VACUUM is necessary to:

- reclaim dead tuple space for reuse,
- maintain the visibility map,
- prevent transaction-ID wraparound,
- reduce unnecessary heap and index work.

The benefit is low read/write blocking. The cost is extra versions, vacuum work,
and possible table/index bloat when cleanup cannot keep pace. PostgreSQL's
[concurrency-control documentation](https://www.postgresql.org/docs/current/mvcc.html)
describes the user-visible model.

### 3.5 WAL and checkpoints

WAL records describe changes before changed data pages are flushed. Commit
durability means the required WAL is made durable; data pages may be written
later. After a crash, PostgreSQL replays WAL from a checkpoint to reconstruct a
consistent state.

Checkpoints bound recovery time, but very frequent checkpoints increase bursts
of data-page writes and full-page images. Infrequent checkpoints reduce that
pressure but retain more WAL and increase recovery work. This is a latency,
throughput, recovery-time, and storage trade-off. See PostgreSQL's
[WAL introduction](https://www.postgresql.org/docs/current/wal-intro.html).

### 3.6 Planner statistics

The planner compares estimated costs for scans, joins, sorting, and aggregation.
`ANALYZE` samples data and records statistics such as distinct-value estimates,
most-common values, histograms, and correlation. The readable `pg_stats` view is
derived from `pg_statistic`.

Statistics are summaries, not exact knowledge. Correlated columns, stale data,
or skew not captured by the sample can produce poor row estimates and therefore
poor plans. PostgreSQL documents these inputs under
[planner statistics](https://www.postgresql.org/docs/current/planner-stats.html).

The columns of `pg_stats` have different roles. `n_distinct` describes how many
values exist, while `most_common_vals` and `most_common_freqs` preserve sampled
skew. Histogram bounds estimate predicates outside the most-common-value list,
and correlation helps the planner estimate the cost of ordered heap access.
Extended statistics are useful when predicates involve correlated columns that
single-column summaries cannot represent.

## 4. Design Trade-Offs

| Choice | Benefit | Cost |
|---|---|---|
| Shared buffer pool | Reuse pages across sessions; coordinated dirty writes | Shared-memory contention and tuning |
| Heap separate from indexes | Multiple index types; stable heap model | Index lookup often needs a second heap access |
| Append new tuple versions | Readers and writers overlap | Dead tuples and VACUUM |
| WAL before data | Fast commits and crash recovery | WAL bandwidth and checkpoint management |
| Cost-based planning | Adapts to data size and distribution | Depends on estimates and configuration |
| B-tree page splits | Keeps ordered logarithmic search | Write amplification, WAL, and temporary fragmentation |

PostgreSQL repeatedly chooses generality and concurrency over the simplest
single-workload representation. That is appropriate for a shared database, but
it means administrators must watch vacuum progress, statistics quality, memory,
and checkpoint behavior.

## 5. Experiments / Observations

### Setup

An isolated PostgreSQL 17.9 instance was initialized with trust authentication
on a temporary local port. It reported:

| Setting | Observed value |
|---|---:|
| Block size | 8192 bytes |
| Shared buffers | 128 MB |
| WAL level | `replica` |

The dataset contained 10,000 customers, 100,000 orders, and 100,000 payments.
Foreign-key columns had B-tree indexes, followed by `ANALYZE`.

```sql
EXPLAIN (ANALYZE, BUFFERS)
SELECT c.region, COUNT(*), SUM(o.amount)
FROM customers c
JOIN orders o   ON o.customer_id = c.id
JOIN payments p ON p.order_id = o.id
WHERE o.status = 'paid' AND p.method = 'card'
GROUP BY c.region
ORDER BY SUM(o.amount) DESC;
```

### Plan summary

```text
Sort
  -> HashAggregate by region
       -> Hash Join (orders.customer_id = customers.id)
            -> Hash Join (payments.order_id = orders.id)
                 -> Seq Scan payments, filter method='card'
                 -> Seq Scan orders, filter status='paid'
            -> Seq Scan customers
```

| Metric | Observed value |
|---|---:|
| Estimated rows after joins | 8,420 |
| Actual rows after joins | 8,333 |
| Shared-buffer hits | 1,236 |
| Shared-buffer reads | 0 |
| Planning time | 6.651 ms |
| Execution time | 35.455 ms |
| Final groups | 5 |

The indexes existed, but sequential scans and hash joins were cheaper because
the filters retained 25,000 orders and 33,333 payments. Thousands of random
index/heap visits would cost more than scanning each compact table once and
building in-memory hashes.

The estimate was close to reality (about 1% high). `pg_stats` reported two
distinct values for both `status` and `method`, and 20 regions. The distinct
counts establish the domains, but the planner's selectivity for the uneven
25,000/100,000 and 33,333/100,000 distributions comes from the corresponding
most-common-value frequencies. These inputs can be inspected directly:

```sql
SELECT tablename, attname, n_distinct,
       most_common_vals, most_common_freqs
FROM pg_stats
WHERE tablename IN ('orders', 'payments', 'customers')
  AND attname IN ('status', 'method', 'region');
```

All buffers were hits, so this was a warm-cache execution; a cold run would
include physical reads and should not be expected to match 35.455 ms.

This experiment shows why an index is not automatically used merely because it
matches a predicate. The planner chooses the cheapest complete plan using table
size, selectivity, statistics, and configured cost assumptions. The output
fields are explained in PostgreSQL's [EXPLAIN documentation](https://www.postgresql.org/docs/current/using-explain.html).

## 6. Key Learnings

- Shared buffers connect query execution to page caching and controlled writes.
- MVCC moves contention out of the read path but creates cleanup work.
- VACUUM is part of the storage design, not optional housekeeping.
- WAL makes commit durability independent from immediate data-page writes.
- A correct index can still be the wrong access path when many rows qualify.
- Planner estimates become understandable when compared with `pg_stats` and
  actual row counts instead of treating EXPLAIN as a black box.

## Sources Consulted

- [PostgreSQL server architecture](https://www.postgresql.org/docs/current/tutorial-arch.html)
- [Database page layout](https://www.postgresql.org/docs/current/storage-page-layout.html)
- [MVCC and concurrency control](https://www.postgresql.org/docs/current/mvcc.html)
- [WAL reliability](https://www.postgresql.org/docs/current/wal-intro.html)
- [Using EXPLAIN](https://www.postgresql.org/docs/current/using-explain.html)
- [Planner statistics](https://www.postgresql.org/docs/current/planner-stats.html)
