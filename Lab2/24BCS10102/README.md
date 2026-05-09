# scaler-Adv-DBMS

# SQLite3 vs PostgreSQL Comparison Report

## Student Details

- Name: Abhay Singh Bhadauria
- Role Number: 24BCS10102

---

# 1. SQLite3 Exploration

## Commands Used

### Create Database

```bash
sqlite3 sample.db
```

### Create Table

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

### Insert Data

```sql
INSERT INTO users (name, age) VALUES
('Abhay', 20),
('Rahul', 22),
('Aman', 21),
('Priya', 23),
('Neha', 24);
```

### Display Data

```sql
SELECT * FROM users;
```

Output:

```text
1|Abhay|20
2|Rahul|22
3|Aman|21
4|Priya|23
5|Neha|24
```

---

# File Size Observation

Command:

```bash
ls -lh
```

Output:

```text
-rw-r--r-- 1 abhay abhay 8.0K May  9 19:24 sample.db
```

Observation:
- SQLite database file size was only 8.0 KB.
- SQLite stores the complete database in a single file.

---

# PRAGMA Analysis

## Page Size

Command:

```sql
PRAGMA page_size;
```

Output:

```text
4096
```

Observation:
- SQLite page size is 4096 bytes (4 KB).

---

## Page Count

Command:

```sql
PRAGMA page_count;
```

Output:

```text
2
```

Observation:
- Database currently uses 2 pages.

---

# mmap_size Experiment

## Default mmap Size

Command:

```sql
PRAGMA mmap_size;
```

Output:

```text
0
```

Observation:
- mmap was disabled by default.

---

## Enable mmap

Command:

```sql
PRAGMA mmap_size = 268435456;
```

Output:

```text
268435456
```

Observation:
- mmap size was successfully changed to 256 MB.

---

# Query Execution Time

## Query

Command:

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

Output:

```text
1|Abhay|20
2|Rahul|22
3|Aman|21
4|Priya|23
5|Neha|24

real    0m0.006s
user    0m0.001s
sys     0m0.005s
```

---

# mmap vs Non-mmap Comparison

## Without mmap

Command:

```sql
PRAGMA mmap_size = 0;
```

Observation:
- mmap disabled successfully.

---

## With mmap

Command:

```sql
PRAGMA mmap_size = 268435456;
```

Query Timing:

```text
real    0m0.006s
user    0m0.002s
sys     0m0.004s
```

Observation:
- No major performance difference was observed because the dataset was very small.

---

# SQLite Process Observation

Command:

```bash
ps aux | grep sqlite
```

Output:

```text
abhay      14320  0.0  0.0  18000  2540 pts/0    S+   19:31   0:00 grep --color=auto sqlite
```

Observation:
- SQLite runs as a lightweight local process.
- No dedicated server process exists.

---

# 2. PostgreSQL (PSQL) Setup

## Installation

Command:

```bash
sudo apt install postgresql postgresql-contrib -y
```

Observation:
- PostgreSQL installed successfully with required dependencies.

---

# PostgreSQL Service Status

Command:

```bash
sudo systemctl status postgresql
```

Output:

```text
Active: active (exited)
```

Observation:
- PostgreSQL service was running successfully.

---

# PostgreSQL Database Creation

## Create Database

```sql
CREATE DATABASE labdb;
```

## Connect Database

```sql
\c labdb
```

## Create Table

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    age INT
);
```

## Insert Data

```sql
INSERT INTO users (name, age) VALUES
('Abhay', 20),
('Rahul', 22),
('Aman', 21),
('Priya', 23),
('Neha', 24);
```

---

# PostgreSQL Page Size

Command:

```sql
SHOW block_size;
```

Output:

```text
8192
```

Observation:
- PostgreSQL block size is 8192 bytes (8 KB).

---

# PostgreSQL Page Count

Command:

```sql
SELECT relpages FROM pg_class WHERE relname='users';
```

Output:

```text
0
```

Observation:
- PostgreSQL showed 0 pages because the table is very small and statistics were not updated yet.

---

# PostgreSQL Query Timing

## Enable Timing

```sql
\timing
```

---

## Query

```sql
SELECT * FROM users;
```

Output:

```text
 id | name  | age
----+-------+-----
  1 | Abhay |  20
  2 | Rahul |  22
  3 | Aman  |  21
  4 | Priya |  23
  5 | Neha  |  24
(5 rows)

Time: 0.482 ms
```

Observation:
- PostgreSQL query execution time was extremely fast.

---

# 3. Comparison Analysis

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | File-based | Server-based |
| Database Size | 8.0 KB | Larger installation footprint |
| Page Size | 4096 bytes | 8192 bytes |
| Page Count | 2 | 0 (small table stats) |
| mmap Support | Yes | Internal memory management |
| Query Time | 0.006s | 0.482 ms |
| Setup Complexity | Easy | Moderate |
| Server Requirement | No | Yes |

---

# Key Observations

## SQLite3

- Lightweight and simple.
- Entire database stored in one file.
- Easy to set up and use.
- Suitable for small applications and embedded systems.

## PostgreSQL

- More powerful and scalable.
- Uses client-server architecture.
- Better suited for large-scale and enterprise applications.
- Provides advanced database features.

---

# Conclusion

SQLite3 is best for lightweight applications because it is simple, fast, and file-based.

PostgreSQL is more suitable for enterprise applications because it supports scalability, advanced features, and multi-user access.

For small datasets, both databases performed very efficiently.