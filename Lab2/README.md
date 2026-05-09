# Creating the DB (Process almost same for both sqlite and psql)

- `sqlite3 sample.db`

```sql
CREATE TABLE users (
  id    INTEGER PRIMARY KEY,
  name  TEXT,
  email TEXT,
  age   INTEGER,
  city  TEXT
);
```

- `INSERT INTO users VALUES (1, 'Mohammed', 'm@example.com', 20, 'Mumbai');`

# Inspecting file size in sqlite

- `ls -lh`
```
-rw-r--r--  1 user  staff   5.0M  6 May 13:07 sample.db
```

# Page size and number of pages in sqlite3

- sqlite> `PRAGMA page_size;`
```
4096
```

- sqlite> `PRAGMA page_count;`
```
1285
```
This makes sense because 4096 × 1285 = 5,263,360 bytes ≈ 5.0M, which matches the file size.

# Page size in PostgreSQL

```sql
SELECT current_setting('block_size');
```
```
8192
```
Almost double of sqlite3. PostgreSQL calls pages "blocks" and defaults to 8 KB.

# mmap_size in sqlite3

- sqlite> `PRAGMA mmap_size;`
```
0
```

# Does changing mmap_size make a difference? sqlite3

Yes, theoretically it should. Because then data is not being copied from kernel buffer cache to user space — it's being memory mapped.

I tested this on a 100k-row users table:

```bash
time sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM users;" > /dev/null
```
```
real 0.041s
```

```bash
time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;" > /dev/null
```
```
real 0.033s
```

The time decreased with mmap enabled (~20% faster). Without mmap, `read()` copies data from kernel buffer to heap (two copies). With mmap, the file is mapped directly into the process address space (one copy).

# mmap_size in PostgreSQL

I couldn't find the exact terminology "mmap_size" in PostgreSQL.
I'm guessing it might be related to `shared_buffers` — the shared memory pool PostgreSQL uses to cache pages across all connections.

```sql
SHOW shared_buffers;
```
```
128MB
```

According to the official docs, the default is 128 MB.

# sqlite runs as a library and postgres runs as a service — confirmed via `ps aux`

```
$ ps aux | grep sqlite3
user   71776  0.0  0.0   7152  2180 pts/2  S+  23:11  0:00 grep sqlite3
```
No sqlite3 process — it runs as a library inside your process, not a server.

```
$ ps aux | grep postgres
postgres  68910  0.0  0.1 218296 29732 ?  Ss  23:02  0:00 /usr/lib/postgresql/17/bin/postgres -D /var/lib/postgresql/17/main
postgres  68911  0.0  0.1 218580 21688 ?  Ss  23:02  0:00 postgres: 17/main: checkpointer
postgres  68912  0.0  0.0 218432  7448 ?  Ss  23:02  0:00 postgres: 17/main: background writer
postgres  68914  0.0  0.0 218296 10304 ?  Ss  23:02  0:00 postgres: 17/main: walwriter
postgres  68915  0.0  0.0 219884  8856 ?  Ss  23:02  0:00 postgres: 17/main: autovacuum launcher
```
PostgreSQL runs as a persistent server daemon with multiple background worker processes.
