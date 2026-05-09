# Database Exploration and Comparison: SQLite3 vs PostgreSQL

Following the instructions and concepts discussed in the lecture, I conducted a series of experiments to observe the internal workings of SQLite3 and PostgreSQL. Below are my detailed observations and the corresponding commands used.

---

## 1. SQLite3 Exploration

I began by creating a new database and a `users` table. The initial size of the database was 12.0K.

```bash
-rwxr-xr-x 1 tirth root 12.0K May  9 14:15 sample.db

```

*Observation:* The database exists purely as a standard file on the operating system. This illustrates the core concept of SQLite: the entire database schema and data are encapsulated within a single file.

Next, I inserted 12,000 rows into the table. Checking the file size again showed an increase to 244K:

```bash
-rwxr-xr-x 1 tirth root 244K May  9 14:16 sample.db

```

*Observation:* As records are added, the file size grows dynamically. The database manager handles the allocation of new storage space within this single file.

To understand how this space is structured, I queried the page size:

```sql
PRAGMA page_size;

```

*Result:* `4096`
*Observation:* SQLite manages data in discrete chunks called pages, which are 4KB in size. Instead of continuous byte-level operations, the engine reads and writes these fixed-size pages.

I then checked the total number of pages:

```sql
PRAGMA page_count;

```

*Result:* `61`
*Observation:* Doing the math, 244KB divided by 4KB per page equals exactly 61 pages. This perfectly correlates with the file size, confirming that the entire file is essentially a collection of these 4KB blocks.

To experiment with memory mapping, I first checked the default state:

```sql
PRAGMA mmap_size;

```

*Result:* `0` (Memory mapping is disabled by default).

I timed a basic query (`SELECT * FROM users;`) without memory mapping:

```bash
real    0m0.082s
user    0m0.016s
sys     0m0.028s

```

I then increased the `mmap_size` to 50MB (52428800 bytes) to allow SQLite to map the file directly into memory, aiming to bypass standard read/write system calls. Running the same query yielded:

```bash
real    0m0.098s
user    0m0.008s
sys     0m0.045s

```

*Observation:* The execution time did not improve and was slightly slower. For small datasets, Linux's built-in page cache is already highly efficient. The overhead of setting up memory mapping can sometimes outweigh the benefits for trivial queries on small files.

To observe the process lifecycle, I ran the database in one terminal and searched for its process in another:

```bash
tirth    52134  0.0  0.0  12056  5424 pts/0    S+   14:20   0:00 sqlite3 sample.db
tirth    52145  0.0  0.0   9156  2296 pts/2    S+   14:21   0:00 grep --color=auto sqlite

```

After exiting the SQLite prompt, the process disappeared entirely:

```bash
tirth    52160  0.0  0.0   9156  2300 pts/2    S+   14:22   0:00 grep --color=auto sqlite

```

*Observation:* This perfectly demonstrates an embedded database. There is no background server or daemon; the database engine runs only as part of the calling application.

To look deeper, I used `strace` to monitor system calls (`strace sqlite3 sample.db`). While the output is extensive, a snippet shows the critical OS-level interactions:

```bash
brk(NULL)                               = 0x55df8b385000
mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7fca2d28e000
access("/etc/ld.so.preload", R_OK)      = -1 ENOENT (No such file or directory)
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=81467, ...}) = 0
mmap(NULL, 81467, PROT_READ, MAP_PRIVATE, 3, 0) = 0x7fca2d27a000
close(3)                                = 0
openat(AT_FDCWD, "/lib/x86_64-linux-gnu/libsqlite3.so.0", O_RDONLY|O_CLOEXEC) = 3
read(3, "\177ELF\2\1\1\0\0\0\0\0\0\0\0\0\3\0>\0\1\0\0\0\0\0\0\0\0\0\0\0"..., 832) = 832

```

*Observation:* Initializing an embedded database still requires significant OS interaction, including file opening (`openat`), memory allocation (`mmap`), and reading library files (`read`).

I checked the inode of the database file:

```bash
105482 sample.db

```

To test table storage, I created a second table (`products`) and inserted a few rows.

```bash
-rwxr-xr-x 1 tirth root 248K May  9 14:35 sample.db

```

Checking the inode again returned `105482`.
*Observation:* The file size increased slightly, but no new file was created. The inode remained unchanged. SQLite stores multiple tables within the identical physical file.

---

## 2. PostgreSQL Setup and Exploration

Moving to PostgreSQL, I replicated the setup by creating a database and a `users` table, then inserting 12,000 rows.

Using `\timing` to monitor query performance, I ran `SELECT * FROM users;`:

```text
Time: 5.042 ms

```

*Observation:* PostgreSQL handles data retrieval exceptionally fast, utilizing highly optimized internal caching mechanisms.

I then queried the internal block size:

```sql
SHOW block_size;

```

*Result:* `8192`
*Observation:* Unlike SQLite's 4KB pages, PostgreSQL operates on an 8KB block size architecture.

Calculating the specific number of pages occupied by the `users` table required querying the internal system catalogs:

```sql
SELECT pg_relation_size('users') / 8192 AS approx_pages;

```

```text
 approx_pages 
--------------
           78

```

*Observation:* This provides an estimate of the internal storage footprint of the specific relation (table).

To check memory configuration, I looked at the shared buffers:

```sql
SHOW shared_buffers;

```

*Result:* `128MB`
*Observation:* PostgreSQL relies heavily on shared memory for caching data blocks, operating much more complex memory management than a simple `mmap_size` toggle.

Next, I investigated how PostgreSQL handles multiple tables on the disk. First, I found the file path for the `users` table:

```sql
SELECT pg_relation_filepath('users');

```

*Result:* `base/16388/16405`

Then, I created a `products` table and checked its path:

```sql
SELECT pg_relation_filepath('products');

```

*Result:* `base/16388/16412`

*Observation:* Both tables exist within the same database directory (`base/16388`), but they are written as entirely separate physical files on the disk. This is a fundamental architectural difference from SQLite.

Finally, I analyzed the background processes. With the PostgreSQL prompt active, I checked the system processes:

```bash
postgres    2175  0.0  0.1 225756 23812 ?        Ss   12:25   0:00 postgres: 16/main: checkpointer 
postgres    2176  0.0  0.0 225628  7808 ?        Ss   12:25   0:00 postgres: 16/main: background writer 
postgres    2182  0.0  0.0 225476 10348 ?        Ss   12:25   0:00 postgres: 16/main: walwriter 
postgres    2183  0.0  0.0 227080  8520 ?        Ss   12:25   0:00 postgres: 16/main: autovacuum launcher 
postgres    2184  0.0  0.0 227056  7976 ?        Ss   12:25   0:00 postgres: 16/main: logical replication launcher 
postgres   60124  0.0  0.0  26096  9296 pts/3    S+   14:45   0:00 /usr/lib/postgresql/16/bin/psql
postgres   60130  0.0  0.1 228520 22944 ?        Ss   14:45   0:00 postgres: 16/main: postgres labdb [local] idle
tirth      61285  0.0  0.0   9156  2292 pts/0    S+   14:46   0:00 grep --color=auto postgres

```

After exiting the `psql` shell using `\q`, I ran the process check again:

```bash
postgres    2164  0.0  0.1 225476 30272 ?        Ss   12:25   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
postgres    2175  0.0  0.1 225756 23812 ?        Ss   12:25   0:00 postgres: 16/main: checkpointer 
postgres    2176  0.0  0.0 225628  7808 ?        Ss   12:25   0:00 postgres: 16/main: background writer 
postgres    2182  0.0  0.0 225476 10348 ?        Ss   12:25   0:00 postgres: 16/main: walwriter 
postgres    2183  0.0  0.0 227080  8520 ?        Ss   12:25   0:00 postgres: 16/main: autovacuum launcher 
tirth      61295  0.0  0.0   9156  2300 pts/0    S+   14:47   0:00 grep --color=auto postgres

```

*Observation:* The background database server and its helper processes (checkpointer, walwriter, autovacuum) continue to run independently of the user session. PostgreSQL is a client-server application, not an embedded library.

---

## 3. Comparison Analysis & Key Takeaways

Based on the experiments above, here is a summary of the core differences and learnings:

* **File Architecture:** At the lowest level, all databases rely on the operating system's file system. However, SQLite encapsulates an entire database (multiple tables) into a single file, whereas PostgreSQL distributes tables across multiple files within an organized directory structure.
* **Storage Pagination:** Both engines divide data into fixed-size segments to optimize disk I/O. SQLite defaults to a 4KB page size, while PostgreSQL utilizes an 8KB block size.
* **Operational Paradigm:** SQLite is strictly an embedded engine. It spins up within the host application's process and vanishes when the application exits. PostgreSQL operates on a client-server model, requiring continuous background daemons to manage connections, write-ahead logging, and caching.
* **Memory Management:** SQLite offers simple memory mapping (`mmap_size`) to load data directly into memory, though performance gains depend heavily on dataset size and OS caching. PostgreSQL manages a complex, dedicated chunk of RAM (`shared_buffers`) optimized explicitly for database operations.
* **System Integration:** Even lightweight tools like SQLite require substantial OS-level interactions (system calls like `openat`, `mmap`, `read`) to function, highlighting the deep relationship between database storage engines and operating system architecture.