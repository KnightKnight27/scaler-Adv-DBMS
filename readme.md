# DB Comparison Report: PostgreSQL vs SQLite vs DuckDB

This report compares three databases on the same dataset and query set.
Numbers below come from a real run of `compare.sh` on this machine
(macOS, Apple Silicon, PostgreSQL 18.3, SQLite 3.51.0, DuckDB v1.5.3).
Full raw output is in `results.txt`.

## Schema

- `users(user_id, country, signup_date, is_premium)`
- `orders(order_id, user_id, order_date, amount, status)`
- Indexes on `orders(user_id)`, `orders(order_date)`, `orders(status)`, `users(country)`

## Data volume

- `users`: 100,000 rows
- `orders`: 1,000,000 rows

## Queries

- **Q1 (analytical join + group-by):** top 10 countries by total order amount, premium users only.
- **Q2 (filter + group-by on a date window):** count of orders per status since `2025-01-01`.
- **Q3 (selective point lookup):** all orders for a single `user_id`.

Each query was run 3 times under the engine's built-in timer; the table
below reports the average. Timings are the engine-reported execution
time (`Run Time` for SQLite/DuckDB, `Time` for psql), so CLI
startup is excluded.

## How to reproduce

```bash
./compare.sh                               # full run (100k users / 1M orders)
USERS=10000 ORDERS=100000 ./compare.sh     # smaller run
```

## Comparison Table

| Aspect | PostgreSQL | SQLite | DuckDB |
|---|---|---|---|
| Model | Server process | Embedded library | Embedded analytical engine |
| Storage | Many relation files in a server data directory | Single `.db` file | Single `.db` file with columnar layout |
| Execution style | Cost-based, parallel, mixed workload | B-tree, lightweight, local-first | Vectorized, hash-based, analytics-first |
| Memory/tuning | `shared_buffers`, `work_mem`, OS cache | `page_size`, `mmap_size`, `cache_size` | `memory_limit`, `threads` |
| DB size observed | `115 MB` | `86 MB` | `46 MB` |
| Key internal numbers | `orders = 57 MB`, `users = 4352 kB`, `block_size = 8192`, `shared_buffers = 128MB`, `effective_cache_size = 4GB` | `page_size = 4096`, `page_count = 22086`, `mmap_size = 0` | `block_size = 262144`, `total_blocks = 185`, `database_size = 46.2 MiB` |
| Q1 avg time | `0.044 s` (44 ms) | `0.382 s` | `0.006 s` (6 ms) |
| Q2 avg time | `0.029 s` (29 ms) | `0.263 s` | `0.002 s` (2 ms) |
| Q3 avg time | `0.0001 s` (0.11 ms) | `< 0.001 s` | `< 0.001 s` |
| Best fit | Multi-user transactional and mixed workloads | Simple local apps and zero-ops storage | Embedded analytics and scan-heavy queries |

## Experiment Observations

- **DuckDB won the analytical queries (Q1 and Q2) by a large margin.**
  Its columnar storage plus vectorized execution scans the 1M-row
  `orders` table and the join in ~6 ms for Q1 and ~2 ms for Q2 —
  ~7× faster than PostgreSQL on Q1 and ~15× faster on Q2 in this run.
  The reference report's slower DuckDB numbers were dominated by CLI
  process startup; using the engine's `.timer` measurement isolates
  execution and the picture flips.
- **PostgreSQL was the all-rounder.** Q1 and Q2 land at 44 ms and 29 ms,
  which is comfortable for a row store with B-tree indexes. Q2's
  first run (53 ms) was noticeably slower than the next two (17 ms),
  consistent with cache warm-up. Q3 was the fastest point lookup at
  ~0.1 ms — index descent on a server already in memory.
- **SQLite was the slowest on the heavy queries** by an order of
  magnitude: 0.38 s on Q1 and 0.26 s on Q2. The query planner is
  simpler (no parallel hash joins) and execution is row-at-a-time, so
  scan-heavy workloads expose that gap. Q3, however, was effectively
  instantaneous — selective B-tree lookups are what SQLite is built
  for.
- **Storage footprint** ranked DuckDB (46 MB) < SQLite (86 MB) <
  PostgreSQL (115 MB). DuckDB's columnar layout compresses the
  repetitive `country` and `status` columns extremely well.
  PostgreSQL's per-row layout plus MVCC visibility metadata and
  separate index files makes it the largest.
- **Index influence:** the indexes on `orders(user_id)` were what made
  Q3 cheap everywhere. Without them, all three would have had to scan
  1M rows. Q2's date filter used `orders(order_date)`; the speedup
  between the cold and warm runs in PostgreSQL (53 → 17 ms) is the
  cache filling, not a plan change.

## Files

- `gen_data.py` — generates `users.csv` and `orders.csv` with a fixed seed.
- `compare.sh` — drives the full experiment across all three engines.
- `results.txt` — raw output from the last run.
- `readme.md` — this report.
