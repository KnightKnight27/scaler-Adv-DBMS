# PostgreSQL Internal Architecture

## 1. Problem Background

A database isn't a single mechanism — it's several subsystems cooperating to solve different problems at once:

- **Buffer Manager** exists because disk I/O is, by a wide margin, the slowest operation a database performs. PostgreSQL keeps a region of shared memory (`shared_buffers`) as a page cache so repeated reads of the same data don't hit disk.
- **B-Tree (`nbtree`)** is PostgreSQL's default, general-purpose index structure because a balanced tree gives logarithmic-time search and insert *and* supports ordered range scans — something a hash index can't do.
- **MVCC** exists because PostgreSQL was designed (going back to its POSTGRES research-project roots) to let many transactions read and write concurrently without readers blocking writers or vice versa. The chosen mechanism is tuple versioning rather than lock-based concurrency.
- **WAL (Write-Ahead Logging)** exists to make durability cheap: instead of forcing every dirty data page to disk on every commit, PostgreSQL only has to fsync a small sequential log record. The data pages themselves can be flushed later, asynchronously, by the Checkpointer.

This investigation sets up a 3-table schema (`customers`, `orders`, `payments`) with 1,000 / 10,000 / 10,000 rows respectively, and uses it to observe these four subsystems directly through `EXPLAIN ANALYZE`, the statistics catalogs, and PostgreSQL's introspection views.

---

## 2. Architecture Overview

```text
Client (psql)
    │
    ▼
Backend Process (one per connection)
    │
    ▼
Planner ──uses──► pg_statistic / pg_stats (table & column statistics)
    │
    ▼
Executor
    │
    ├──► Buffer Manager ──► shared_buffers (page cache) ──► Disk (only on cache miss)
    │
    ├──► B-Tree (nbtree) ──► used only if a matching index exists
    │
    └──► MVCC visibility check ──► xmin / xmax on each heap tuple
    │
    ▼
WAL ──► every change logged before the transaction commits
    │
    ▼
Checkpointer / Background Writer ──► flushes dirty buffers to disk asynchronously
```

A query enters through a backend process, gets a plan built using catalog statistics, executes by pulling pages through the buffer manager (and through an index if one exists and helps), checks row visibility via MVCC, and any change is durably logged to WAL before the client gets a commit acknowledgment.

---

## 3. Internal Design

### 3.1 Buffer Manager

Every `EXPLAIN (..., BUFFERS)` line in the experiments below reports `Buffers: shared hit=N` with **no `read=` figure at all**. In PostgreSQL's `BUFFERS` output, `hit` means the page was already resident in `shared_buffers`; `read` would mean it had to be pulled from disk. Across every query run, **zero disk reads occurred** — the entire 1,000+10,000+10,000-row dataset comfortably fits in the buffer cache, so the executor never left memory. This is the buffer manager doing its job: avoid disk I/O whenever a page is already cached.

> This is real evidence the buffer manager is active, but it doesn't show cache *misses* or replacement — that only shows up on a dataset bigger than `shared_buffers`. See "Recommended Additional Experiments" below.

### 3.2 B-Tree Implementation

No secondary index was created on `orders.customer_id` in this experiment — and PostgreSQL does **not** automatically index a foreign-key column (only the referenced primary key gets one). The result was a Seq Scan even on a selective filter (`WHERE customer_id = 100`, 18 matching rows out of 10,000):

```text
Seq Scan on orders  (cost=0.00..180.00 rows=18 width=14) (actual time=0.105..1.315 rows=18 loops=1)
   Filter: (customer_id = 100)
   Rows Removed by Filter: 9982
   Buffers: shared hit=55
```

This is correct PostgreSQL behavior, and it's a useful negative result: without a B-tree index, the planner has no cheaper option than scanning all 55 pages of the table and discarding 9,982 non-matching rows, even though only 18 rows were needed. A B-tree index on `customer_id` would let the planner walk from the tree's root page down to the matching leaf page(s) directly, touching only a handful of pages instead of all 55.

> No index existed in this run, so no page-level B-tree inspection was done. See "Recommended Additional Experiments" below for the commands to fill this in.

### 3.3 MVCC

`pg_stat_user_tables` showed `n_dead_tup = 0` for all three tables:

```text
  relname  | n_live_tup | n_dead_tup | last_vacuum |         last_autovacuum
-----------+------------+------------+-------------+----------------------------------
 orders    |      10000 |          0 |             | 2026-06-21 22:00:39.12131+05:30
 customers |       1000 |          0 |             |
 payments  |      10000 |          0 |             | 2026-06-21 22:00:39.122244+05:30
```

This is consistent with MVCC's design: dead tuples (old row versions PostgreSQL keeps around for snapshot consistency) only accumulate after `UPDATE` or `DELETE` activity — a pure bulk-`INSERT` workload like this one produces none. `orders` and `payments` already show a `last_autovacuum` timestamp (autovacuum reacted to the large bulk insert to update statistics/cleanup), while `customers` — being smaller — hadn't crossed autovacuum's threshold yet at the time of the query.

> Tuple-level versioning (`xmin`/`xmax`, `ctid` changing across an `UPDATE`) wasn't demonstrated directly in this run. See "Recommended Additional Experiments" below.

### 3.4 WAL

Not exercised in this run — no `pg_current_wal_lsn()`, checkpoint, or `pg_stat_bgwriter` queries were captured. See "Recommended Additional Experiments" below for the exact commands needed to fill this in before final submission, since WAL is explicitly part of Topic 2's required Study Focus.

### 3.5 Query Planning & Statistics

`pg_stats` was queried directly for all three tables:

```text
 schemaname | tablename |   attname    | n_distinct
------------+-----------+--------------+------------
 public     | customers | customer_id  |         -1
 public     | customers | name         |         -1
 public     | orders    | order_id     |         -1
 public     | orders    | customer_id  |       1000
 public     | orders    | amount       |    -0.9523
 public     | payments  | payment_id   |         -1
 public     | payments  | order_id     |    -0.6359
 public     | payments  | payment_date |          1
```

`pg_stats` is a readable view built directly on top of the raw `pg_statistic` system catalog (it joins in column/table names; `pg_statistic` itself stores the same data keyed by `starelid`/`staattnum`, with arrays like `stavalues1`/`stanumbers1` instead of named columns). The planner reads `pg_statistic` directly when costing a plan — `pg_stats` exists purely for humans to inspect the same numbers.

Reading the values:
- `order_id`, `payment_id`, `customer_id` (PK columns) → `-1`, meaning "essentially all values are unique."
- `orders.customer_id` (FK) → `1000`, an exact count, because there are precisely 1,000 distinct customer IDs being referenced.
- `orders.amount` → `-0.9523`, i.e. ~95% of values are distinct — expected for random two-decimal numerics with a small chance of collision.
- `payments.order_id` → `-0.6359`, i.e. ~63.6% distinct. This is sampling 10,000 values *with replacement* from a domain of 10,000 possible order IDs — the expected fraction of values hit at least once converges to `1 − 1/e ≈ 63.2%`, which matches almost exactly.
- `payments.payment_date` → `1`, a single distinct value — because `CURRENT_TIMESTAMP` is fixed for the duration of a transaction, and all 10,000 rows were inserted by one `INSERT...SELECT` statement.

These statistics are exactly what the planner used to cost the multi-table join below.

---

## 4. Design Trade-Offs

| Subsystem | What it buys you | What it costs |
|---|---|---|
| **Buffer Manager** | Avoids disk I/O for hot pages (every query in this experiment ran with `hit`, zero `read`) | A fixed chunk of shared memory (`shared_buffers`) must be sized and tuned; too small and hit rate collapses, too large and it starves other memory uses |
| **B-Tree Index** | Turns an O(n) scan into an O(log n) lookup — would have replaced the 9,982-row discard in the Seq Scan experiment with a direct tree descent | Extra storage, and every `INSERT`/`UPDATE`/`DELETE` must also maintain the index (write amplification) — this experiment paid zero index-maintenance cost only *because* no index existed |
| **MVCC** | Readers never block writers, writers to different rows never block each other | Old row versions accumulate as dead tuples and must be reclaimed by `VACUUM`/autovacuum — not yet visible here since the workload was insert-only |
| **WAL** | Commit only requires an inexpensive sequential log write, not a full data-page flush; enables crash recovery | Every change is written twice over time — once to WAL, once eventually to the data file via checkpoint |

The clearest concrete trade-off observed directly in this experiment is the **missing index on `orders.customer_id`**: zero index-maintenance overhead during the 10,000-row bulk insert, paid for by scanning all 55 pages and discarding 9,982 rows on every subsequent lookup by `customer_id`. At this scale the cost is invisible (1.3 ms, everything cached); at production scale with millions of rows and disk-resident data, that same missing index would be a serious performance problem.

---

## 5. Experiments / Observations

### Setup
```sql
CREATE TABLE customers (customer_id SERIAL PRIMARY KEY, name VARCHAR(100));
CREATE TABLE orders (order_id SERIAL PRIMARY KEY, customer_id INT REFERENCES customers(customer_id), amount NUMERIC(10,2));
CREATE TABLE payments (payment_id SERIAL PRIMARY KEY, order_id INT REFERENCES orders(order_id), payment_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP);

-- 1,000 customers, 10,000 orders, 10,000 payments inserted via generate_series + random()
ANALYZE;
```

### Recommended Exercise: multi-table join

```sql
EXPLAIN (ANALYZE, BUFFERS, VERBOSE)
SELECT c.customer_id, c.name, COUNT(o.order_id) AS total_orders, SUM(o.amount) AS total_amount
FROM customers c
JOIN orders o ON c.customer_id = o.customer_id
JOIN payments p ON o.order_id = p.order_id
GROUP BY c.customer_id, c.name
HAVING COUNT(o.order_id) > 5
ORDER BY total_amount DESC
LIMIT 20;
```

Resulting plan (abridged):

```text
Limit (actual rows=20 loops=1)            Buffers: shared hit=117
  -> Sort (Sort Method: top-N heapsort, Memory: 27kB)   est. rows=333, actual rows=20
       -> HashAggregate  est. rows=333, actual rows=842   Filter: count(o.order_id) > 5
            -> Hash Join (o.customer_id = c.customer_id)  rows=10000
                 -> Hash Join (p.order_id = o.order_id)   rows=10000
                      -> Seq Scan on payments p   Buffers: shared hit=55
                      -> Hash on orders o          Buffers: shared hit=55
                 -> Hash on customers c             Buffers: shared hit=7
Planning Time: 0.662 ms
Execution Time: 17.316 ms
```

**Analysis of the plan:**
- The planner correctly chose **Hash Join** for both joins — appropriate here since there's no index to drive a Nested Loop or Merge Join, and both join keys are high-cardinality equi-joins over the full table.
- **Sort used a top-N heapsort**, not a full sort, because `LIMIT 20` lets PostgreSQL maintain only a 20-row heap instead of sorting all qualifying groups — this is why the Sort step's actual row count is 20 even though 842 groups existed beneath it.
- The **planner's row estimate diverges sharply from reality** at the `HAVING` filter: it estimated 333 surviving groups but actually got 842. Estimating selectivity for a filter applied *after* aggregation (`COUNT(...) > 5`) is inherently harder than estimating a plain column filter, since there's no direct histogram for "count of joined rows per group" — this is a good concrete example of why planner estimates and actual execution stats can disagree even with fresh statistics.
- **Zero disk reads anywhere** (`Buffers: shared hit=117`, no `read=`) — the whole working set, including the 22 buffer hits needed just for planning, was already cache-resident.

### Index-less lookup

```sql
EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM orders WHERE customer_id = 100;
```
```text
Seq Scan on orders  (actual time=0.105..1.315 rows=18 loops=1)
   Filter: (customer_id = 100)
   Rows Removed by Filter: 9982
   Buffers: shared hit=55
Execution Time: 1.384 ms
```
Confirms there is no index on `orders.customer_id` — PostgreSQL had to inspect and discard 9,982 of 10,000 rows to find the 18 matches.

### Recommended Additional Experiments (to complete Topic 2's required coverage)

These weren't captured in this run but are needed to fully cover Buffer Manager, B-Tree, MVCC, and WAL per the assignment's Study Focus. Run these and paste the output into the corresponding subsection above before submitting:

**B-Tree — create the missing index and inspect it:**
```sql
CREATE INDEX idx_orders_customer_id ON orders(customer_id);
EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM orders WHERE customer_id = 100;  -- compare to the Seq Scan above

CREATE EXTENSION IF NOT EXISTS pageinspect;
SELECT * FROM bt_metap('idx_orders_customer_id');             -- note the 'root' block number
SELECT * FROM bt_page_stats('idx_orders_customer_id', <root>); -- replace <root> with that number
```

**MVCC — show tuple versioning directly:**
```sql
SELECT ctid, xmin, xmax, * FROM orders WHERE order_id = 1;
UPDATE orders SET amount = amount + 1 WHERE order_id = 1;
SELECT ctid, xmin, xmax, * FROM orders WHERE order_id = 1;  -- ctid and xmin will differ
SELECT relname, n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname = 'orders';  -- n_dead_tup now > 0
```

**WAL — observe durability machinery:**
```sql
SHOW wal_level;
SELECT pg_current_wal_lsn();
CHECKPOINT;
SELECT pg_current_wal_lsn();  -- compare LSN before/after
SELECT * FROM pg_stat_bgwriter;
```

---

## 6. Key Learnings

1. **Foreign keys don't get automatic indexes in PostgreSQL** — only primary keys do. `orders.customer_id` had no index, so even a highly selective filter fell back to a full Seq Scan.
2. **Planner estimates can diverge significantly after aggregation.** The `HAVING COUNT(o.order_id) > 5` filter was estimated at 333 surviving groups but actually produced 842 — selectivity on post-aggregate conditions is fundamentally harder to predict than a plain column filter.
3. **`CURRENT_TIMESTAMP` is per-transaction, not per-row** — a classic gotcha when generating "realistic" timestamp test data with a single bulk `INSERT...SELECT`.
4. **`Buffers: shared hit=N` with no `read=`** is a clean, direct signal that a dataset fits entirely inside `shared_buffers` — useful for confirming when a benchmark is actually testing CPU/algorithm cost rather than disk I/O.
5. **MVCC's storage cost is workload-dependent**, not constant — a pure-insert workload (this experiment) produces zero dead tuples; the cost only appears once `UPDATE`/`DELETE` activity begins.
6. **`payments.order_id` landing at ~63.6% distinct values** is a nice real-world confirmation of the `1 − 1/e ≈ 63.2%` rule for sampling-with-replacement — distinct-value statistics aren't just abstract numbers, they reflect the actual data-generation process.
7. **This run is strong on query planning/statistics but incomplete on B-Tree internals, tuple-level MVCC, and WAL** — flagged explicitly above with the exact commands needed to close those gaps before submission.