## 1. Student Details

**Name:** Aparna Singha  
**Roll No:** 24BCS10353  
**Course:** Advanced DBMS  
**Lab:** Lab 2

## 2. What this lab is about

In this lab, I compared SQLite3 and PostgreSQL using a small `users` table. The main goal was to see how both systems store data and how to check things like page size, page count or closest equivalent, file/database size, query timing, and the effect of `mmap` in SQLite.

This is a small practice lab, so the timings below should be treated only as simple observations, not as a serious benchmark.

## 3. SQLite3 Exploration

I used a small sample database file called `sample.db` with a `users` table.

### SQLite shell commands

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
.timer ON
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
PRAGMA mmap_size = 0;
SELECT * FROM users;
PRAGMA mmap_size = 268435456;
SELECT * FROM users;
```

### Terminal commands

```bash
ls -lh sample.db
ps aux | grep sqlite
```

### Observation

SQLite does not normally run as a separate server process. It is embedded in the program or process using the database file. So unlike PostgreSQL, there is no long-running SQLite server process for this simple local database.

## 4. SQLite3 observations table

| Item | Observation |
|---|---|
| Database file name | `sample.db` |
| File size | `8.0K` (`8192 bytes`) in local sample run |
| Page size | `4096 bytes` |
| Page count | `2` |
| mmap size before | `0` |
| mmap size after | `268435456` |
| Query time without mmap | `Run Time: real 0.001 sec` in local sample run |
| Query time with mmap | `Run Time: real 0.000 sec` in local sample run |

The file size matched the page size and page count for this small sample database. Since the table had only a few rows, the difference in query timing was very small.

## 5. PostgreSQL Setup and Exploration

### psql connection command

```bash
psql -U postgres -h 127.0.0.1 -p 5432
```

### Commands used in psql

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

SELECT version();
SELECT current_setting('block_size');
SELECT pg_database_size('lab2db');
\timing
SELECT * FROM users;
SELECT pg_relation_size('users') AS users_table_size;
SELECT relpages, reltuples FROM pg_class WHERE relname = 'users';
```

In PostgreSQL, `relpages` is the closest simple value to compare with SQLite page count for a table.

## 6. PostgreSQL observations table

I observed these values on my local PostgreSQL setup.

| Item | Observation |
|---|---|
| PostgreSQL version | `17.9` |
| Database name | `lab2db` |
| Port used | `5433` |
| Block size | `8192 bytes` |
| Database size | `8189619 bytes` |
| Query execution time | `3.566 ms` |
| relpages / reltuples | Checked using relpages query; value depends on local run |

## 7. SQLite3 vs PostgreSQL comparison table

| Point | SQLite3 | PostgreSQL |
|---|---|---|
| Page size | `4096 bytes` in local sample run | `8192 bytes` in my local PostgreSQL run |
| Page count / page equivalent | `2` pages in local sample run | Use `relpages` from `pg_class` as closest equivalent; exact value depends on local run |
| Query performance | `0.001 sec` without mmap and `0.000 sec` with mmap in local sample run | `3.566 ms` in my local PostgreSQL run |
| mmap impact | Can be checked with `PRAGMA mmap_size` and compared before/after | No SQLite-style `PRAGMA mmap_size` used here |
| Storage model | File-based database stored in one `.db` file | Server-managed database storage |
| Server vs embedded design | Embedded, no separate database server process needed | Runs as a separate database server |

## 8. Analysis

SQLite is lightweight and file-based, so it is easy to create and inspect for a small local lab. PostgreSQL is server-based, so it has more metadata, more setup, and more storage overhead. In my local PostgreSQL run, I observed an `8192 bytes` block size, `8189619 bytes` database size, and `3.566 ms` query time for the simple `SELECT * FROM users;` check.

In SQLite, changing `mmap_size` can slightly affect read behavior. In this small sample run, the timing difference was very small. Since the dataset is tiny, these timings should not be treated as a proper performance benchmark.

## 9. Conclusion

From this lab, I understood that SQLite and PostgreSQL both store data in fixed-size units, but they are made for different use cases. SQLite is simpler and lighter, while PostgreSQL is more suited for a full server-based database setup.
