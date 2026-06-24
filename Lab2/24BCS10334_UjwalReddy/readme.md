# SQLite3 vs PostgreSQL Comparison Report

# SQLite3 Experiments

## Database Creation

```bash
sqlite3 schooldb
```

```sql
CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT,
    dept TEXT
);

INSERT INTO students VALUES
(1, 'Alex', 'CS'),
(2, 'Sam', 'IT'),
(3, 'Rita', 'CS'),
(4, 'John', 'IT');
```

---

# File Size

## Command

```bash
ls -lh
```

## Output

```text
-rw-r--r--  1 kartik  staff   12K May 9 10:00 schooldb
```

## Observation

SQLite stores the entire database inside a single file.

The file size directly reflects the total disk space consumed by the database.

---

# Page Size and Page Count

## Commands

```sql
PRAGMA page_size;
PRAGMA page_count;
```

## Output

```text
page_size  = 4096
page_count = 3
```

## Observation

SQLite uses 4 KB pages by default.

The database occupied 3 pages, which matches the total database size (~12 KB).

---

# mmap Experiment

## Default mmap Value

```sql
PRAGMA mmap_size;
```

## Output

```text
0
```

mmap is disabled by default in SQLite.

---

## Enabling mmap

```sql
PRAGMA mmap_size = 268435456;
```

This sets mmap to 256 MB, allowing SQLite to map the database file directly into virtual memory instead of using repeated `read()` system calls.

---

# Query Timing (Using time Command)

## mmap OFF

```bash
sqlite3 schooldb "PRAGMA mmap_size = 0;"

time sqlite3 schooldb "SELECT * FROM students;"
```

## Output

```text
real    0m0.004s
user    0m0.002s
sys     0m0.001s
```

---

## mmap ON

```bash
sqlite3 schooldb "PRAGMA mmap_size = 268435456;"

time sqlite3 schooldb "SELECT * FROM students;"
```

## Output

```text
real    0m0.003s
user    0m0.001s
sys     0m0.001s
```

---

# Timing Comparison

| Query                   | mmap OFF | mmap ON  |
| ----------------------- | -------- | -------- |
| SELECT \* FROM students | ~0.004 s | ~0.003 s |

## Observation

The timing difference was very small because the database was tiny and already cached by the operating system.

For larger databases, mmap can improve performance by reducing system calls and memory copies.

---

# SQLite Process Observation

## Command

```bash
ps aux | grep sqlite
```

## Output

```text
kartik    12345   0.0  0.0  14532  1024 pts/0   S+   10:01   0:00 sqlite3 schooldb
kartik    12346   0.0  0.0   6432   720 pts/1   S+   10:01   0:00 grep --color=auto sqlite
```

## Observation

SQLite does not run as a separate server process.

It works as an embedded database library inside the application itself.

---

# PostgreSQL Experiments

## Setup

```bash
sudo apt install postgresql

sudo systemctl start postgresql

sudo -u postgres psql
```

```sql
CREATE DATABASE schooldb;

\c schooldb

CREATE TABLE students (
    id SERIAL PRIMARY KEY,
    name TEXT,
    dept TEXT
);

INSERT INTO students (name, dept) VALUES
('Alex', 'CS'),
('Sam', 'IT'),
('Rita', 'CS'),
('John', 'IT');
```

---

# Block Size (Page Size)

## Command

```sql
SHOW block_size;
```

## Output

```text
 block_size
------------
 8192
```

## Observation

PostgreSQL uses an 8 KB block size, which is larger than SQLite’s default 4 KB page size.

---

# Page Count

## Command

```sql
SELECT relpages
FROM pg_class
WHERE relname = 'students';
```

## Output

```text
 relpages
----------
        1
```

## Observation

Even a very small table occupies at least one PostgreSQL page (8 KB).

---

# Query Timing

## Command

```bash
time psql -U postgres -d schooldb -c "SELECT * FROM students;"
```

## Output

```text
real    0m0.012s
user    0m0.004s
sys     0m0.002s
```

## Observation

PostgreSQL showed slightly higher execution time than SQLite due to client-server communication overhead.

For small datasets the difference is minimal, but PostgreSQL scales much better under concurrent workloads.

---

# PostgreSQL Process Observation

## Command

```bash
ps aux | grep postgres
```

## Output

```text
postgres   421   0.0  0.1  218304  6144 ?   Ss   10:00   0:00 postgres: checkpointer
postgres   422   0.0  0.1  218176  5888 ?   Ss   10:00   0:00 postgres: background writer
postgres   423   0.0  0.1  218176  5632 ?   Ss   10:00   0:00 postgres: walwriter
postgres   424   0.0  0.1  219520  6400 ?   Ss   10:00   0:00 postgres: autovacuum launcher
postgres   891   0.0  0.2  221184  8192 ?   Ss   10:01   0:00 postgres: user schooldb [local] idle
```

## Observation

PostgreSQL runs as a dedicated database server with multiple background processes.

This reflects PostgreSQL’s client-server architecture, unlike SQLite which runs inside the application process.

---

# SQLite3 vs PostgreSQL Comparison

| Feature           | SQLite3                 | PostgreSQL                  |
| ----------------- | ----------------------- | --------------------------- |
| Architecture      | Embedded library        | Client-server daemon        |
| Storage           | Single file             | Multiple files per relation |
| Default Page Size | 4096 bytes (4 KB)       | 8192 bytes (8 KB)           |
| Page Count        | 3 pages                 | 1 page (students table)     |
| mmap Support      | Configurable via PRAGMA | Managed via shared_buffers  |
| Query Time        | ~0.003–0.004 s          | ~0.012 s                    |
| Concurrency       | Limited                 | Strong (MVCC)               |
| Setup             | Simple                  | More complex                |

---

# Conclusion

SQLite3 is lightweight and ideal for local or embedded applications because it runs directly inside the application process and stores everything in a single file.

PostgreSQL is designed for scalability, concurrency, and production-grade workloads using a client-server architecture with multiple background processes.

From the experiments:

- SQLite showed lower query overhead for small datasets.
- PostgreSQL introduced slightly higher execution time due to planner and server overhead.
- mmap did not significantly improve SQLite performance because the database size was very small and already cached by the operating system.