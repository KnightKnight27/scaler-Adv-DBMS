# Lab Assignment: DBMS Performance Analysis

**Name:** Siddhanth Kapoor  
**Roll Number:** 10154

## 1. SQLite3 Exploration

For this task, I created a sample database named `sample.db` and populated it with a `users` table containing 1,000,000 records.

### Commands Used
```bash
# Check file size
ls -lh sample.db

# Find page size and count
sqlite3 sample.db "PRAGMA page_size;"
sqlite3 sample.db "PRAGMA page_count;"

# Check memory map size
sqlite3 sample.db "PRAGMA mmap_size;"

# Time queries with and without mmap
time sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM users;" > /dev/null
time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;" > /dev/null

# Track background process
ps aux | grep sqlite
```

### Observations
* **File Size:** `ls -lh` showed the file size as 43 MB.
* **Page Size:** 4096 bytes (4 KB).
* **Page Count:** 10894 (10894 * 4096 = 44,621,824 bytes, matching the file size).
* **Execution Time (Without mmap):** `0.392 seconds`
* **Execution Time (With mmap):** `0.344 seconds`
* **mmap Impact:** Changing the `mmap_size` to 256MB (`268435456`) reduced the query execution time slightly by avoiding standard `read()` system calls and letting the OS map the file directly into memory. The process list (`ps aux | grep sqlite`) showed `sqlite3` using more virtual memory while the query executed.

---

## 2. PostgreSQL Exploration

I set up PostgreSQL, created a database named `mydb`, and copied the exact same 1,000,000 records into a `users` table.

### Commands Used
```sql
-- Inside psql
SHOW block_size;
VACUUM ANALYZE users;
SELECT relpages FROM pg_class WHERE relname = 'users';
```

```bash
# Time query
time psql mydb -c "SELECT * FROM users;" > /dev/null
```

### Observations
* **Page Size (Block Size):** 8192 bytes (8 KB).
* **Page Count:** 9360.
* **Table Size:** Approx. 73 MB.
* **Execution Time:** `2.509 seconds`

---

## 3. Comparison Analysis

| Metric | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| **Page Size** | 4096 bytes (4 KB) | 8192 bytes (8 KB) |
| **Page Count** | 10894 | 9360 |
| **Total Size** | ~43 MB | ~73 MB |
| **Query execution time**| ~0.35s | ~2.51s |

**Analysis:**
1. **Storage Footprint:** PostgreSQL has a larger page size (8 KB vs 4 KB) and requires more storage for the exact same dataset (73 MB vs 43 MB). This is because Postgres stores additional tuple headers, MVCC metadata (like `xmin`, `xmax`), and uses a different page layout compared to SQLite's highly compact format.
2. **Execution Time:** The SQLite query ran much faster in this specific local test. Since SQLite is an embedded database, it runs in the same process space as the querying application. PostgreSQL, being a client-server architecture, has networking/socket overhead, connection setup time, and IPC overhead when running `psql`, making a raw full-table dump slower locally.
3. **mmap Impact:** In SQLite, enabling memory mapping drastically improves read performance by bypassing the OS buffer cache overhead and reading directly from memory. PostgreSQL does not use an `mmap_size` equivalent tunable in the same way; instead, it relies heavily on its own `shared_buffers` architecture and the OS page cache for performance.
