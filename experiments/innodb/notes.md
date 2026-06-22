# innodb

mysql 8.0.42, local instance on port 3307 (separate data dir so I don't mess with my main mysql)

## what I ran
- created `orders` (100 rows) and `users` (100 rows) — see `schema.sql`
- EXPLAIN on PK vs email lookup → both `const`, 1 row (`explain.txt`)
- SHOW ENGINE INNODB STATUS → buffer pool hit 999/1000, redo ahead of checkpoint (`status.txt`)
- EXPLAIN FORMAT=JSON in `explain-json.txt`

## phantom read
two sessions, RR isolation:
- session A counts pending orders → 33
- session B inserts another pending row, commits → total is 34
- session A counts again → still 33

so the snapshot sticks. details in `phantom-read.txt`
