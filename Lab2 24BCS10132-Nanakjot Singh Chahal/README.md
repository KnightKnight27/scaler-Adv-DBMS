# SQLite3 vs PostgreSQL — Lab Report

**Name:** Nanakjot Singh Chahal
**Roll Number:** 24BCS10132

---

## 1. SQLite3 Exploration

### Installation
```bash
sudo apt update && sudo apt install sqlite3
wget https://github.com/lerocha/chinook-database/raw/master/ChinookDatabase/DataSources/Chinook_Sqlite.sqlite
```

### Commands Used

```bash
ls -lh Chinook_Sqlite.sqlite
```

```sql
sqlite3 Chinook_Sqlite.sqlite

PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
PRAGMA mmap_size=268435456;
PRAGMA mmap_size;

-- Without mmap
PRAGMA mmap_size=0;
.timer on
SELECT * FROM Track;

-- With mmap
PRAGMA mmap_size=268435456;
.timer on
SELECT * FROM Track;
```

```bash
ps aux | grep sqlite
```

### Observations

- File size: 3.2 MB
- Page size: 4096 bytes
- Page count: 812
- Default mmap_size: 0
- mmap_size after change: 268435456 (256 MB)
- Query time WITHOUT mmap: 3.21 ms
- Query time WITH mmap: 1.84 ms
- mmap reduced query time by approximately 43%

---

## 2. PostgreSQL Setup & Exploration

### Installation

```bash
sudo apt install postgresql postgresql-contrib
sudo service postgresql start
sudo -u postgres psql
```

### Commands Used

```sql
CREATE DATABASE labdb;
\c labdb

CREATE TABLE users (
  id SERIAL PRIMARY KEY,
  name TEXT,
  email TEXT
);

INSERT INTO users (name, email)
SELECT 'User' || i, 'user' || i || '@test.com'
FROM generate_series(1, 10000) AS i;

SHOW block_size;

SELECT relpages FROM pg_class WHERE relname = 'users';

\timing on
SELECT * FROM users;

SHOW shared_buffers;
```

### Observations

- Block (page) size: 8192 bytes
- Page count for users table: 94
- Query time: 22.3 ms
- shared_buffers: 128 MB
- PostgreSQL uses shared_buffers as its memory caching mechanism

---

## 3. Comparison Report

### Page Size

| Database   | Page / Block Size |
|------------|-------------------|
| SQLite3    | 4096 bytes        |
| PostgreSQL | 8192 bytes        |

- SQLite3 uses a default page size of 4096 bytes
- PostgreSQL uses a larger block size of 8192 bytes
- Larger block size in PostgreSQL allows more data per read, better for large datasets

---

### Page Count

| Database   | Table / File     | Page Count |
|------------|------------------|------------|
| SQLite3    | Chinook_Sqlite   | 812        |
| PostgreSQL | users (10k rows) | 94         |

- SQLite3 page count reflects the entire database file
- PostgreSQL page count is per table and depends on block size and row size

---

### Query Performance

| Database   | Condition       | Query Time |
|------------|-----------------|------------|
| SQLite3    | Without mmap    | 3.21 ms    |
| SQLite3    | With mmap       | 1.84 ms    |
| PostgreSQL | With shared_buffers | 22.3 ms |

- SQLite3 is faster for simple local queries on smaller datasets
- PostgreSQL overhead comes from its client-server architecture
- For concurrent and large-scale workloads, PostgreSQL performs better overall

---

### mmap Impact

| Database   | mmap Mechanism  | Effect |
|------------|-----------------|--------|
| SQLite3    | mmap_size PRAGMA | Reduced query time by ~43% |
| PostgreSQL | shared_buffers  | Caches frequently accessed blocks in RAM |

- SQLite3 allows direct mmap configuration via PRAGMA
- Setting mmap_size=0 disables memory mapping — all reads go to disk
- Setting mmap_size=268435456 maps 256MB of the database file into memory
- PostgreSQL does not expose mmap directly; shared_buffers serves the same purpose
- Both mechanisms reduce disk I/O and improve read performance

---

## 4. Analysis & Conclusion

| Parameter        | SQLite3              | PostgreSQL            |
|------------------|----------------------|-----------------------|
| Type             | File-based           | Client-Server         |
| Page Size        | 4096 bytes           | 8192 bytes            |
| Page Count       | 812                  | 94                    |
| Query Time       | 1.84 ms (with mmap)  | 22.3 ms               |
| Memory Mapping   | mmap_size PRAGMA     | shared_buffers        |
| Best Use Case    | Local / lightweight  | Concurrent / large-scale |

- SQLite3 is ideal for lightweight, single-user, local applications
- PostgreSQL is designed for production, multi-user, large-scale environments
- mmap in SQLite3 significantly improves performance by reducing disk reads
- PostgreSQL manages memory automatically via shared_buffers
- For this lab's dataset size, SQLite3 outperformed PostgreSQL due to lower overhead
- At scale with concurrent users, PostgreSQL would be the clear winner