# Advanced DBMS Lab Report – SQLite3 vs PostgreSQL

## Objective

The objective of this lab was to study and compare the behavior of two popular database management systems: SQLite3 and PostgreSQL. The experiments focused on understanding internal storage structure, page management, query execution time, and the effect of memory mapping (`mmap`) on database performance. Different commands and tools were used to inspect how both databases manage storage and execute queries.

---

# 1. SQLite3 Exploration

## Introduction

SQLite3 is a lightweight, file-based relational database management system. Unlike traditional DBMS software, SQLite does not require a dedicated server process. The entire database is stored in a single file, making it simple and portable.

---

## Commands Used

### Check Database File Size

```bash
ls -lh
```

This command was used to observe the size of the SQLite database file stored in the system.

---

### Open SQLite Database

```bash
sqlite3 sample.db
```

This command opened the sample SQLite database.

---

### Find Page Size

```sql
PRAGMA page_size;
```

This command returned the size of each database page in bytes.

---

### Find Total Page Count

```sql
PRAGMA page_count;
```

This command displayed the total number of pages allocated for the database.

---

### Check Current mmap Size

```sql
PRAGMA mmap_size;
```

This command showed the current memory-mapped I/O size being used by SQLite.

---

### Change mmap Size

```sql
PRAGMA mmap_size = 268435456;
```

This command increased the mmap size to improve read performance by reducing disk I/O operations.

---

### Execute Query

```sql
SELECT * FROM users;
```

This query fetched all records from the `users` table.

---

### Measure Query Execution Time

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

The `time` command was used to measure how long the query execution took.

---

### Check SQLite Process

```bash
ps aux | grep sqlite
```

This command was used to inspect SQLite-related processes running in the system.

---

## Observations

* SQLite stores the complete database in a single file.
* Database file size increased as more data was inserted.
* Page size and total page count were successfully retrieved using PRAGMA commands.
* SQLite showed fast execution for smaller datasets.
* Increasing `mmap_size` slightly improved query execution speed, especially for repeated read operations.
* SQLite consumed very low memory and CPU resources.
* Since SQLite is serverless, no separate background server processes were observed.

---

# 2. PostgreSQL Exploration

## Introduction

PostgreSQL is a powerful open-source relational database management system that follows a client-server architecture. It is designed for high scalability, concurrency, and large-scale applications.

---

## Commands Used

### Open PostgreSQL

```bash
psql -U postgres
```

This command opened the PostgreSQL command-line interface.

---

### Create Database

```sql
CREATE DATABASE labdb;
```

This command created a new database for testing.

---

### Connect to Database

```sql
\c labdb
```

This command connected to the newly created database.

---

### Check PostgreSQL Block Size

```sql
SHOW block_size;
```

This command displayed the default block/page size used internally by PostgreSQL.

---

### Find Relation Page Count

```sql
SELECT relpages 
FROM pg_class 
WHERE relname='users';
```

This query returned the number of pages allocated for the `users` table.

---

### Measure Query Execution Time

```sql
\timing
SELECT * FROM users;
```

The `\timing` command enabled query execution timing measurement.

---

### Check PostgreSQL Processes

```bash
ps aux | grep postgres
```

This command displayed PostgreSQL background server processes running in the system.

---

## Observations

* PostgreSQL runs as a dedicated server-based DBMS.
* Multiple background processes were observed for memory management and concurrency handling.
* PostgreSQL internally manages storage using fixed-size pages/blocks.
* Query performance was better for larger datasets and complex operations.
* PostgreSQL used more system resources compared to SQLite.
* It provides better support for concurrent users and transactions.
* Memory management and caching are handled internally by PostgreSQL.

---

# 3. SQLite3 vs PostgreSQL Comparison

| Feature             | SQLite3                  | PostgreSQL                 |
| ------------------- | ------------------------ | -------------------------- |
| Architecture        | File-based               | Client-Server              |
| Database Storage    | Single file              | Multiple internal files    |
| Installation        | Simple                   | More complex               |
| Page Size           | Configurable             | Fixed block size           |
| Query Performance   | Fast for small workloads | Better for large workloads |
| mmap Support        | Directly configurable    | Internally managed         |
| Resource Usage      | Low                      | Higher                     |
| Concurrency Support | Limited                  | Excellent                  |
| Scalability         | Small to medium projects | Large-scale applications   |

---

# mmap Impact Analysis

## SQLite3

* Memory mapping (`mmap`) reduced direct disk access.
* Query execution became slightly faster after increasing mmap size.
* Performance improvement was more visible for repeated read operations.
* Useful for lightweight and read-heavy applications.

---

## PostgreSQL

* PostgreSQL internally manages caching and shared memory efficiently.
* Direct mmap tuning is not commonly exposed to users.
* Query optimization is handled automatically through internal buffer management.

---

# Conclusion

Through this lab, both SQLite3 and PostgreSQL were successfully explored and compared. SQLite3 proved to be lightweight, easy to use, and efficient for smaller applications with low concurrency requirements. PostgreSQL demonstrated better scalability, concurrency handling, and performance optimization for large datasets and enterprise-level systems.

SQLite is more suitable for embedded systems, local applications, and lightweight projects, while PostgreSQL is better suited for production-grade applications requiring reliability, scalability, and multi-user support.
