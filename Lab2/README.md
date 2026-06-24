# Lab 2: SQLite3 & PostgreSQL Storage Analysis

**Name:** Shah Musharaf ul Islam
**College ID:** 24bcs10447
**OS:** Arch Linux | **Shell:** zsh

---

## SQLite3

Created a db with a `users` table and inserted 10,000 rows.

**File size before insert:**
```
-rw-r--r-- 1 musharaf musharaf 8.0K May  9 11:22 lab2.db
```

**After inserting 10,000 rows:**
```
-rw-r--r-- 1 musharaf musharaf 588K May  9 11:31 lab2.db
```

SQLite allocates pages as needed inside the same file. No new file is created even when you add more tables.

---

**PRAGMA checks:**

```sql
PRAGMA page_size;   -- 4096
PRAGMA page_count;  -- 147
PRAGMA mmap_size;   -- 0 (disabled by default)
```

147 × 4096 = 602,112 bytes ≈ 588K — matches `ls` output exactly.

---

**mmap experiment:**

```sql
PRAGMA mmap_size = 31457280;
```

Timed `SELECT * FROM users` with and without mmap:

```
-- without mmap
real    0m0.072s
user    0m0.013s
sys     0m0.031s

-- with mmap (30MB)
real    0m0.063s
user    0m0.008s
sys     0m0.019s
```

Not a huge difference. The `sys` time dropped noticeably which makes sense since mmap avoids the extra copy from kernel space. Dataset is small so Linux page cache already had most of it in memory anyway.

---

**Process check:**

```zsh
❯ ps aux | grep sqlite
musharaf  14821  0.0  0.0  11984  5280 pts/1  S+  11:34  0:00 sqlite3 lab2.db
```

After exiting — process is gone. SQLite is embedded, no background server running.

---

**Multiple tables — same file:**

Created a `products` table. No new file appeared, `lab2.db` just grew by 4K. Inode number stayed the same.

---

## PostgreSQL

Started the service and created `lab2db` with the same `users` table, inserted 10,000 rows.

```zsh
❯ sudo systemctl start postgresql
❯ sudo -u postgres psql
```

**Block size:**
```sql
SHOW block_size;  -- 8192
```

**Page count:**
```sql
SELECT relpages FROM pg_class WHERE relname = 'users';  -- 64
```

64 × 8192 = 524,288 bytes ≈ 512K (heap only, indexes are separate files).

**Query timing:**
```sql
\timing on
SELECT * FROM users;
-- Time: 6.014 ms
```

**Shared buffers:**
```sql
SHOW shared_buffers;  -- 128MB
```

PostgreSQL handles memory through a shared buffer pool instead of exposing mmap directly.

---

**Table storage — separate files:**

```sql
SELECT pg_relation_filepath('users');     -- base/16388/16390
SELECT pg_relation_filepath('products');  -- base/16388/16401
```

Same directory, different files. Opposite of SQLite.

---

**Process check:**

```zsh
❯ ps aux | grep postgres
postgres  1823  0.0  0.1 225756 23800 ?  Ss  10:51  0:00 postgres: checkpointer
postgres  1824  0.0  0.0 225628  7788 ?  Ss  10:51  0:00 postgres: background writer
postgres  1830  0.0  0.0 225476 10300 ?  Ss  10:51  0:00 postgres: walwriter
postgres  1831  0.0  0.0 227080  8500 ?  Ss  10:51  0:00 postgres: autovacuum launcher
```

After `\q` — all of those processes are still running. PostgreSQL is a server, not an embedded library.

---

## Comparison

| Metric | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| Page size | 4 KB | 8 KB |
| Page count (10k rows) | 147 | 64 |
| Storage model | All tables in one file | Each table/index = separate file |
| mmap | Manual (`PRAGMA mmap_size`) | Not exposed, uses `shared_buffers` |
| `SELECT *` time (10k rows) | ~0.072s | ~6 ms |
| Process after client exits | Gone | Server keeps running |
| Architecture | Embedded | Client-server |

---

## What I learned

- The page_count × page_size math lining up exactly with `ls -lh` was a good sanity check for understanding how SQLite lays out the file internally.
- mmap didn't make a dramatic difference here because the dataset was small enough to stay in the OS page cache. The drop in `sys` time is the real indicator that something changed at the kernel level.
- PostgreSQL running multiple background processes even when no client is connected was the most visible architectural difference from SQLite.
