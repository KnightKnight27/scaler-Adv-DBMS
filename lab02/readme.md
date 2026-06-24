# SQLITE3
**Name:** Patel Jash | **Batch:** B | **Roll:** 24BCS10632 | **Lab:** 02 | **Title:** Storage_Engine_2

> In this lab I explored how SQLite3 manages its storage internally, using the pragmas and commands demonstrated during the lecture.

---

* To begin, I created a fresh database along with a `users` table. The initial file size turned out to be 8.0K.

  ```
  -rwxr-xr-x 1 zephoryx root 8.0K May  9 12:10 sample.db
  ```

  What caught my attention is that the entire database lives inside a single ordinary file on disk — there is no separate server or directory structure involved.

---

* After inserting 10,000 rows the file grew to 196K.

  ```
  -rwxr-xr-x 1 zephoryx root 196K May  9 12:10 sample.db
  ```

  This clearly shows that as we keep adding records, SQLite expands the same file by appending additional pages internally rather than creating new files.

---

* Running `PRAGMA page_size` gave 4096, which tells us each page occupies 4KB.

  SQLite organizes the database into uniformly sized pages. All read/write operations happen at the page level, not individual bytes.

---

* `PRAGMA page_count` yielded 49. Quick math: 196KB ÷ 4KB = 49 pages — it lines up exactly with the file size.

  This confirmed that the entire file is essentially a contiguous array of pages. Knowing the page count lets us reason about how much storage a table actually consumes.

---

* `PRAGMA mmap_size` returned 0, indicating memory-mapped I/O was currently turned off.

  By default SQLite relies on regular read/write system calls instead of mapping the file into the process address space.

---

* I then enabled mmap by setting the size to 30MB so that the database could be accessed via memory-mapped I/O.

  Memory-mapped I/O allows the OS to map the file directly into virtual memory, potentially reducing the overhead of repeated read() calls.

---

* First I reset mmap back to 0 and timed the `SELECT * FROM users;` query:

  ```
  real    0m0.070s
  user    0m0.014s
  sys     0m0.023s
  ```

---

* Then I re-enabled mmap at 30MB and ran the same query again:

  ```
  real    0m0.091s
  user    0m0.006s
  sys     0m0.040s
  ```

  Interestingly, enabling mmap did not speed things up here. For a small dataset like this, the Linux kernel's page cache already buffers the file efficiently, so mapping it into memory provided no additional benefit.

---

* I opened `sqlite3 sample.db` in one terminal and examined the running processes from another:

  ```
  zephoryx   47121  0.0  0.0  12056  5424 pts/0    S+   16:47   0:00 sqlite3 sample.db
  zephoryx   47693  0.0  0.0   9156  2296 pts/2    S+   16:49   0:00 grep --color=auto sqlite
  ```

  Once I closed the sqlite3 session the process disappeared completely:

  ```
  zephoryx   47907  0.0  0.0   9156  2300 pts/2    S+   16:49   0:00 grep --color=auto sqlite
  ```

  This demonstrates that SQLite is an in-process / embedded engine. It does not keep any background daemon running — the moment the host application exits, the database process terminates as well.

---

* To observe what happens under the hood I ran `strace sqlite3 sample.db`. The output was quite extensive; here is a small excerpt:

  ```
  brk(NULL)                               = 0x59af8b385000
  mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x77da2d28e000
  access("/etc/ld.so.preload", R_OK)      = -1 ENOENT (No such file or directory)
  openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
  fstat(3, {st_mode=S_IFREG|0644, st_size=81467, ...}) = 0
  mmap(NULL, 81467, PROT_READ, MAP_PRIVATE, 3, 0) = 0x77da2d27a000
  close(3)                                = 0
  openat(AT_FDCWD, "/lib/x86_64-linux-gnu/libsqlite3.so.0", O_RDONLY|O_CLOEXEC) = 3
  read(3, "\177ELF\2\1\1\0\0\0\0\0\0\0\0\0\3\0>\0\1\0\0\0\0\0\0\0\0\0\0\0"..., 832) = 832
  fstat(3, {st_mode=S_IFREG|0644, st_size=1468440, ...}) = 0
  mmap(NULL, 1472056, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE, 3, 0) = 0x77da2d112000
  mmap(0x77da2d132000, 1093632, PROT_READ|PROT_EXEC, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x20000) = 0x77da2d132000
  ```

  Although most of the output was hard to parse, a few familiar system calls stood out:

  * `openat()` – opens files
  * `read()` – reads data from file descriptors
  * `mmap()` – maps file regions into memory
  * `close()` – releases file descriptors

  Even a seemingly simple action like launching the SQLite shell triggers dozens of low-level OS interactions.

---

* I also looked up the inode number of `sample.db`:

  ```
  104371 sample.db
  ```

  The inode is the unique identifier the filesystem uses to track this file internally, independent of its human-readable name.

---

* Since SQLite is embedded, adding another table should not produce a new file. I created a `products` table:

  ```
  CREATE TABLE products (
  id INTEGER PRIMARY KEY,
  price INT
  );
  ```

  After inserting a few rows I verified:

  ```
  -rwxr-xr-x 1 zephoryx root 200K May  9 17:17 sample.db
  ```

  The file only grew by 4KB and the inode remained identical:

  ```
  104371 sample.db
  ```

  This confirms that SQLite packs every table into the same single database file rather than allocating separate files per table.

---

# POSTGRESQL

> Next I performed similar experiments on PostgreSQL to compare how a client-server database handles storage.

---

* I followed the same workflow — set up a database called `labdb`, created a `users` table, and populated it with 10,000 rows.

---

* Enabled query timing with `\timing`.

---

* `SELECT * FROM users;` completed in roughly 4.193 ms.

  ```
  Time: 4.193 ms
  ```

  Even with 10k rows, PostgreSQL returned results very quickly thanks to its optimized buffer management.

---

* `SHOW block_size;` returned 8192, so PostgreSQL uses an 8KB block (page) size — twice as large as SQLite's default.

  All table data is stored and accessed in 8KB chunks internally.

---

* Computing the approximate page count requires a manual calculation because PostgreSQL does not expose a simple pragma like SQLite:

  ```
  SELECT pg_relation_size('users') / 8192 AS approx_pages;
  ```

  Result:

  ```
   approx_pages 
  --------------
             64
  ```

  This gave a rough idea of the number of 8KB blocks the `users` table occupies on disk.

---

* `SHOW shared_buffers;` returned 128MB.

  PostgreSQL maintains a pool of shared memory buffers (128MB here) that caches frequently accessed pages in RAM, reducing disk I/O. This is conceptually similar to mmap in SQLite but is managed explicitly by the database engine itself.

---

* Unlike SQLite, PostgreSQL stores each table in its own file. I verified this by querying the file paths:

  ```
  SELECT pg_relation_filepath('users');
  ```

  Output:

  ```
  base/16388/16390
  ```

  Then for the `products` table:

  ```
  SELECT pg_relation_filepath('products');
  ```

  Output:

  ```
  base/16388/16399
  ```

  Both reside under the same database directory (`base/16388/`) but as separate files. This is fundamentally different from SQLite's single-file model.

---

* Finally I checked whether PostgreSQL processes persist after disconnecting.

  While connected, `ps aux | grep postgres` showed:

  ```
  postgres    2175  0.0  0.1 225756 23812 ?        Ss   12:25   0:00 postgres: 16/main: checkpointer 
  postgres    2176  0.0  0.0 225628  7808 ?        Ss   12:25   0:00 postgres: 16/main: background writer 
  postgres    2182  0.0  0.0 225476 10348 ?        Ss   12:25   0:00 postgres: 16/main: walwriter 
  postgres    2183  0.0  0.0 227080  8520 ?        Ss   12:25   0:00 postgres: 16/main: autovacuum launcher 
  postgres    2184  0.0  0.0 227056  7976 ?        Ss   12:25   0:00 postgres: 16/main: logical replication launcher 
  root       58805  0.0  0.0  19860  7816 pts/2    S+   17:24   0:00 sudo -u postgres psql
  root       58825  0.0  0.0  19860  2716 pts/3    Ss   17:25   0:00 sudo -u postgres psql
  postgres   58826  0.0  0.0  26096  9296 pts/3    S+   17:25   0:00 /usr/lib/postgresql/16/bin/psql
  postgres   59796  0.0  0.1 228520 22944 ?        Ss   17:28   0:00 postgres: 16/main: postgres labdb [local] idle
  zephoryx   61282  0.0  0.0   9156  2292 pts/0    S+   17:32   0:00 grep --color=auto postgres
  ```

  After quitting with `\q`, the server-related processes were still active:

  ```
  postgres    2164  0.0  0.1 225476 30272 ?        Ss   12:25   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
  postgres    2175  0.0  0.1 225756 23812 ?        Ss   12:25   0:00 postgres: 16/main: checkpointer 
  postgres    2176  0.0  0.0 225628  7808 ?        Ss   12:25   0:00 postgres: 16/main: background writer 
  postgres    2182  0.0  0.0 225476 10348 ?        Ss   12:25   0:00 postgres: 16/main: walwriter 
  postgres    2183  0.0  0.0 227080  8520 ?        Ss   12:25   0:00 postgres: 16/main: autovacuum launcher 
  postgres    2184  0.0  0.0 227056  7976 ?        Ss   12:25   0:00 postgres: 16/main: logical replication launcher 
  zephoryx   61659  0.0  0.0   9156  2300 pts/0    S+   17:34   0:00 grep --color=auto postgres
  ```

  This highlights the fundamental architectural difference: PostgreSQL operates as a dedicated server with several helper processes (checkpointer, background writer, WAL writer, etc.) that keep running regardless of whether any client is connected.

---

# KEY TAKEAWAYS

* Every database ultimately stores its data as files on the filesystem.
* Both SQLite and PostgreSQL break data into fixed-size pages, though the default sizes differ (4KB vs 8KB).
* SQLite is an embedded, serverless engine — everything lives in one file and no background process is needed.
* PostgreSQL follows a client-server architecture with persistent background workers handling I/O, caching, and maintenance.
* Memory-mapped I/O (mmap) can theoretically reduce copying overhead, but its benefit depends on dataset size and OS-level caching.
* Simple database operations rely heavily on underlying OS system calls like openat, read, mmap, and close.
* SQLite bundles all tables into a single file; PostgreSQL allocates a separate file per relation.
* Understanding storage engines bridges the gap between high-level SQL and low-level OS/memory management.