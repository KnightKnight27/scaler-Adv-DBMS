# DB Comparison Report: PostgreSQL vs SQLite vs DuckDB

This report compares three databases on the same dataset and query set.

Schema:

- `users(user_id, country, signup_date, is_premium)`
- `orders(order_id, user_id, order_date, amount, status)`
- Indexes on `orders(user_id)`, `orders(order_date)`, `orders(status)`, `users(country)`

Data volume:

- `users`: 100,000 rows
- `orders`: 1,000,000 rows

## Comparison Table

| Aspect | PostgreSQL | SQLite | DuckDB |
|---|---|---|---|
| Model | Server process | Embedded library | Embedded analytical engine |
| Storage | Many relation files in a server data directory | Usually one main `.db` file | One `.db` file with columnar layout |
| File layout | Multiple internal files | Single primary file | Single primary file |
| Execution style | Cost-based, parallel, mixed workload | B-tree, lightweight, local-first | Vectorized, hash-based, analytics-first |
| Memory/tuning | `shared_buffers`, `work_mem`, OS cache | `page_size`, `mmap_size`, `cache_size` | `memory_limit`, `threads` |
| DB size observed | `122 MB` | `90M` | `48M` |
| Key internal numbers | `orders = 60 MB`, `users = 4352 kB` | `page_size = 4096`, `page_count = 22888`, `mmap_size = 0` baseline | `block_size = 262144`, `database_size = 47.2 MiB` |
| Q1 avg time | `0.057 s` | `0.208 s` | `0.263 s` |
| Q2 avg time | `0.082 s` | `0.183 s` | `0.265 s` |
| Q3 avg time | `0.027 s` | `0.003 s` | `0.240 s` |
| Best fit | Multi-user transactional and mixed workloads | Simple local apps and zero-ops storage | Embedded analytics and scan-heavy queries |

## Experiment Observations

- PostgreSQL gave the lowest average time on Q1 and Q2 in this run.
- SQLite gave the fastest Q3 result because the lookup was highly selective.
- DuckDB’s query execution was efficient, but the short CLI-run queries were slower end to end than PostgreSQL here.
- SQLite’s `mmap_size` change was easy to apply, but the observed Q1 time still stayed above PostgreSQL.
