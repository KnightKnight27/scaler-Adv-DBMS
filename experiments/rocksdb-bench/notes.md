# rocksdb

cloned v9.10.0, built `db_bench` with cmake + mingw (took a while)

ran 100k random writes then random reads, compared:
- leveled compaction (default)
- universal compaction

summary in `db-bench.txt`. full counter dump in `db-bench-full.txt`.

didn't see any compaction at 100k keys — probably need millions to trigger stalls.
