# Lab 2: SQLite3 and PostgreSQL Comparison

## Student Details

Name: Aparna Singha  
Roll No: 24BCS10353  
Course: Advanced DBMS  

## 1. Aim of the Lab

This lab is about exploring how SQLite3 and PostgreSQL store data and comparing page size, page count, query timing, and mmap impact.

## 2. SQLite3 Exploration

### 2.1 Creating sample database

```text
sqlite3 sample.db

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
```

### 2.2 Checking file size

```bash
ls -lh sample.db
```

File size observed: `8.0K / 8192 bytes`

### 2.3 Page size and page count

```sql
PRAGMA page_size;
PRAGMA page_count;
```

Observed:

- Page size: `4096 bytes`
- Page count: `2`

### 2.4 mmap_size experiment

```sql
PRAGMA mmap_size;
PRAGMA mmap_size = 0;
SELECT * FROM users;

PRAGMA mmap_size = 268435456;
PRAGMA mmap_size;
SELECT * FROM users;
```

Observed:

- mmap before: `0`
- mmap after: `268435456`

### 2.5 Query timing

```text
.timer ON
```

Observed:

- Without mmap: `0.015015 seconds`
- With mmap: `0.008886 seconds`

Since the table has only four rows, the timing difference should not be treated as a serious benchmark.

### 2.6 SQLite process check

```bash
ps aux | grep sqlite
```

SQLite is embedded and does not run as a separate database server like PostgreSQL. For this local database, it mainly works through the process that opens the `sample.db` file.

## 3. PostgreSQL Exploration

### 3.1 Setup and connection

```bash
psql -U postgres -h 127.0.0.1 -p 5433
```

Observed:

- PostgreSQL version: `17.9`
- Database name: `lab2db`
- Port used: `5433`

### 3.2 Creating database and table

```text
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
```

### 3.3 Block size and database size

```sql
SELECT current_setting('block_size');
SELECT pg_database_size('lab2db');
```

Observed:

- Block size: `8192 bytes`
- Database size: `8189619 bytes`

### 3.4 Query timing

```text
\timing
SELECT * FROM users;
```

Observed:

- Query execution time: `3.566 ms`

### 3.5 Page-count equivalent

```sql
SELECT pg_relation_size('users') AS users_table_size;
SELECT relpages, reltuples FROM pg_class WHERE relname = 'users';
```

PostgreSQL does not show page count in the same direct way as SQLite `PRAGMA page_count`. I used `relpages` as the closest PostgreSQL page-count equivalent. The exact value can vary depending on the local run and whether `ANALYZE` or `VACUUM` has updated statistics.

I did not write a fixed `relpages` value here because it depends on the local table statistics. The command is included because this is the closest PostgreSQL equivalent I used for checking page count.

## 4. Observations

### SQLite observations

| Item | Observation |
|---|---|
| Database file | `sample.db` |
| File size | `8.0K / 8192 bytes` |
| Page size | `4096 bytes` |
| Page count | `2` |
| mmap before | `0` |
| mmap after | `268435456` |
| Query time without mmap | `0.015015 seconds` |
| Query time with mmap | `0.008886 seconds` |

### PostgreSQL observations

| Item | Observation |
|---|---|
| Version | `17.9` |
| Database name | `lab2db` |
| Port | `5433` |
| Block size | `8192 bytes` |
| Database size | `8189619 bytes` |
| Query execution time | `3.566 ms` |
| Page-count equivalent command | `SELECT relpages, reltuples FROM pg_class WHERE relname = 'users';` |

## 5. SQLite3 vs PostgreSQL Comparison

| Point | SQLite3 | PostgreSQL |
|---|---|---|
| Type | Embedded | Server-based |
| Storage | Single `.db` file | PostgreSQL-managed files |
| Page size / block size | `4096 bytes` | `8192 bytes` |
| Page count / page equivalent | `2` pages from `PRAGMA page_count` | `relpages` is the closest equivalent |
| Query performance | `0.015015 seconds` without mmap, `0.008886 seconds` with mmap | `3.566 ms` |
| mmap impact | `mmap_size` changed the query time slightly | No matching SQLite-style `mmap_size` check used here |
| Best use case | Small local apps and simple file-based storage | Bigger multi-user systems and server-based work |

## 6. Analysis

- SQLite was easier to set up because it uses one database file.
- PostgreSQL had more overhead because it is a full server-based DBMS.
- SQLite `mmap_size` affected the query time slightly in my small run.
- Since the dataset is very small, timings should not be considered a proper performance benchmark.
- The main learning was understanding page size, page count, block size, mmap, and how storage differs between SQLite and PostgreSQL.
- The main difference I noticed is that SQLite exposes page count directly using PRAGMA, while PostgreSQL needs catalog/statistics queries like `pg_class` to get a similar idea.

## 7. Conclusion

This lab helped me understand how SQLite3 and PostgreSQL manage storage differently. SQLite is lightweight and file-based, while PostgreSQL is server-based and has more internal storage structures. The page size, page count, mmap, and query timing checks helped me connect the lab with DBMS storage concepts.