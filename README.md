# SQLite3 vs PostgreSQL Comparison

## SQLite3 Experiments

### Commands Used

```bash
ls -lh
```

```bash
sqlite3 test.db
```

```sql
PRAGMA page_size;
```

```sql
PRAGMA page_count;
```

```sql
PRAGMA mmap_size;
```

```sql
PRAGMA mmap_size = 268435456;
```

```bash
time sqlite3 test.db "PRAGMA mmap_size=0; SELECT * FROM users;"
```

```bash
time sqlite3 test.db "PRAGMA mmap_size=268435456; SELECT * FROM users;"
```

```bash
ps aux | grep sqlite
```

---

### Observations

#### Database File Size

The database file size was **8.8 MB**.

SQLite keeps the entire database inside one file, which in this experiment was `test.db`.

#### Page Size

The SQLite page size was **4096 bytes**, which is equal to **4 KB**.

#### Page Count

The total number of pages in the SQLite database was **2242**.

#### mmap_size

- The default value of `mmap_size` was **0**.
- During the experiment, it was changed to **268435456 bytes**, or **256 MB**.
- After closing and reopening SQLite, the value returned to **0**.
- This shows that `mmap_size` behaved as a connection-specific setting in this setup.

#### Query Performance

Query timing without mmap:

```text
real 0.04
user 0.03
sys 0.00
```

Query timing with mmap enabled at 256 MB:

```text
real 0.04
user 0.03
sys 0.00
```

#### Performance Observation

The query time was almost the same with and without mmap. Since the database was only 8.8 MB, the operating system cache was already enough to make both runs fast.

#### SQLite Process Observation

```text
mahirabidi 50329 0.0 0.0 410059408 144 ?? R 2:48PM 0:00.00 grep sqlite
mahirabidi 50315 0.0 0.0 410229392 2160 ?? Ss 2:48PM 0:00.01 /bin/zsh -lc ps aux | grep sqlite
```

SQLite did not appear as a separate long-running server process. It runs inside the command or application that opens the database file.

## PostgreSQL Experiments

### Commands Used

```bash
brew install postgresql
```

```bash
/opt/homebrew/opt/postgresql@18/bin/postgres -D /opt/homebrew/var/postgresql@18 -p 55432
```

```bash
/opt/homebrew/opt/postgresql@18/bin/createdb -p 55432 scaler_lab
```

```bash
/opt/homebrew/opt/postgresql@18/bin/psql -p 55432 -d scaler_lab
```

```sql
SHOW block_size;
```

```sql
SELECT relpages
FROM pg_class
WHERE relname = 'users';
```

```sql
SELECT pg_size_pretty(pg_relation_size('users')) AS table_size;
```

```sql
EXPLAIN ANALYZE SELECT * FROM users;
```

```bash
ps aux | grep postgres
```

### Observations

#### Block Size

```text
block_size
8192
```

The PostgreSQL block size was **8192 bytes**, or **8 KB**.

#### Page Count

```text
relpages
935
```

The `users` table occupied **935 pages** in PostgreSQL.

The table size was:

```text
table_size
7480 kB
```

#### Query Execution Time

```text
QUERY PLAN
Seq Scan on users  (cost=0.00..1935.00 rows=100000 width=43) (actual time=0.270..4.125 rows=100000.00 loops=1)
  Buffers: shared hit=935
Planning:
  Buffers: shared hit=80
Planning Time: 0.225 ms
Execution Time: 6.496 ms
```

#### Query Performance Observation

PostgreSQL performed a sequential scan on the `users` table. The query planner first prepared the execution plan, and then PostgreSQL executed the query efficiently with an execution time of about **6.496 ms**.

#### PostgreSQL Processes

```text
mahirabidi 57237 0.0 0.1 410700240 4256 ?? Ss 2:55PM 0:00.04 /opt/homebrew/opt/postgresql@18/bin/postgres -D /opt/homebrew/var/postgresql@18 -p 55432
mahirabidi 57254 0.0 0.0 410700928 1504 ?? Ss 2:55PM 0:00.01 postgres: io worker 0
mahirabidi 57255 0.0 0.0 410699904 1504 ?? Ss 2:55PM 0:00.00 postgres: io worker 1
mahirabidi 57256 0.0 0.0 410830976 1504 ?? Ss 2:55PM 0:00.00 postgres: io worker 2
mahirabidi 57257 0.0 0.0 410699904 1424 ?? Ss 2:55PM 0:00.00 postgres: checkpointer
mahirabidi 57258 0.0 0.0 410830976 1744 ?? Ss 2:55PM 0:00.02 postgres: background writer
mahirabidi 57260 0.0 0.0 410699904 1760 ?? Ss 2:55PM 0:00.01 postgres: walwriter
mahirabidi 57261 0.0 0.0 410709120 2352 ?? Ss 2:55PM 0:00.00 postgres: autovacuum launcher
mahirabidi 57262 0.0 0.0 410832000 2160 ?? Ss 2:55PM 0:00.00 postgres: logical replication launcher
```

#### Process Observation

PostgreSQL runs using multiple background processes. These include processes for checkpointing, writing WAL records, background writing, autovacuum, and logical replication. This shows that PostgreSQL works as a client-server database system, unlike SQLite.

## Comparison Analysis

| Feature | SQLite3 | PostgreSQL |
|:---|:---|:---|
| Architecture | Embedded database | Client-server database |
| Storage | Stored in a single database file | Stored across multiple internal database files |
| Page Size | 4096 bytes | 8192 bytes |
| Page Count | 2242 | 935 |
| mmap Support | Supports mmap using `PRAGMA mmap_size` | Uses shared buffers and OS cache instead of direct mmap configuration |
| Query Timing | Around 0.04 seconds | Around 6.5 ms execution time |
| Processes | Single lightweight process | Multiple server and background processes |
| Setup Complexity | Very simple | Requires more setup |
| Concurrency Support | Limited compared to PostgreSQL | Strong concurrency support |
| Resource Usage | Low resource usage | Uses more resources due to server architecture |

## Conclusion

SQLite3 is simple, lightweight, and easy to set up. It stores the database in a single file and can be inspected easily using PRAGMA commands. In this experiment, enabling mmap did not create a noticeable timing improvement because the database was small and already cached efficiently.

PostgreSQL is a more advanced relational database system based on client-server architecture. It uses multiple background processes and provides better support for query optimization, concurrency, and scalability. However, it also requires more setup and uses more system resources than SQLite.

From the experiment, SQLite was easier to use and inspect, while PostgreSQL showed better execution efficiency and a more powerful internal architecture. mmap did not significantly change SQLite performance in this run, whereas PostgreSQL relied on its own buffer management and server processes for performance.
