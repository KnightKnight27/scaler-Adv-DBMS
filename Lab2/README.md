# SQLITE3 DATABASE EXPLORATION

> This laboratory exercise involved working with SQLite3. I utilized the commands demonstrated in the lecture by the instructor.

---

* Initially, I created a database with a users table, resulting in a file size of 8.0K

  ```
  -rwxr-xr-x 1 shreyasreddy root 8.0K May  9 12:10 sample.db
  ```

  The database exists as a standard file in the filesystem. This was fascinating because the entire database is essentially stored within this single file.

---

* Subsequently, I inserted 10,000 rows, increasing the size to 196K

  ```
  -rwxr-xr-x 1 shreyasreddy root 196K May  9 12:10 sample.db
  ```

  With additional data, the database file expanded automatically. This demonstrated how the database allocates more pages internally to accommodate new data within the same file.

---

* `PRAGMA page_size` returned 4096, indicating a 4KB page size.

  SQLite organizes the database into fixed 4KB pages. Rather than processing data byte-by-byte, it operates page-by-page internally.

---

* `PRAGMA page_count` returned 49, meaning 49 pages after adding 10,000 rows. Notably, total_size/page_size (196/4) equals 49 exactly!

  This correlation was remarkable and confirmed that the database file is internally structured into pages.

---

* `PRAGMA mmap_size` returned 0, indicating it was currently disabled.

  Memory mapping was not active initially.

---

* I then configured mmap_size to 30MB to enable memory mapping.

  Memory mapping allows SQLite to directly map the database file into memory, avoiding redundant copying operations.

---

* I reset mmap to 0, then timed the `select * from users;` query with these results:

  ```
  real    0m0.070s
  user    0m0.014s
  sys     0m0.023s
  ```

---

* I repeated the test with mmap_size set to 30MB, yielding these results:

  ```
  real    0m0.091s
  user    0m0.006s
  sys     0m0.040s
  ```

  Interestingly, memory mapping did not provide faster performance in this case. I discovered that memory mapping is not always guaranteed to enhance performance. Given the small dataset and Linux's built-in page caching, the performance difference was minimal.

---

* I launched `sqlite3 sample.db` in one terminal and checked its process in another:

  ```
  shreyasreddy   47121  0.0  0.0  12056  5424 pts/0    S+   16:47   0:00 sqlite3 sample.db
  shreyasreddy   47693  0.0  0.0   9156  2296 pts/2    S+   16:49   0:00 grep --color=auto sqlite
  ```

  After terminating it, the process disappeared:

  ```
  shreyasreddy   47907  0.0  0.0   9156  2300 pts/2    S+   16:49   0:00 grep --color=auto sqlite
  ```

  This illustrated that SQLite functions as an embedded database. It does not maintain a persistent server process. When the sqlite3 program terminates, the process completely vanishes.

---

* To examine system calls, I executed `strace sqlite3 sample.db`, which produced extensive output. While most details were incomprehensible, here's a sample:

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

  Despite not understanding every system call, I identified key operations:

  * `openat()`
  * `read()`
  * `mmap()`
  * `close()`

  This revealed that even basic SQLite operations involve numerous low-level OS interactions.

---

* I verified the inode number of sample.db:

  ```
  104371 sample.db
  ```

  The inode number serves as the file's internal identifier within Linux.

---

* As SQLite is an embedded database, creating additional tables does not generate new files.

  I executed this command:

  ```
  CREATE TABLE products (
  id INTEGER PRIMARY KEY,
  price INT
  );
  ```

  After inserting some rows, I checked for new file creation.

  ```
  -rwxr-xr-x 1 shreyasreddy root 200K May  9 17:17 sample.db
  ```

  No additional file was created; sample.db grew by only 4KB, maintaining the same inode number:

  ```
  104371 sample.db
  ```

  This demonstrated that SQLite consolidates multiple tables within a single database file rather than creating separate files.

---

# POSTGRESQL DATABASE EXPLORATION

> Now proceeding to PostgreSQL.

---

* I replicated the SQLite procedures: created a database and users table, then inserted 10,000 rows.

---

* Enabled query timing with `\timing`.

---

* Executed `select * from users;` with these results:

  ```
  Time: 4.193 ms
  ```

  PostgreSQL demonstrated excellent performance even with substantial data volumes.

---

* `SHOW block_size;` returned 8192, indicating an 8KB block size.

  PostgreSQL internally manages data in 8KB blocks/pages.

---

* Calculating page count in PostgreSQL is more complex due to its internal storage management:

  ```
  SELECT pg_relation_size('users') / 8192 AS approx_pages;
  ```

  Result:

  ```
   approx_pages
  --------------
              64
  ```

  This provided an estimate of the internal pages occupied by the table.

---

---

* To examine memory buffering, I checked `SHOW shared_buffers;` which returned 128MB.

  PostgreSQL maintains a shared memory buffer for caching pages in RAM. Unlike SQLite, it doesn't expose mmap_size in the same straightforward manner.

---

* Similar to SQLite, I investigated whether creating another table generates a separate file.

  First, I determined the directory location of the users table:

  ```
  SELECT pg_relation_filepath('users');
  ```

  Result:

  ```
  base/16388/16390
  ```

  Then I created a products table and checked its location:

  ```
  SELECT pg_relation_filepath('products');
  ```

  Result:

  ```
  base/16388/16399
  ```

  Both tables reside in the same directory but are distinct files.

  This contrasts sharply with SQLite. PostgreSQL maintains tables as separate files internally, whereas SQLite consolidates everything into one database file.

---

* I examined whether PostgreSQL processes persist like SQLite or not.

  Running `ps aux | grep postgres` showed:

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
  shreyasreddy   61282  0.0  0.0   9156  2292 pts/0    S+   17:32   0:00 grep --color=auto postgres
  ```

  After exiting psql with `\q`, PostgreSQL processes continued running:

  ```
  postgres    2164  0.0  0.1 225476 30272 ?        Ss   12:25   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
  postgres    2175  0.0  0.1 225756 23812 ?        Ss   12:25   0:00 postgres: 16/main: checkpointer
  postgres    2176  0.0  0.0 225628  7808 ?        Ss   12:25   0:00 postgres: 16/main: background writer
  postgres    2182  0.0  0.0 225476 10348 ?        Ss   12:25   0:00 postgres: 16/main: walwriter
  postgres    2183  0.0  0.0 227080  8520 ?        Ss   12:25   0:00 postgres: 16/main: autovacuum launcher
  postgres    2184  0.0  0.0 227056  7976 ?        Ss   12:25   0:00 postgres: 16/main: logical replication launcher
  shreyasreddy   61659  0.0  0.0   9156  2300 pts/0    S+   17:34   0:00 grep --color=auto postgres
  ```

  This highlighted the fundamental difference: PostgreSQL operates as a full database server with persistent background processes, unlike SQLite.

---

# KEY INSIGHTS GAINED

* Databases are essentially files stored within the filesystem.
* Data is internally organized into fixed-size pages.
* SQLite is lightweight and embedded in nature.
* PostgreSQL is more resource-intensive but offers superior internal capabilities.
* Memory mapping directly loads database files into RAM.
* Even basic database operations involve numerous low-level system calls.
* SQLite consolidates multiple tables within a single database file.
* PostgreSQL maintains tables as separate files internally.
* PostgreSQL maintains background processes even after client disconnection.
* Database storage engines are intricately connected to operating system and memory management.

---

# STUDENT INFORMATION
**Roll Number:** 24bcs10152
**Name:** Shreyas Reddy
