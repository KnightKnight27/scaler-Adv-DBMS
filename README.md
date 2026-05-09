# Lab 2 — SQLite3 & PostgreSQL Storage Exploration

> Tried out the things sir showed us in the lecture. Used a sample database with a `users` table and ran experiments to understand how both databases handle storage internally.

---

## SQLITE3

### Setting up the database

First I created `sample.db` and added a `users` table with columns `id`, `name`, `email`, `age`, and `city`. Checked the file size right after:

```
-rw-r--r--@ 1 aryansirohi  staff   8.0K May  9 18:52 sample.db
```

The whole database was sitting as a single file on the filesystem. This was honestly surprising to me at first — I always thought databases were some complicated system, but here it is, just a file.

---

### After inserting 10,000 rows

```python
# used a python script to bulk insert 10k rows
import sqlite3, random

conn = sqlite3.connect('sample.db')
c = conn.cursor()
# ... inserted 10000 rows with random names, emails, ages, cities
conn.commit()
```

File size after insertion:

```
-rw-r--r--@ 1 aryansirohi  staff   1.0M May  9 18:56 sample.db
```

The file grew from 8KB to 1MB. Makes sense — more data means more pages allocated internally. The file just keeps getting bigger as you add data.

---

### PRAGMA page_size

```bash
sqlite3 sample.db "PRAGMA page_size;"
```

Output:

```
4096
```

So SQLite uses 4KB pages. Instead of working byte by byte, it reads and writes data in chunks of 4KB. This is similar to how the OS handles disk I/O in blocks — made me think about the connection between databases and the OS.

---

### PRAGMA page_count

```bash
sqlite3 sample.db "PRAGMA page_count;"
```

Output:

```
257
```

So 257 pages × 4096 bytes = 1,052,672 bytes ≈ 1.0MB — which matches the file size exactly. The math actually worked out and that was satisfying to see. The entire database is literally just a sequence of 4KB pages laid out in that one file.

---

### Checking mmap_size (default)

```bash
sqlite3 sample.db "PRAGMA mmap_size;"
```

Output:

```
0
```

mmap is disabled by default. mmap (memory-mapped I/O) allows the database file to be mapped directly into the process's virtual address space, so instead of doing `read()` system calls, it can just access memory addresses — the OS handles the actual disk reads behind the scenes.

---

### Enabling mmap — setting it to 30MB

```bash
sqlite3 sample.db "PRAGMA mmap_size = 31457280;"
```

Output:

```
31457280
```

Now SQLite is allowed to use up to 30MB of mmap for this database.

---

### Timing SELECT query — WITHOUT mmap

Reset mmap to 0 first, then ran:

```bash
time sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM users;" > /dev/null
```

Result:

```
real    0m0.012s
user    0m0.007s
sys     0m0.002s
```

---

### Timing SELECT query — WITH mmap (30MB)

```bash
time sqlite3 sample.db "PRAGMA mmap_size=31457280; SELECT * FROM users;" > /dev/null
```

Result:

```
real    0m0.010s
user    0m0.007s
sys     0m0.002s
```

The difference was very small — mmap didn't give a dramatic improvement here. I think this is because the dataset (1MB) is tiny and macOS already caches the file pages in memory after the first read. So there's nothing really left for mmap to optimize. It would probably show a bigger difference on a much larger database that doesn't fit in the OS page cache.

---

### Checking the inode number

```bash
ls -i sample.db
```

Output:

```
82970004 sample.db
```

This is the inode — the internal identity Linux/Unix assigns to a file. It doesn't change even if you rename the file.

---

### SQLite process check

Opened `sqlite3 sample.db` in one terminal, then ran this in another:

```bash
ps aux | grep sqlite
```

Output (while open):

```
aryansirohi  14023   0.0  0.0  34152  4812 s001  S+    6:58PM   0:00.01 sqlite3 sample.db
aryansirohi  14089   0.0  0.0  34052  2688 s002  S+    6:59PM   0:00.00 grep sqlite
```

After closing with `.quit`:

```bash
ps aux | grep sqlite
```

Output (after exit):

```
aryansirohi  14102   0.0  0.0  34052  2300 s002  S+    6:59PM   0:00.00 grep sqlite
```

The sqlite3 process is completely gone after exiting. This confirmed what sir said — SQLite is an embedded database. There's no persistent server process running in the background. When your program exits, that's it, nothing left behind.

---

### Creating a second table — does it create a new file?

```sql
CREATE TABLE products (
  id INTEGER PRIMARY KEY,
  name TEXT,
  price REAL
);
INSERT INTO products (name, price) VALUES ('Pen', 10.5), ('Notebook', 55.0);
```

Checked the directory after:

```bash
ls -lh sample.db
```

```
-rw-r--r--@ 1 aryansirohi  staff   1.0M May  9 19:01 sample.db
```

No new file. Only `sample.db` exists, size barely changed (a few KB for the new pages). Inode stayed the same:

```
82970004 sample.db
```

SQLite puts every table inside the same single file. This is very different from what PostgreSQL does (as I found out later).

---

## POSTGRESQL

> Installed PostgreSQL on macOS using Homebrew. Created a database called `mydb` and ran the same kinds of experiments.

---

### Setup

```bash
brew install postgresql
brew services start postgresql
createdb mydb
psql -d mydb
```

Created the users table and inserted 10,000 rows:

```sql
CREATE TABLE users (
  id SERIAL PRIMARY KEY,
  name TEXT NOT NULL,
  email TEXT UNIQUE NOT NULL,
  age INTEGER,
  city TEXT
);
-- then inserted 10,000 rows
```

---

### Timing a query with \timing

Turned on query timing inside psql:

```sql
\timing on
SELECT * FROM users;
```

Output:

```
Time: 6.512 ms
```

That's pretty fast for 10,000 rows. PostgreSQL handles this well because it has a proper buffer/cache layer internally.

---

### Block size (page size)

```sql
SHOW block_size;
```

Output:

```
 block_size
------------
 8192
```

PostgreSQL uses 8KB pages, which is double the 4KB SQLite uses. This is actually configurable at compile time for PostgreSQL (SQLite's is fixed at 4KB by default, though it can be changed before the DB is created).

---

### Calculating approximate page count

In SQLite, page count was a simple PRAGMA. In PostgreSQL it's a bit different:

```sql
SELECT pg_relation_size('users') / 8192 AS approx_pages;
```

Output:

```
 approx_pages
--------------
           64
```

So 64 pages × 8192 bytes = 524,288 bytes ≈ 512KB used for the users table. Note that this is just the table itself — PostgreSQL also creates separate files for indexes, TOAST data, etc.

---

### Shared buffers (PostgreSQL's memory cache)

```sql
SHOW shared_buffers;
```

Output:

```
 shared_buffers
----------------
 128MB
```

This is PostgreSQL's internal buffer pool — it caches pages in RAM so it doesn't have to go to disk every time. This is PostgreSQL's equivalent to SQLite's mmap in some sense, but it's a dedicated server-managed cache rather than a simple file mapping. You can't just set it to 0 and test without it the way I could with SQLite's mmap_size.

---

### Does each table get its own file?

First checked where the users table is stored:

```sql
SELECT pg_relation_filepath('users');
```

Output:

```
 pg_relation_filepath
----------------------
 base/16384/16385
```

Then created a products table and checked:

```sql
CREATE TABLE products (id SERIAL PRIMARY KEY, name TEXT, price REAL);
SELECT pg_relation_filepath('products');
```

Output:

```
 pg_relation_filepath
----------------------
 base/16384/16392
```

Both are in the same directory (`base/16384/`) but they're different files — `16385` and `16392`. This is completely opposite to SQLite. PostgreSQL gives each table its own file in the data directory.

---

### PostgreSQL process check (before and after exiting psql)

While connected with `psql`, ran in another terminal:

```bash
ps aux | grep postgres
```

Output:

```
_postgres  1823   0.0  0.1  220984  22016 ??  Ss   12:25PM   0:00.14 /usr/local/opt/postgresql@14/bin/postgres -D /usr/local/var/postgresql@14
_postgres  1824   0.0  0.0  221240   7808 ??  Ss   12:25PM   0:00.02 postgres: checkpointer
_postgres  1825   0.0  0.0  221112   6144 ??  Ss   12:25PM   0:00.02 postgres: background writer
_postgres  1826   0.0  0.0  221240   7936 ??  Ss   12:25PM   0:00.03 postgres: walwriter
_postgres  1827   0.0  0.0  221496   8192 ??  Ss   12:25PM   0:00.05 postgres: autovacuum launcher
aryansirohi  2109  0.0  0.0  34344  9216 s001  S+   7:12PM   0:00.03 psql -d mydb
_postgres  2110  0.0  0.0  226040  11264 ??  Ss   7:12PM   0:00.01 postgres: aryansirohi mydb [local] idle
aryansirohi  2150  0.0  0.0  34052   2300 s002  S+   7:13PM   0:00.00 grep postgres
```

After running `\q` to exit psql:

```bash
ps aux | grep postgres
```

Output:

```
_postgres  1823   0.0  0.1  220984  22016 ??  Ss   12:25PM   0:00.14 /usr/local/opt/postgresql@14/bin/postgres -D /usr/local/var/postgresql@14
_postgres  1824   0.0  0.0  221240   7808 ??  Ss   12:25PM   0:00.02 postgres: checkpointer
_postgres  1825   0.0  0.0  221112   6144 ??  Ss   12:25PM   0:00.02 postgres: background writer
_postgres  1826   0.0  0.0  221240   7936 ??  Ss   12:25PM   0:00.03 postgres: walwriter
_postgres  1827   0.0  0.0  221496   8192 ??  Ss   12:25PM   0:00.05 postgres: autovacuum launcher
aryansirohi  2161  0.0  0.0  34052   2300 s002  S+   7:15PM   0:00.00 grep postgres
```

The `psql` client and the session worker process (`aryansirohi mydb [local] idle`) are gone, but all the background PostgreSQL processes — checkpointer, background writer, walwriter, autovacuum — are still running. They were there before I connected and they're still there after I disconnected.

This is fundamentally different from SQLite. PostgreSQL is a server — it runs continuously and manages multiple connections. SQLite is embedded — it only exists while your program is using it.

---

## Comparison: SQLite3 vs PostgreSQL

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| **Page / Block Size** | 4096 bytes (4KB) | 8192 bytes (8KB) |
| **Page Count (10k rows)** | 257 pages | ~64 pages (per table) |
| **Total storage used** | ~1.0MB (all tables in one file) | ~512KB per table (separate files) |
| **Query time (10k rows)** | ~0.012s (shell overhead included) | ~6.5ms |
| **mmap support** | Yes, via `PRAGMA mmap_size` | Not directly exposed; uses `shared_buffers` instead |
| **mmap impact** | Minimal on small datasets (OS page cache already helps) | N/A (abstracted away) |
| **Storage model** | Single file for entire database | Separate file per table/index |
| **Process model** | Embedded — no background process | Client-server — always running in background |
| **Memory caching** | Optional mmap | Dedicated shared buffer pool (128MB default) |

---

### mmap behavior — what I noticed

SQLite without mmap (mmap_size = 0):
```
real    0m0.012s
user    0m0.007s
sys     0m0.002s
```

SQLite with mmap (mmap_size = 30MB):
```
real    0m0.010s
user    0m0.007s
sys     0m0.002s
```

The improvement was tiny — about 2ms. The reason is that once the OS loads the file pages into its page cache (which happens after the first read), subsequent reads are served from RAM anyway, with or without mmap. mmap would help more in scenarios where the database is much larger than RAM, or where you're doing random seeks across a huge file.

---

## What I ended up learning from all this

- Databases are just files. Opening them in a hex editor and seeing the page structure would literally show you the data.
- SQLite is incredibly simple internally — one file, fixed-size pages, no server. Great for local apps or small projects.
- PostgreSQL is a proper server with background workers doing checkpointing, write-ahead logging, autovacuum, etc. — all happening even when no one is connected.
- The page size difference (4KB vs 8KB) affects how efficiently space is used and how many I/O operations are needed for a given amount of data.
- mmap is not a magic performance fix. On small databases with warm OS caches, it barely makes a difference.
- Even something as simple as `sqlite3 sample.db` involves dozens of system calls — `openat()`, `mmap()`, `read()`, `fstat()`, `close()` — before you even type a query.
- The embedded vs. client-server distinction matters a lot in practice — SQLite can't handle multiple writers simultaneously, PostgreSQL was built for exactly that.
