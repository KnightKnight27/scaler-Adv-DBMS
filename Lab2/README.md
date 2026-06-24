# Storage Engine Lab Report: SQLite3 vs PostgreSQL

**Name:** Daksh  
**Assignment:** Lab-2

---

## Objective

The goal of this lab was to observe how SQLite3 and PostgreSQL store data internally, how page-based storage works, how memory mapping affects SQLite, and how PostgreSQL differs as a server-based database system.

---

## 1) SQLite3 Experiment

### Setup

I created a sample SQLite database named `sample.db`, added a `users` table, and inserted 10,000 rows to make the file size and storage behavior easier to observe.

### File size before and after loading data

At first, the database file was very small:

```bash
-rwxr-xr-x 1 daksh root 8.0K May  9 12:10 sample.db
```

After inserting more records, the file grew automatically:

```bash
-rwxr-xr-x 1 daksh root 196K May  9 12:10 sample.db
```

This showed that SQLite stores the entire database inside a single file and expands that file as data increases.

### Page size and page count

I checked the page-related settings using `PRAGMA`:

```sql
PRAGMA page_size;
PRAGMA page_count;
```

The results were:

- **Page size:** `4096`
- **Page count:** `49`

That means SQLite was using 4 KB pages, and the database file was split into 49 pages after the insert operation.

### mmap behavior

I also checked the mmap setting:

```sql
PRAGMA mmap_size;
```

Initially, it returned `0`, which means mmap was disabled.

Then I enabled a larger mmap region:

```sql
PRAGMA mmap_size = 30000000;
```

This allowed SQLite to map part of the database into memory instead of relying only on normal file I/O.

### Query timing with and without mmap

I compared query time for:

```sql
SELECT * FROM users;
```

Without mmap, the result was:

```bash
real    0m0.070s
user    0m0.014s
sys     0m0.023s
```

With mmap enabled, the result was:

```bash
real    0m0.091s
user    0m0.006s
sys     0m0.040s
```

In this case, mmap did not give a clear speedup. The dataset was small, so the Linux page cache already handled most of the work efficiently.

### Process check

To confirm how SQLite runs, I opened `sqlite3 sample.db` in one terminal and checked processes from another terminal:

```bash
daksh+   47121  0.0  0.0  12056  5424 pts/0    S+   16:47   0:00 sqlite3 sample.db
daksh+   47693  0.0  0.0   9156  2296 pts/2    S+   16:49   0:00 grep --color=auto sqlite
```

After exiting SQLite, the `sqlite3` process disappeared:

```bash
daksh+   47907  0.0  0.0   9156  2300 pts/2    S+   16:49   0:00 grep --color=auto sqlite
```

This confirmed that SQLite is embedded and does not keep a background server process running.

### System call observation

I also used `strace` on SQLite. The output was large, but some important calls I noticed were:

- `openat()`
- `read()`
- `mmap()`
- `close()`

A small part of the trace looked like this:

```bash
brk(NULL)                               = 0x59af8b385000
mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x77da2d28e000
access("/etc/ld.so.preload", R_OK)      = -1 ENOENT (No such file or directory)
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=81467, ...}) = 0
mmap(NULL, 81467, PROT_READ, MAP_PRIVATE, 3, 0) = 0x77da2d27a000
close(3)                                = 0
openat(AT_FDCWD, "/lib/x86_64-linux-gnu/libsqlite3.so.0", O_RDONLY|O_CLOEXEC) = 3
read(3, "\177ELF\2\1\1\0\0\0\0\0\0\0\0\0\3\0>\0\1\0\0\0\0\0\0\0\0\0\0\0"..., 832) = 832
```

This made it clear that even a simple database program depends on several low-level operating system operations.

### Inode check

I checked the inode of the database file:

```bash
104371 sample.db
```

The inode acts like the file’s internal identity in the filesystem.

### Adding another table

Since SQLite is an embedded database, I tested whether creating another table would generate a new file.

```sql
CREATE TABLE products (
  id INTEGER PRIMARY KEY,
  price INT
);
```

After inserting a few rows and checking the directory again, there was still only one database file, and the file size increased slightly:

```bash
-rwxr-xr-x 1 daksh root 200K May  9 17:17 sample.db
```

The inode stayed the same:

```bash
104371 sample.db
```

This confirmed that multiple tables live inside the same SQLite database file.

---

## 2) PostgreSQL (PSQL) Experiment

### Setup

I repeated the same style of experiment in PostgreSQL by creating a database, making a `users` table, and inserting 10,000 rows.

### Timing a query

I enabled timing using:

```sql
	timing
```

Then I ran:

```sql
SELECT * FROM users;
```

The execution time was:

```bash
Time: 4.239 ms
```

This was very fast, even with a larger dataset.

### Block size

I checked the PostgreSQL block size using:

```sql
SHOW block_size;
```

It returned:

```bash
8192
```

So PostgreSQL uses an 8 KB internal block size.

### Estimating page usage

To get a rough idea of how many pages the table occupied, I used:

```sql
SELECT pg_relation_size('users') / 8192 AS approx_pages;
```

The output was:

```bash
approx_pages
--------------
64
```

This gave me an approximate count of the blocks used by the relation.

### Shared buffers

I also checked buffer memory:

```sql
SHOW shared_buffers;
```

It returned:

```bash
128MB
```

This showed that PostgreSQL keeps a shared buffer pool in memory for caching data pages.

### Table file locations

To compare storage layout with SQLite, I checked where the table files were stored.

For `users`:

```sql
SELECT pg_relation_filepath('users');
```

Output:

```bash
base/16388/16390
```

For `products`:

```sql
SELECT pg_relation_filepath('products');
```

Output:

```bash
base/16388/16399
```

Both tables were inside the same database cluster directory, but each relation had its own separate file. That is different from SQLite, where all tables stay inside one `.db` file.

### Process behavior

I checked PostgreSQL processes with:

```bash
ps aux | grep postgres
```

Before closing the client, I saw the server and helper processes running:

```bash
postgres    2164  0.0  0.1 225476 30272 ?        Ss   12:25   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
postgres    2175  0.0  0.1 225756 23812 ?        Ss   12:25   0:00 postgres: 16/main: checkpointer
postgres    2176  0.0  0.0 225628  7808 ?        Ss   12:25   0:00 postgres: 16/main: background writer
postgres    2182  0.0  0.0 225476 10348 ?        Ss   12:25   0:00 postgres: 16/main: walwriter
postgres    2183  0.0  0.0 227080  8520 ?        Ss   12:25   0:00 postgres: 16/main: autovacuum launcher
postgres    2184  0.0  0.0 227056  7976 ?        Ss   12:25   0:00 postgres: 16/main: logical replication launcher
```

After exiting `psql` with `\q`, the server-side PostgreSQL processes were still present:

```bash
postgres    2164  0.0  0.1 225476 30272 ?        Ss   12:25   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
postgres    2175  0.0  0.1 225756 23812 ?        Ss   12:25   0:00 postgres: 16/main: checkpointer
postgres    2176  0.0  0.0 225628  7808 ?        Ss   12:25   0:00 postgres: 16/main: background writer
postgres    2182  0.0  0.0 225476 10348 ?        Ss   12:25   0:00 postgres: 16/main: walwriter
postgres    2183  0.0  0.0 227080  8520 ?        Ss   12:25   0:00 postgres: 16/main: autovacuum launcher
postgres    2184  0.0  0.0 227056  7976 ?        Ss   12:25   0:00 postgres: 16/main: logical replication launcher
```

This showed that PostgreSQL is a server-based database system that keeps running in the background.

---

## 3) Comparison Summary

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Storage style | Entire database in one file | Separate files per relation inside the cluster |
| Page / block size | 4096 bytes | 8192 bytes |
| Query timing | Small difference with mmap | Fast execution in this test |
| Memory behavior | Can use `mmap_size` | Uses shared buffers |
| Process model | One client process | Always-running server process |
| Tables | Multiple tables inside one file | Tables stored as separate relation files |

---

## Final Takeaways

- SQLite is simple, compact, and fully file-based.
- PostgreSQL is more structured and runs as a service.
- Both systems rely on pages/blocks internally.
- Memory handling is different in each system.
- `mmap` can help in SQLite, but not always for small workloads.
- PostgreSQL keeps background processes alive even after the client exits.
- SQLite stores all tables in one database file, while PostgreSQL stores relations separately.

