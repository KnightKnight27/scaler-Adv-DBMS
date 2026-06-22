# pg vs sqlite

22 jun 2026, ran on my laptop

**setup:** same table `bench_users` (id, name, dept_id), 5000 rows in one transaction

| | sqlite 3.44 | postgres 16 (docker) |
|--|-------------|----------------------|
| page size | 4 KB | 8 KB |
| disk after insert | 25 pages (~100 KB) | 31 pages (248 KB) |
| insert time | 126 ms | 1046 ms |
| filter query plan | SCAN bench_users | seq scan, 31 buffer hits, 0.48 ms |

switched sqlite to WAL mode with `PRAGMA journal_mode=WAL`

postgres slower on insert mostly because of docker + piping sql through psql.
both picked seq scan for `dept_id = 3` — makes sense at 5k rows.

see `results.txt` for raw output, `schema.sql` for table def
