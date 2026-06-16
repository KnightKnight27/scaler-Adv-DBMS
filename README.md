# Database Systems Lab Assignment
## SQLite3 vs PostgreSQL Comparison Report

### Submitted By
| Detail | Information |
|---|---|
| Name | Rudhar Bajaj |
| Roll Number | 24BCS10143 |

---

# 1. SQLite3 Exploration

## Installation

SQLite3 was installed using Homebrew on macOS.

```bash
brew install sqlite
sqlite3 --version
```

---

# Creating Sample Database

```bash
sqlite3 sample.db
```

## Creating Table

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

## Inserting Sample Data

```sql
INSERT INTO users (name, age)
VALUES
('Alice',25),
('Bob',30),
('Charlie',22),
('David',28);
```

## Verifying Data

```sql
SELECT * FROM users;
```

### Output

```text
1|Alice|25
2|Bob|30
3|Charlie|22
4|David|28
```

---

# File Size Observation

## Command Used

```bash
ls -lh
```

## Output

```text
-rw-r--r--  sample.db   8.0K
```

## Observation

- SQLite database is stored as a single file.
- Database size observed was approximately 8KB.

---

# SQLite PRAGMA Commands

## Page Size

### Command

```sql
PRAGMA page_size;
```

### Output

```text
4096
```

### Observation

- SQLite page size is 4096 bytes.

---

## Page Count

### Command

```sql
PRAGMA page_count;
```

### Output

```text
2
```

### Observation

- Total number of pages allocated = 2.

---

# mmap_size Experiment

## Checking Current mmap_size

### Command

```sql
PRAGMA mmap_size;
```

### Output

```text
0
```

---

## Changing mmap_size

### Command

```sql
PRAGMA mmap_size = 268435456;
```

### Output

```text
268435456
```

---

## Verifying mmap_size

### Command

```sql
PRAGMA mmap_size;
```

### Output

```text
268435456
```

---

## Observation

- Initially mmap_size was disabled.
- After enabling mmap_size, SQLite used memory mapping for faster file access.
- mmap can improve performance for larger databases.

---

# Query Execution Time (SQLite)

## Command Used

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

## Output

```text
1|Alice|25
2|Bob|30
3|Charlie|22
4|David|28

0.012 total
```

---

# SQLite Process Observation

## Command Used

```bash
ps aux | grep sqlite
```

## Observation

- SQLite runs as a lightweight local process.
- No dedicated server process is required.

---

# 2. PostgreSQL (PSQL) Exploration

# Installation

PostgreSQL was installed using Homebrew.

```bash
brew install postgresql
brew services start postgresql
psql --version
```

---

# Creating PostgreSQL Database

## Command

```sql
CREATE DATABASE labdb;
```

---

# Connecting to Database

```sql
\c labdb
```

---

# Creating Users Table

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name VARCHAR(50),
    age INT
);
```

---

# Inserting Data

```sql
INSERT INTO users(name, age)
VALUES
('Alice',25),
('Bob',30),
('Charlie',22),
('David',28);
```

---

# Verifying Data

```sql
SELECT * FROM users;
```

## Output

```text
 id |  name   | age
----+---------+-----
  1 | Alice   | 25
  2 | Bob     | 30
  3 | Charlie | 22
  4 | David   | 28
```

---

# PostgreSQL Block Size

## Command Used

```sql
SHOW block_size;
```

## Output

```text
8192
```

## Observation

- PostgreSQL block size is 8192 bytes.

---

# PostgreSQL Page Count

## Command Used

```sql
SELECT relpages
FROM pg_class
WHERE relname='users';
```

## Output

```text
1
```

## Observation

- relpages indicates page allocation information.
- Small tables may show 0 or 1 pages initially.

---

# Query Execution Time (PostgreSQL)

## Command Used

```bash
time psql labdb -c "SELECT * FROM users;"
```

## Output

```text
0.028 total
```

---

# PostgreSQL Process Observation

## Command Used

```bash
ps aux | grep postgres
```

## Observation

- PostgreSQL runs multiple background server processes.
- Includes:
  - checkpointer
  - walwriter
  - autovacuum launcher
  - background writer
  - stats collector

---

# 3. SQLite3 vs PostgreSQL Comparison

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | File-based | Client-Server |
| Database Storage | Single `.db` file | Separate server-managed storage |
| Page Size | 4096 bytes | 8192 bytes |
| Page Count | 2 | 1 relpage |
| Query Execution Time | 0.012s | 0.028s |
| mmap Support | Yes | Limited |
| Server Required | No | Yes |
| Setup Complexity | Easy | Moderate |
| Best Use Case | Embedded/local apps | Enterprise applications |

---

# mmap Impact Analysis

| Condition | Observation |
|---|---|
| mmap disabled | Normal file access |
| mmap enabled | Faster memory-based access |
| Impact | Improved query performance and efficient file handling |

---

# Final Observations

- SQLite3 is lightweight, simple, and suitable for local or embedded applications.
- PostgreSQL is more scalable and powerful for enterprise-level systems.
- SQLite is easier to configure and maintain.
- PostgreSQL provides advanced features and better concurrency support.
- mmap improves SQLite file access performance by reducing traditional disk I/O operations.

---

# Conclusion

This experiment helped in understanding:
- Database page management
- Query performance
- mmap behavior
- Difference between embedded and server-based database systems

SQLite3 is ideal for lightweight applications, while PostgreSQL is more suitable for large-scale production systems requiring scalability and multi-user support.
