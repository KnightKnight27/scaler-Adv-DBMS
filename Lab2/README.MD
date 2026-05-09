## Student Details

**Name:** Aparna Singha  
**Roll No:** 24BCS10353  
**Course:** Advanced DBMS  
**Submission:** Lab 1 

---

# Lab 2: SQLite3 and PostgreSQL Comparison

## 1. Objective

The aim of this lab is to compare SQLite3 and PostgreSQL by creating a small database in both systems and observing their storage and execution characteristics. The comparison focuses on database size, page or block size, page count, query execution time, and the effect of SQLite's `mmap_size` setting.

SQLite3 is an embedded database that stores everything in a single local file, while PostgreSQL is a server-based relational database system designed for larger and concurrent applications.

---

## 2. SQLite3 Experiment

### 2.1 Starting SQLite3

```bash
sqlite3 sample.db
```

### 2.2 SQL Commands Used

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

### 2.3 Checking SQLite Database File Size

Linux/macOS:

```bash
ls -lh sample.db
```

Windows PowerShell:

```powershell
dir sample.db
```

---

## 3. SQLite3 Observations

| Parameter | Observation |
|---|---:|
| SQLite Version | 3.53.1 |
| Database File | `sample.db` |
| Database File Size | 8192 bytes |
| Page Size | 4096 bytes |
| Page Count | 2 |
| mmap Size Before Change | 0 |
| mmap Size After Change | 268435456 |
| Query Time Without mmap | 0.015015 seconds |
| Query Time With mmap | 0.008886 seconds |

### SQLite3 Summary

A `users` table was created with four records. The database file was very small because SQLite stores the complete database in a single `.db` file. The page size was 4096 bytes and the page count was 2, which means the file occupied two database pages.

The `mmap_size` value was initially set to `0`. After increasing it to `268435456`, the query time reduced slightly in this small test. Since the dataset was tiny, the improvement was not very large, but it showed that memory-mapped I/O can influence SQLite query performance.

---

## 4. PostgreSQL Experiment

### 4.1 Starting PostgreSQL

Example command on Windows:

```powershell
& "C:\Program Files\PostgreSQL\17\bin\psql.exe" -U postgres -h 127.0.0.1 -p 5433
```

On Linux/macOS, it may look like:

```bash
psql -U postgres -h 127.0.0.1 -p 5432
```

### 4.2 SQL Commands Used

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
|---|---:|
| PostgreSQL Version | 17.9 |
| Database Name | `lab2db` |
| Port Used | 5433 |
| Block Size | 8192 bytes |
| Database Size | 8189619 bytes |
| Query Execution Time | 3.566 ms |

### PostgreSQL Summary

The same `users` table was created inside the PostgreSQL database named `lab2db`. PostgreSQL reported a block size of 8192 bytes. The database size was much larger than SQLite for the same small dataset because PostgreSQL maintains additional system catalogs, metadata, transaction-related information, and server-managed storage structures.

---

## 6. SQLite3 vs PostgreSQL Comparison

| Comparison Point | SQLite3 | PostgreSQL |
|---|---|---|
| Database Type | Embedded database | Server-based relational database |
| Setup | Very simple; no server required | Requires PostgreSQL server installation and configuration |
| Storage Model | Stores the database in one `.db` file | Stores database data in PostgreSQL-managed directories and files |
| File/Database Size in Experiment | 8192 bytes | 8189619 bytes |
| Page/Block Size | 4096 bytes | 8192 bytes |
| Page Count | 2 pages | Not measured in the same direct SQLite style |
| Query Timing | 0.015015 sec without mmap, 0.008886 sec with mmap | 3.566 ms |
| mmap Control | Supports `PRAGMA mmap_size` | Does not use SQLite-style `PRAGMA mmap_size` |
| Best Suited For | Small apps, local storage, prototypes, testing | Production systems, multi-user apps, larger workloads |

---

## 7. Analysis

SQLite3 is lightweight and easy to use because it does not require a separate database server. It is useful when an application needs local storage, simple setup, and low overhead. The database is portable because it exists as a single file.

PostgreSQL is more powerful and is designed for production-level applications. It supports concurrency, user management, transactions, indexing options, extensions, permissions, and many advanced database features. However, because it runs as a full server-based database system, it has more storage overhead than SQLite for very small examples.

In this experiment, SQLite used a 4096-byte page size and PostgreSQL used an 8192-byte block size. SQLite's file size stayed very small because the dataset contained only one small table. PostgreSQL showed a larger database size because it includes internal structures even when the user-created table is small.

The `mmap_size` experiment in SQLite showed that enabling memory-mapped I/O can reduce query execution time slightly. For a small table, the change is minor. For larger read-heavy workloads, the effect may become more noticeable.

---

## 8. Lab 2 Conclusion

SQLite3 and PostgreSQL are both relational database systems, but they are designed for different use cases. SQLite3 is best for small, local, embedded, and lightweight applications. PostgreSQL is better for larger applications where reliability, concurrency, security, and scalability are important.

The experiment showed that SQLite has less setup and smaller storage overhead, while PostgreSQL provides a more complete database server environment. The SQLite `mmap_size` setting also demonstrated how memory-based file access can affect query performance.

---

# Final Overall Conclusion

Lab 1 explained how file operations work at the system-call level using file descriptors and kernel-managed I/O. Lab 2 compared two database systems and showed how storage design, page/block size, query execution, and memory mapping differ between SQLite3 and PostgreSQL.

Together, both labs helped connect operating-system-level storage concepts with database-system behavior.
