# SQLite3 Experiments

> The following experiments were conducted using SQLite3, based on the concepts discussed in the lecture.

---

* **Initial Database Creation**: Created a new database named `sample.db` and added a `users` table. Initial file size observed was 8.0K.

  ```bash
  -rwxr-xr-x 1 sourabh root 8.0K May  9 21:10 sample.db
  ```

  This demonstrates that SQLite stores the entire database within a single regular file on the filesystem.

---

* **Data Insertion**: Inserted 10,000 rows into the `users` table. The file size subsequently increased to 196K.

  ```bash
  -rwxr-xr-x 1 sourabh root 196K May  9 21:15 sample.db
  ```

  The automatic increase in file size indicates that SQLite dynamically allocates additional pages internally as new data is added, while still maintaining everything in the same single file.

---

* **Page Size Analysis**: Running `PRAGMA page_size` returned `4096`.
  
  This confirms that SQLite manages data in fixed blocks or pages of 4KB. Instead of manipulating byte by byte, it performs operations at the page level.

---

* **Page Count Verification**: Running `PRAGMA page_count` yielded `49`. 
  
  Doing a quick calculation, total size / page size (196KB / 4KB) exactly equals 49. This perfectly validates that the database is internally structured as a collection of pages.

---

* **Memory Mapping (mmap) Status**: `PRAGMA mmap_size` returned `0`.
  
  This implies that memory-mapped I/O is disabled by default.

---

* **Enabling mmap**: Configured `mmap_size` to 30MB, allowing SQLite to utilize a memory-mapped area.
  
  Memory mapping allows the database file to be mapped directly into the RAM, theoretically reducing the overhead of repetitive read/write system calls.

---

* **Performance Testing without mmap**: Reset `mmap_size` to 0 and timed a basic query (`SELECT * FROM users;`).

  ```bash
  real    0m0.065s
  user    0m0.012s
  sys     0m0.021s
  ```

---

* **Performance Testing with mmap**: Set `mmap_size` back to 30MB and ran the same query again.

  ```bash
  real    0m0.088s
  user    0m0.008s
  sys     0m0.035s
  ```

  Interestingly, `mmap` did not yield a faster execution time here. This happens because for smaller datasets, the OS-level page cache is already highly effective, making the `mmap` performance gain negligible (and sometimes slightly slower due to mapping overhead).

---

* **Process Management**: Ran `sqlite3 sample.db` in one terminal and checked active processes from another terminal:

  ```bash
  sourabh   45102  0.0  0.0  12056  5424 pts/0    S+   21:20   0:00 sqlite3 sample.db
  sourabh   45120  0.0  0.0   9156  2296 pts/2    S+   21:21   0:00 grep --color=auto sqlite
  ```

  After exiting the sqlite3 shell, the process disappeared entirely:

  ```bash
  sourabh   45145  0.0  0.0   9156  2300 pts/2    S+   21:21   0:00 grep --color=auto sqlite
  ```

  This illustrates the nature of an embedded database. SQLite doesn't rely on a persistent background daemon/server; it runs only as long as the application using it is active.

---

* **System Calls Observation**: Ran `strace sqlite3 sample.db` to track system calls. While the output was vast, here's a relevant snippet:

  ```bash
  brk(NULL)                               = 0x56af8b385000
  mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x76da2d28e000
  access("/etc/ld.so.preload", R_OK)      = -1 ENOENT (No such file or directory)
  openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
  fstat(3, {st_mode=S_IFREG|0644, st_size=81467, ...}) = 0
  mmap(NULL, 81467, PROT_READ, MAP_PRIVATE, 3, 0) = 0x76da2d27a000
  close(3)                                = 0
  openat(AT_FDCWD, "/lib/x86_64-linux-gnu/libsqlite3.so.0", O_RDONLY|O_CLOEXEC) = 3
  read(3, "\177ELF\2\1\1\0\0\0\0\0\0\0\0\0\3\0>\0\1\0\0\0\0\0\0\0\0\0\0\0"..., 832) = 832
  fstat(3, {st_mode=S_IFREG|0644, st_size=1468440, ...}) = 0
  mmap(NULL, 1472056, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE, 3, 0) = 0x76da2d112000
  ```

  Even though it's complex, we can clearly identify fundamental I/O operations like `openat()`, `read()`, `mmap()`, and `close()`. It highlights that even starting SQLite involves numerous low-level OS interactions.

---

* **File Identity (Inode)**: Checked the inode number of the database file:

  ```bash
  205412 sample.db
  ```

  The inode serves as the unique identifier for this file within the Linux filesystem.

---

* **Multiple Tables Storage**: Since SQLite is embedded, creating a new table shouldn't spawn a new file. Executed the following:

  ```sql
  CREATE TABLE products (
  id INTEGER PRIMARY KEY,
  price INT
  );
  ```

  Inserted data and checked the file system again:

  ```bash
  -rwxr-xr-x 1 sourabh root 200K May  9 21:28 sample.db
  ```

  As expected, no additional files were created. The file size just grew slightly (by 4KB), and the inode remained unchanged:

  ```bash
  205412 sample.db
  ```

  This confirms that all tables, indexes, and data are housed within the single `sample.db` file.

---

# PostgreSQL Experiments

> Moving on to PostgreSQL analysis.

---

* **Database Initialization**: Replicated the setup by creating a database and a `users` table, followed by inserting 10,000 rows.

---

* **Query Timing**: Used the `\timing` meta-command to enable query execution time reporting.

---

* **Execution Speed**: Executed `SELECT * FROM users;`. Result:

  ```
  Time: 4.012 ms
  ```

  PostgreSQL executes the query significantly faster, showcasing its optimization for larger workloads.

---

* **Block Size**: Checked the internal block size using `SHOW block_size;` which resulted in `8192`.

  This means PostgreSQL organizes data into 8KB blocks (pages), twice the size of SQLite's default.

---

* **Estimating Page Count**: Unlike SQLite, PostgreSQL doesn't have a direct `PRAGMA`. Calculated the number of pages occupied by the table using:

  ```sql
  SELECT pg_relation_size('users') / 8192 AS approx_pages;
  ```

  Result:

  ```
   approx_pages 
  --------------
              64
  ```

  This provides a good estimate of the internal pages allocated for the table.

---

* **Memory Buffers**: Checked memory allocation with `SHOW shared_buffers;`, which returned `128MB`.

  PostgreSQL maintains a dedicated shared memory segment (RAM) to cache blocks, which operates differently from SQLite's straightforward mmap approach.

---

* **Storage Architecture**: Investigated whether new tables create new files. Found the location of the `users` table:

  ```sql
  SELECT pg_relation_filepath('users');
  ```

  Result:

  ```
  base/16388/16390
  ```

  Created a `products` table and checked its path:

  ```sql
  SELECT pg_relation_filepath('products');
  ```

  Result:

  ```
  base/16388/16399
  ```

  Both tables reside in the same base directory, but they correspond to entirely different files (`16390` vs `16399`). This is a fundamental architectural difference from SQLite, as PostgreSQL allocates separate files for different database relations (tables/indexes).

---

* **Process Persistence**: Checked active PostgreSQL processes before and after exiting the `psql` client.

  While `psql` was active:

  ```bash
  postgres    1845  0.0  0.1 225756 23812 ?        Ss   18:15   0:00 postgres: 16/main: checkpointer 
  postgres    1846  0.0  0.0 225628  7808 ?        Ss   18:15   0:00 postgres: 16/main: background writer 
  postgres    1847  0.0  0.0 225476 10348 ?        Ss   18:15   0:00 postgres: 16/main: walwriter 
  postgres    1848  0.0  0.0 227080  8520 ?        Ss   18:15   0:00 postgres: 16/main: autovacuum launcher 
  postgres    1849  0.0  0.0 227056  7976 ?        Ss   18:15   0:00 postgres: 16/main: logical replication launcher 
  root       52101  0.0  0.0  19860  7816 pts/2    S+   21:40   0:00 sudo -u postgres psql
  postgres   52110  0.0  0.0  26096  9296 pts/3    S+   21:41   0:00 /usr/lib/postgresql/16/bin/psql
  postgres   52115  0.0  0.1 228520 22944 ?        Ss   21:41   0:00 postgres: 16/main: postgres labdb [local] idle
  sourabh    52205  0.0  0.0   9156  2292 pts/0    S+   21:42   0:00 grep --color=auto postgres
  ```

  After exiting `psql` (using `\q`):

  ```bash
  postgres    1830  0.0  0.1 225476 30272 ?        Ss   18:15   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
  postgres    1845  0.0  0.1 225756 23812 ?        Ss   18:15   0:00 postgres: 16/main: checkpointer 
  postgres    1846  0.0  0.0 225628  7808 ?        Ss   18:15   0:00 postgres: 16/main: background writer 
  postgres    1847  0.0  0.0 225476 10348 ?        Ss   18:15   0:00 postgres: 16/main: walwriter 
  postgres    1848  0.0  0.0 227080  8520 ?        Ss   18:15   0:00 postgres: 16/main: autovacuum launcher 
  postgres    1849  0.0  0.0 227056  7976 ?        Ss   18:15   0:00 postgres: 16/main: logical replication launcher 
  sourabh    52215  0.0  0.0   9156  2300 pts/0    S+   21:44   0:00 grep --color=auto postgres
  ```

  This demonstrates that PostgreSQL is a dedicated background server process. The core engine and its background workers (walwriter, checkpointer, etc.) keep running continuously, completely independent of whether a client is connected.

---

# Key Takeaways

* **Storage Identity**: Databases ultimately map to actual files on the underlying OS filesystem.
* **Pagination**: Data management internally revolves around fixed-size pages (e.g., 4KB in SQLite, 8KB in Postgres).
* **Architecture Differences**: SQLite operates as a lightweight, embedded library. PostgreSQL acts as a robust, persistent background server.
* **Memory Management**: Mechanisms like `mmap` can map files to memory to optimize I/O, though benefits depend on OS caching.
* **OS Interactivity**: Executing even basic database queries triggers numerous low-level system calls.
* **Data Organization**: SQLite bundles all database objects (tables, indexes) into a single file, whereas PostgreSQL distributes relations across separate files within a directory.
* **Process Lifecycle**: PostgreSQL processes persistently run in the background, contrasting with SQLite's ephemeral execution linked to the calling application.
