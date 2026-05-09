# Database Exploration and Comparison: SQLite3 vs PostgreSQL

Using the lecture steps and ideas, I ran a set of experiments to look into the internal behavior of SQLite3 and PostgreSQL. Below are my observations along with the commands I executed.

---

## 1. SQLite3 Exploration

I started by creating a fresh database and a `users` table. The database file initially measured 12.0K.

```bash
-rwxr-xr-x 1 juhil root 12.0K May  9 14:45 sample.db

```

_Observation:_ The database is just a regular OS file. This highlights SQLite's core model: schema and data are packaged into one file.

Next, I inserted 12,000 rows into the table. A second size check showed it had grown to 244K:

```bash
-rwxr-xr-x 1 juhil root 244K May  9 14:46 sample.db

```

_Observation:_ As rows accumulate, the file expands on demand. The engine allocates new storage inside the same file.

To understand the storage layout, I queried the page size:

```sql
PRAGMA page_size;

```

_Result:_ `4096`
_Observation:_ SQLite organizes data into fixed pages of 4KB. The engine reads and writes at the page level rather than at arbitrary byte offsets.

I then checked the total page count:

```sql
PRAGMA page_count;

```

_Result:_ `61`
_Observation:_ The math works out: 244KB divided by 4KB per page is 61 pages. That matches the file size, so the file is effectively a sequence of 4KB blocks.

To explore memory mapping, I first checked the default setting:

```sql
PRAGMA mmap_size;

```

_Result:_ `0` (Memory mapping is disabled by default).

I timed a basic query (`SELECT * FROM users;`) with memory mapping disabled:

```bash
real    0m0.082s
user    0m0.016s
sys     0m0.028s

```

I then raised `mmap_size` to 50MB (52428800 bytes) so SQLite could map the file directly into memory, avoiding regular read/write calls. Running the same query produced:

```bash
real    0m0.098s
user    0m0.008s
sys     0m0.045s

```

_Observation:_ The runtime did not improve and was slightly slower. For small datasets, Linux's page cache is already efficient, and mmap setup overhead can outweigh benefits for tiny workloads.

To observe the process lifecycle, I ran the database in one terminal and looked for its process from another:

```bash
juhil    52134  0.0  0.0  12056  5424 pts/0    S+   14:50   0:00 sqlite3 sample.db
juhil    52145  0.0  0.0   9156  2296 pts/2    S+   14:51   0:00 grep --color=auto sqlite

```

After exiting the SQLite prompt, the process vanished:

```bash
juhil    52160  0.0  0.0   9156  2300 pts/2    S+   14:52   0:00 grep --color=auto sqlite

```

_Observation:_ This clearly shows the embedded model. There is no background server or daemon; the engine runs only inside the calling app.

To dig deeper, I used `strace` to watch system calls (`strace sqlite3 sample.db`). The output is long, but a small snippet shows the key OS interactions:

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

_Observation:_ Even an embedded DB does a lot of OS work: opening files (`openat`), allocating memory (`mmap`), and reading libraries (`read`).

I checked the database file's inode:

```bash
105482 sample.db

```

To test table storage, I created a second table (`products`) and inserted a few rows.

```bash
-rwxr-xr-x 1 juhil root 248K May  9 15:05 sample.db

```

Checking the inode again returned `105482`.
_Observation:_ The file grew a bit, but no new file appeared. The inode stayed the same. SQLite keeps multiple tables in the same physical file.

---

## 2. PostgreSQL Setup and Exploration

Moving to PostgreSQL, I repeated the setup: create a database and a `users` table, then insert 12,000 rows.

With `\timing` enabled, I ran `SELECT * FROM users;`:

```text
Time: 5.042 ms

```

_Observation:_ PostgreSQL returns rows very quickly, relying on aggressive internal caching.

I then queried the server's internal block size:

```sql
SHOW block_size;

```

_Result:_ `8192`
_Observation:_ PostgreSQL uses 8KB blocks, unlike SQLite's 4KB pages.

To estimate how many blocks the `users` table occupies, I queried the system catalogs:

```sql
SELECT pg_relation_size('users') / 8192 AS approx_pages;

```

```text
 approx_pages
--------------
           78

```

_Observation:_ This gives an approximate storage footprint for the specific relation (table).

To check memory configuration, I inspected shared buffers:

```sql
SHOW shared_buffers;

```

_Result:_ `128MB`
_Observation:_ PostgreSQL relies on shared memory to cache blocks, a more complex strategy than a simple `mmap_size` switch.

Next, I looked at how PostgreSQL stores multiple tables on disk. First, I found the file path for `users`:

```sql
SELECT pg_relation_filepath('users');

```

_Result:_ `base/16388/16405`

Then, I created a `products` table and checked its path:

```sql
SELECT pg_relation_filepath('products');

```

_Result:_ `base/16388/16412`

_Observation:_ Both tables live under the same database directory (`base/16388`), but each table is stored as a separate file. This is a core difference from SQLite.

Finally, I examined background processes. With the PostgreSQL prompt active, I checked system processes:

```bash
postgres    2175  0.0  0.1 225756 23812 ?        Ss   12:55   0:00 postgres: 16/main: checkpointer
postgres    2176  0.0  0.0 225628  7808 ?        Ss   12:55   0:00 postgres: 16/main: background writer
postgres    2182  0.0  0.0 225476 10348 ?        Ss   12:55   0:00 postgres: 16/main: walwriter
postgres    2183  0.0  0.0 227080  8520 ?        Ss   12:55   0:00 postgres: 16/main: autovacuum launcher
postgres    2184  0.0  0.0 227056  7976 ?        Ss   12:55   0:00 postgres: 16/main: logical replication launcher
postgres   60124  0.0  0.0  26096  9296 pts/3    S+   15:15   0:00 /usr/lib/postgresql/16/bin/psql
postgres   60130  0.0  0.1 228520 22944 ?        Ss   15:15   0:00 postgres: 16/main: postgres labdb [local] idle
juhil      61285  0.0  0.0   9156  2292 pts/0    S+   15:16   0:00 grep --color=auto postgres

```

After leaving the `psql` shell using `\q`, I checked processes again:

```bash
postgres    2164  0.0  0.1 225476 30272 ?        Ss   12:55   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
postgres    2175  0.0  0.1 225756 23812 ?        Ss   12:55   0:00 postgres: 16/main: checkpointer
postgres    2176  0.0  0.0 225628  7808 ?        Ss   12:55   0:00 postgres: 16/main: background writer
postgres    2182  0.0  0.0 225476 10348 ?        Ss   12:55   0:00 postgres: 16/main: walwriter
postgres    2183  0.0  0.0 227080  8520 ?        Ss   12:55   0:00 postgres: 16/main: autovacuum launcher
juhil      61295  0.0  0.0   9156  2300 pts/0    S+   15:17   0:00 grep --color=auto postgres

```

_Observation:_ The database server and helper processes (checkpointer, walwriter, autovacuum) keep running independently of the user session. PostgreSQL is a client-server system, not an embedded library.

---

## 3. Comparison Analysis & Key Takeaways

Based on the experiments above, here is a concise summary of key differences and takeaways:

- **File Architecture:** At the lowest level, all databases rely on the OS file system. SQLite packs an entire database (multiple tables) into one file, while PostgreSQL spreads tables across multiple files inside a structured directory.
- **Storage Pagination:** Both engines split data into fixed-size blocks for I/O efficiency. SQLite defaults to 4KB pages, while PostgreSQL uses 8KB blocks.
- **Operational Paradigm:** SQLite is embedded and runs inside the host process, ending when the app exits. PostgreSQL follows a client-server model and keeps background daemons running for connections, WAL, and caching.
- **Memory Management:** SQLite provides straightforward memory mapping (`mmap_size`) to load data into memory, with benefits tied to dataset size and OS caching. PostgreSQL maintains a dedicated shared memory area (`shared_buffers`) optimized for database workloads.
- **System Integration:** Even a lightweight tool like SQLite makes heavy OS calls (e.g., `openat`, `mmap`, `read`), underscoring how tightly storage engines depend on the operating system.
