# PostgreSQL Internal Architecture

## 1. Problem Background

PostgreSQL is a production-grade relational database server designed for correctness, extensibility, concurrency, and durability. Its internal architecture reflects the needs of a shared multi-user system: many sessions must read and write the same data at the same time, failed transactions must not corrupt durable state, and the optimizer must choose efficient execution plans from incomplete information.

The central design problem PostgreSQL solves is coordination. A database server must coordinate memory, locks, transactions, indexes, write-ahead logging, and recovery across many client connections. PostgreSQL accepts the complexity of a client-server architecture because this gives it a single place to enforce transaction isolation, cache pages, recover after crashes, maintain catalogs, and collect planner statistics.

## 2. Architecture Overview

```text
Client
  |
  v
Backend process
  |
  +--> Parser and rewriter
  +--> Planner / optimizer
  +--> Executor
  |
  +--> Buffer manager ---- shared buffers ---- data and index pages
  +--> Lock manager ------ lock table
  +--> Transaction manager - snapshots, XIDs, visibility
  +--> WAL manager -------- WAL buffers and pg_wal

Background processes:
  checkpointer, background writer, WAL writer, autovacuum

Disk:
  heap relation files, index files, pg_wal, catalogs
```

For a read query, PostgreSQL parses SQL, builds a plan, chooses scan and join strategies, pins needed buffers, checks tuple visibility, and returns rows. For a write query, PostgreSQL also creates new tuple versions, updates indexes when needed, writes WAL records, marks buffers dirty, and later flushes dirty pages through background work.

## 3. Internal Design

### Buffer Manager

PostgreSQL stores tables and indexes as fixed-size pages, commonly 8 KB. The buffer manager is responsible for bringing those pages from disk into shared buffers and deciding which buffers can be reused. A backend does not normally read a heap page directly from the file system and then discard it privately. It pins a shared buffer page, works with that page, and releases the pin when finished.

The important design decision is that cache state is shared across sessions. If one query reads a table page, a later query may reuse that page from shared buffers instead of going to disk. This is why repeated queries can become faster after the first run.

Buffer replacement is based on usage information rather than a perfect LRU list. PostgreSQL's clock-sweep style approach is cheaper to maintain under concurrency than constantly moving buffers through an exact recency list.

### B-Tree Index Implementation

PostgreSQL's default index type is B-tree. A B-tree index stores ordered keys and tuple references. Searches descend from the root page through internal pages to leaf pages. Leaf entries point back to heap tuples. This separation lets the heap remain a general-purpose storage structure while indexes provide alternate access paths.

The trade-off is that MVCC can create multiple physical tuple versions for one logical row. A B-tree index entry points to a physical tuple version, not to a logical row identity that magically changes in place. Therefore updates can create new index entries, and old entries may remain until cleanup.

Page splits happen when an index page cannot fit a new key. PostgreSQL creates space by splitting the page and updating parent links. This keeps search logarithmic, but random inserts can cause extra page writes and fragmentation.

### MVCC

PostgreSQL implements multi-version concurrency control by storing transaction metadata in heap tuples. Tuple headers include fields such as `xmin` and `xmax`, which identify the transaction that created a tuple and the transaction that deleted or replaced it. A snapshot determines which transaction IDs are visible to a query.

This design allows readers and writers to overlap. A reader can keep seeing the old visible tuple version while a writer creates a newer version. The cost is dead tuples. Old row versions cannot be removed immediately because an active snapshot might still need them.

VACUUM is necessary because MVCC creates garbage as a normal part of updates and deletes. VACUUM identifies tuples no active transaction can still see and makes their space reusable. ANALYZE collects statistics used by the planner.

### WAL And Recovery

PostgreSQL uses write-ahead logging. The rule is simple: before a dirty data page is written in a way that depends on a change, the corresponding WAL record must already be durable. This makes crash recovery possible. After restart, PostgreSQL can replay WAL records from the last checkpoint to restore committed changes.

Checkpoints reduce recovery time by ensuring that dirty pages up to a certain WAL position have been written to disk. The trade-off is write pressure: aggressive checkpointing reduces crash recovery time but can increase I/O during normal operation.

### Query Planner And Statistics

PostgreSQL's planner estimates row counts, selectivity, join costs, index scan costs, and sort/hash costs. It relies on catalog statistics, including information collected by ANALYZE. A plan is only as good as its estimates. Stale statistics can lead to sequential scans when index scans would be better, or bad join orders when table size estimates are wrong.

`EXPLAIN ANALYZE` is useful because it shows both the estimated plan and actual execution behavior. The comparison between estimated rows and actual rows is often the fastest way to find planner-statistics problems.

## 4. Design Trade-Offs

| Design choice | Benefit | Cost |
| --- | --- | --- |
| Shared buffer pool | Reuses pages across sessions | Requires concurrency control around buffers |
| MVCC tuple versions | Readers and writers overlap | Dead tuples require VACUUM |
| Separate heap and indexes | Flexible index types and heap layout | Indexes may point to dead tuple versions |
| WAL before data writes | Strong crash recovery | Extra sequential write path |
| Cost-based optimizer | Adapts to data distribution | Depends on accurate statistics |
| Background workers | Moves maintenance out of foreground queries | More server processes and configuration |

PostgreSQL chooses correctness and concurrency over simplicity. The system has more moving parts than an embedded database, but the reward is strong behavior under many clients, large datasets, and mixed workloads.

## 5. Experiments / Observations

### EXPLAIN ANALYZE Exercise

Example schema:

```sql
CREATE TABLE users (
  id integer PRIMARY KEY,
  name text
);

CREATE TABLE orders (
  id integer PRIMARY KEY,
  user_id integer REFERENCES users(id),
  amount integer
);

ANALYZE;

EXPLAIN ANALYZE
SELECT u.name, o.amount
FROM users u
JOIN orders o ON u.id = o.user_id
WHERE o.amount > 100;
```

What to analyze:

- Whether the planner chooses a sequential scan or index scan.
- Estimated rows vs actual rows.
- Join order.
- Join type, such as nested loop, hash join, or merge join.
- Whether statistics were accurate after `ANALYZE`.

### Practical Observation

In the local PostgreSQL exploration, `SELECT * FROM users;` was faster on the second run than the first run. This is expected because pages can be served from PostgreSQL shared buffers or the operating system page cache. The same experiment also showed that `relpages` can appear stale until `ANALYZE` refreshes statistics.

## 6. Key Learnings

- PostgreSQL is built around coordinated shared state: buffers, locks, WAL, snapshots, and statistics.
- MVCC improves concurrency but turns cleanup into a first-class maintenance problem.
- WAL separates transaction durability from immediate data-page flushing.
- B-tree indexes are efficient but must cooperate with MVCC cleanup.
- Query performance depends heavily on statistics; `EXPLAIN ANALYZE` is essential for understanding planner decisions.
- PostgreSQL's complexity is the price paid for strong multi-user behavior.
