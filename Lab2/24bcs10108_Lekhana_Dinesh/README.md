# Lab 2: SQLite3 and PostgreSQL Comparison

**Name:** Lekhana Dinesh  
**Roll No:** 24BCS10108  

---

## 1. Objective

The objective is to explore SQLite3 and PostgreSQL and compare them based on database size, page/block size, page count, query execution time, and the impact of `mmap_size` in SQLite3.

---

## 2. SQLite3 Exploration

### Commands Used

```bash
sqlite3 sample.db
```

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT
);

INSERT INTO users (name, email) VALUES
('Aarav', 'aarav@example.com'),
('Diya', 'diya@example.com'),
('Rohan', 'rohan@example.com'),
('Meera', 'meera@example.com');

.tables

PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;

.timer ON

PRAGMA mmap_size = 0;
SELECT * FROM users;

PRAGMA mmap_size = 268435456;
PRAGMA mmap_size;
SELECT * FROM users;
```

After exiting SQLite3, the database file size was checked using:

```powershell
dir sample.db
```

---

## 3. SQLite3 Observations

| Parameter | Observation |
|---|---|
| SQLite Version | 3.53.1 |
| Database File | sample.db |
| Database File Size | 8192 bytes |
| Page Size | 4096 bytes |
| Page Count | 2 |
| mmap Size Before Change | 0 |
| mmap Size After Change | 268435456 |
| Query Time Without mmap | 0.015015 seconds |
| Query Time With mmap | 0.008886 seconds |

### SQLite3 Output Summary

The `users` table was created with four sample records. The page size was 4096 bytes and the page count was 2. Initially, `mmap_size` was 0. After setting `mmap_size` to 268435456, the query execution time became slightly faster for this small dataset.

---

## 4. PostgreSQL Exploration

### Commands Used

```powershell
& "C:\Program Files\PostgreSQL\17\bin\psql.exe" -U postgres -h 127.0.0.1 -p 5433
```

```sql
DROP DATABASE IF EXISTS lab2db;

CREATE DATABASE lab2db;

\c lab2db

CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100),
    email VARCHAR(100)
);

INSERT INTO users (name, email) VALUES
('Aarav', 'aarav@example.com'),
('Diya', 'diya@example.com'),
('Rohan', 'rohan@example.com'),
('Meera', 'meera@example.com');

SELECT current_setting('block_size');

SELECT pg_database_size('lab2db');

\timing

SELECT * FROM users;
```

---

## 5. PostgreSQL Observations

| Parameter | Observation |
|---|---|
| PostgreSQL Version | 17.9 |
| Database Name | lab2db |
| Port Used | 5433 |
| Block Size | 8192 bytes |
| Database Size | 8189619 bytes |
| Query Execution Time | 3.566 ms |

### PostgreSQL Output Summary

The `users` table was created in the `lab2db` database with four sample records. The block size was 8192 bytes. The database size was 8189619 bytes, and the query execution time for selecting all records from the `users` table was 3.566 ms.

---

## 6. SQLite3 vs PostgreSQL Comparison

| Parameter | SQLite3 | PostgreSQL |
|---|---|---|
| Type | Embedded database | Server-based database |
| Setup | Simple and lightweight | Requires PostgreSQL server setup |
| Storage | Stores data in a single `.db` file | Stores data in PostgreSQL-managed database files |
| Database/File Size | 8192 bytes | 8189619 bytes |
| Page/Block Size | 4096 bytes | 8192 bytes |
| Page Count | 2 | Not checked in the same direct way as SQLite |
| Query Time | 0.015015 seconds without mmap, 0.008886 seconds with mmap | 3.566 ms |
| mmap Support | Can be controlled using `PRAGMA mmap_size` | Not controlled using SQLite-style `mmap_size` |
| Best Use Case | Small apps, local storage, testing | Larger applications, multi-user systems, production use |

---

## 7. Analysis

SQLite3 is very simple to use because it does not require a separate database server. It stores the whole database in a single file, which makes it useful for small applications, testing, and local storage.

PostgreSQL is more powerful and runs as a proper database server. It is better suited for larger applications where multiple users may access the database at the same time. It also provides stronger features for reliability, security, and scalability.

In this experiment, SQLite3 used a page size of 4096 bytes, while PostgreSQL used a block size of 8192 bytes. SQLite3 had a very small database file size because the database contained only one small table with four records. PostgreSQL had a larger database size because it maintains additional internal database structures.

The `mmap_size` setting in SQLite3 showed a small improvement in query time. Without mmap, the query took 0.015015 seconds. After enabling mmap, the query took 0.008886 seconds. Since the dataset was very small, the difference was minor, but it still showed that memory-mapped I/O can affect query performance.

PostgreSQL does not use `PRAGMA mmap_size` like SQLite3. It handles memory and disk access through its own internal database engine and buffer management system.

---

## 8. Conclusion

SQLite3 is best for lightweight and local database usage because it is easy to set up and stores everything in a single file. PostgreSQL is better for production-level applications because it supports server-based access, better concurrency, and advanced database features.


