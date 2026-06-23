# Scaler-Adv-DBMS

## Lab 2: SQLite3 v/s PSQL

This report gives a description on the comparisons between the working of SQLite3 and PSQL in a structured manner.

---

### 1. SQLite3

#### Setup

- OS: Windows 11 via WSL2 (Ubuntu)
- Installation Command: `sudo apt install sqlite3`
- Sample DB used: [Chinook_Sqlite.sqlite](https://github.com/lerocha/chinook-database/raw/master/ChinookDatabase/DataSources/Chinook_Sqlite.sqlite)

#### Commands Used

- `ls -lh Chinook_Sqlite.sqlite`
- `sqlite3 Chinook_Sqlite.sqlite`
- `PRAGMA page_size;`
- `PRAGMA page_count;`
- `PRAGMA mmap_size;`
- `PRAGMA mmap_size=268435456;`
- `time sqlite3 Chinook_Sqlite.sqlite "SELECT * FROM Track;" > /dev/null`
- `time sqlite3 -cmd "PRAGMA mmap_size=268435456;" Chinook_Sqlite.sqlite "SELECT * FROM Track;" > /dev/null`
- `ps aux | grep sqlite`

#### Observations
| Metric     | Value |
|------------|-------|
| File Size  | 984K  |
| Page Size  | 4096  |
| Page Count | 246   |

#### Query Timing
| Condition    | real     | user     | sys      |
|--------------|----------|----------|----------|
| Without mmap | 0m0.168s | 0m0.016s | 0m0.016s |
| With mmap    | 0m0.048s | 0m0.003s | 0m0.009s |

#### mmap Observations
A significant improvement of around 3.5x on an average 
was found on queries resulting in using mmap was observed 
on a dataset of 984 KB. This is a significant improvement 
on such a small dataset.

---

### 2. PostgreSQL

#### Setup
- Installed using: `sudo apt install postgresql`
- Database: labdb, Table: users (100,000 rows)

#### Commands Used
```sql
SHOW block_size;
SELECT relpages FROM pg_class WHERE relname = 'users';
SELECT pg_size_pretty(pg_database_size('labdb'));
\timing
SELECT * FROM users;
```

#### Observations
| Metric     | Value    |
|------------|----------|
| Block Size | 8192     |
| Page Count | 834      |
| DB Size    | 16 MB    |
| Query Time | 1.011 ms |

#### mmap in PostgreSQL
PostgreSQL does not expose a manual mmap_size control like SQLite3.
Instead, it uses `shared_buffers` for its buffer pool, and relies on 
the OS page cache for additional caching.

---

### 3. Comparison
| Parameter    | SQLite3              | PostgreSQL                       |
|--------------|----------------------|----------------------------------|
| Page Size    | 4096                 | 8192                             |
| Page Count   | 246                  | 834                              |
| Query Time   | 0m0.048s             | 0m0.172s                         |
| mmap Control | Manual via PRAGMA    | OS-managed / shared_buffers      |
| Architecture | Serverless, embedded | Client-server                    |
| Best For     | Local/embedded apps  | Multi-user, concurrent workloads |

---

### 4. Key Takeaways

- SQLite3 stores everything in a single file; PostgreSQL uses a managed
  data directory with many internal files.
- SQLite3's default page size is 4096 bytes; PostgreSQL uses 8192 bytes,
  suited for larger concurrent reads.
- mmap in SQLite3 can reduce read latency by avoiding repeated read()
  syscalls; the effect is minimal on small databases but significant at scale.
- PostgreSQL abstracts memory management away from the user, making it
  more suitable for production server environments.