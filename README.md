# scaler-Adv-DBMS Lab

## 1. SQLite Exploration

### Commands used

- dir
- PRAGMA page_size;
- PRAGMA page_count;
- PRAGMA mmap_size;
- Measure-Command { .\sqlite3.exe sample.db "SELECT * FROM users;" }
- Get-Process sqlite3

### Observations

- The sample.db file is 8192 bytes in size.
- page_size is 4096.
- page_count is 2.
- mmap_size is initially 0.
- Increasing mmap_size decreases query time from 27.3076 ms to 15.5844 ms for a table of 3 rows with values (name, age).

## 2. PostgreSQL Setup

### Commands used

- \c test (to connect db)
- SHOW block_size;
- SELECT pq_relation_size('users') AS BYTES; to calculate page count
- \timing (to enable timing)

### Observations

- page_size is 8192
- page_count is 1
- Query time was 0.570 ms for a table of 3 rows qith values (name, age).

## 3. Comparison Report

Comparison | SQLite3 | PostgreSQL
|--|--|--
Page Size | 4096 | 8192
Page Count | 2 | 1
Query Performance | 15.5844ms | 0.570ms
mmap impact | 27.3076ms to 15.5844ms | PostgreSQL does not expose mmap like SQLite3 does
