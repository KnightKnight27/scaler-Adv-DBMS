# DB Comparison Report — PostgreSQL · SQLite · DuckDB

Same schema, same data, same five queries against three databases designed
around three different points in the design space.

> The timings, plan snippets, and storage sizes in this report are
> **representative** values, not measurements from this machine. Every
> number-bearing table is marked `(illustrative)`.

## 1. The three engines

| Engine | Design point | Storage layout | Concurrency |
|---|---|---|---|
| **PostgreSQL 18** | Multi-tenant transactional server | Row store (heap + B-tree indexes) | Process-per-connection, MVCC, parallel queries |
| **SQLite 3.51** | Single-process embedded library | Row store (B-tree pages in one file) | Many readers + one writer (WAL), no parallelism |
| **DuckDB 1.5** | Single-process embedded analytics | Column store (compressed blocks + zone maps) | Vectorized, multi-threaded scans, MVCC |

## 2. Schema and data

```sql
CREATE TABLE users (
    user_id      BIGINT PRIMARY KEY,
    country      TEXT NOT NULL,        -- 20 countries, weighted
    signup_date  DATE NOT NULL,
    is_premium   BOOLEAN NOT NULL      -- ~10%, biased toward older accounts
);

CREATE TABLE orders (
    order_id    BIGINT PRIMARY KEY,
    user_id     BIGINT NOT NULL REFERENCES users(user_id),
    order_date  DATE NOT NULL,
    amount      NUMERIC(10,2) NOT NULL,
    status      TEXT NOT NULL          -- completed 78%, pending 15%, cancelled 7%
);

-- After bulk load, on PG and SQLite only (DuckDB scans columns directly):
CREATE INDEX idx_orders_user_id    ON orders (user_id);
CREATE INDEX idx_orders_order_date ON orders (order_date);
CREATE INDEX idx_orders_status     ON orders (status);
CREATE INDEX idx_users_country     ON users  (country);
```

Volume: `users` = **100,000 rows**, `orders` = **1,000,000 rows**, generated
with a fixed seed so all three engines load byte-identical CSVs. Order
amounts are log-normal; users-to-orders follows a power law (a few users
order a lot, most order a little).

## 3. Methodology

- Load CSVs once, build indexes after the load, `ANALYZE` so each planner
  has fresh statistics.
- Per query: 1 warmup run discarded, then 5 measured runs. Report mean.
- Caches warm — cold-cache numbers mostly measure your SSD, not the DB.
- One connection per engine reused across runs, so connection setup cost
  doesn't dominate Q3's timing.

## 4. Storage footprint *(illustrative)*

| | PostgreSQL | SQLite | DuckDB |
|---|---|---|---|
| Whole DB on disk | ≈ 120 MB | ≈ 90 MB | ≈ 48 MB |
| `orders` table + indexes | ≈ 62 MB heap + 35 MB indexes | ≈ 78 MB | ≈ 40 MB columnar+compressed |
| Page / block size | 8 KiB | 4 KiB | 256 KiB |
| Compression | TOAST (long values only) | None | LZ4 / FSST per column block |
| Files on disk | many under `$PGDATA` | one `.db` file | one `.db` file |

DuckDB compresses each column independently, so `status` (3 distinct values)
and `country` (20 distinct values) collapse hard. PostgreSQL pays for its
per-row MVCC visibility metadata, free-space maps, and visibility maps —
the cost of multi-writer concurrency.

## 5. The five queries

| | Query | What it stresses |
|---|---|---|
| **Q1** | Top 10 countries by revenue from completed orders | Big hash join + agg + sort/limit |
| **Q2** | Premium users' spend in the last 90 days, ≥3 orders | Selective filter + join + group |
| **Q3** | All orders for `user_id = 42` | Indexed point lookup (OLTP shape) |
| **Q4** | Monthly revenue trend, completed orders | Full scan + date-truncated group |
| **Q5** | First order amount per user + days-to-next-order | Window functions over 100k partitions |

## 6. Per-query timings *(illustrative)*

5 warm runs, mean wall time:

| Query | PostgreSQL | SQLite | DuckDB | Winner |
|---|---:|---:|---:|---|
| **Q1** Top-10 countries | **55 ms** | 210 ms | 65 ms | PG (parallel hash agg) |
| **Q2** Premium last-90d | **85 ms** | 185 ms | 95 ms | PG (selective + indexed) |
| **Q3** Point lookup user=42 | 0.3 ms | **0.2 ms** | 240 ms | SQLite (B-tree dive, no startup) |
| **Q4** Monthly revenue | 95 ms | 230 ms | **45 ms** | DuckDB (canonical columnar win) |
| **Q5** Window per user | 320 ms | 1,100 ms | **180 ms** | DuckDB (vectorized partition scan) |

Three different engines win three different categories — and that is the
point of the comparison.

## 7. Why the timings look that way

**Q1 / Q2 (PostgreSQL wins).** PG runs a parallel hash aggregate with two
workers per gather by default. Both children partial-aggregate, then merge.
SQLite is single-threaded by design; DuckDB's vectorized pipeline is great
but at this row count the extra coordination doesn't outrun PG's parallel
plan. Q2 is selective enough (premium ∩ last-90-days) that PG's index lookup
wins outright.

**Q3 (SQLite wins).** This is the OLTP shape. PG and SQLite both walk
`idx_orders_user_id` and pull ~10 rows — sub-millisecond. SQLite edges PG
because there's no process/connection overhead. DuckDB does not maintain
secondary B-trees on regular columns by design, so it scans all 1 M rows
applying a filter. 240 ms isn't DuckDB being slow — it's DuckDB being asked
the wrong question.

**Q4 (DuckDB wins).** Canonical columnar workload. DuckDB reads only the
three columns it needs (`order_date`, `amount`, `status`) instead of every
row's every column, and zone maps over its 256 KiB column blocks let it skip
entire blocks whose `status` range doesn't include `'completed'`. PG and
SQLite have to walk the heap row-by-row.

**Q5 (DuckDB wins).** Window functions sort by `(user_id, order_date)`
once. DuckDB vectorizes the partition boundaries; PG does a single-threaded
`WindowAgg` over a `Sort` (and spills to temp files if `work_mem` is too
small — bump it if you see *external merge Disk* in `EXPLAIN ANALYZE`).
SQLite added window functions in 3.25 and they work, but they aren't a
strong suit of its optimizer.

## 8. Honest tradeoffs

- **PostgreSQL** is the slowest to set up: it's a server process with
  users, config, and a `psql` connection. Every query pays connection
  overhead. The point of paying that cost is concurrent writers, MVCC,
  rich SQL, and a mature ecosystem.
- **SQLite**'s "many readers, one writer" rule is enforced at the file
  level — two writers serialize on the database file's WAL lock. There is
  no horizontal scaling story; that's a feature for embedded use, a
  showstopper for a multi-user service.
- **DuckDB** is single-process. Two CLI sessions can't both read the same
  `.db` with one of them writing — it errors out on a file lock. Fine for
  analytics (one human, one job at a time); wrong for a multi-user OLAP
  service.

## 9. When to pick what

- **App with multiple processes reading and writing, data must not lie.** →
  **PostgreSQL.**
- **Embedded / mobile / desktop, one process touches the data, no server
  wanted.** → **SQLite.**
- **You have CSVs / Parquet and want to run analytical queries from a
  Python notebook.** → **DuckDB.**
- **Mixed transactional + analytical traffic in one service.** → PostgreSQL
  for the writes, optionally replicated into DuckDB for the analytics.
