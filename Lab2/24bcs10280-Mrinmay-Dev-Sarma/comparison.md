# Lab 2: DBMS Storage & Performance Analysis
**Name:** Mrinmay Dev Sarma
**Roll Number:** 24BCS10280

## 1. SQLite3 Observations
- **Page Size:** 4096 bytes 
- **Page Count:** 506;
- **mmap_size Impact:** - *Initial Observation:* Queries using mmap showed lighter CPU usage (88% vs 84%) but maintaining same total wall-clock time.
  - *Timing (No mmap):* 0.030s
  - *Timing (With mmap):* 0.030s
- *File Size (ls -lh):* 2.0M 
## 2. PostgreSQL Observations
- **Block (Page) Size:** 8192 bytes 
- **Page Count:**  637
- **Execution Time:** 12.536ms (for `SELECT COUNT(*) FROM users;`)

## 3. Comparison Analysis
| Metric | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| **Default Page Size** | 4KB | 8KB |
| **Memory Mapping** | via mmap | Managed by OS Buffer Cache |


## 4. Commands Used

#### SQLite3 Commands

```
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size = 268435456;
-- Terminal timing:
-- time sqlite3 lab2_test.db "SELECT * FROM users;" > /dev/null
```

#### PostgreSQL Commands

```
SHOW block_size;
SELECT relpages FROM pg_class WHERE relname = 'users';
ANALYZE users;
\timing on
SELECT COUNT(*) FROM users;
```
