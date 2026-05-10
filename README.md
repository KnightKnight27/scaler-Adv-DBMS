# scaler-Adv-DBMS
# Database Lab Report: SQLite3 vs PostgreSQL

**Name:** `<Your Name>`  
**Role Number:** `<Your Role Number>`

---

# 1. SQLite3 Exploration

## Installation

```bash
sudo apt update
sudo apt install sqlite3
```

## Create and Open Database

```bash
sqlite3 sample.db
```

## Create Table

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

## Insert Sample Data

```sql
INSERT INTO users (name, age) VALUES
('Alice', 22),
('Bob', 25),
('Charlie', 30);
```

## Check File Size

```bash
ls -lh
```

### Observation
- SQLite database is stored as a single file.
- File size increases when data is inserted.

---

## Find Page Size

```sql
PRAGMA page_size;
```

### Example Output

```text
4096
```

### Observation
- Default SQLite page size is usually 4096 bytes.

---

## Find Page Count

```sql
PRAGMA page_count;
```

### Observation
- Page count increases as database grows.

---

## Check mmap Size

```sql
PRAGMA mmap_size;
```

## Change mmap Size

```sql
PRAGMA mmap_size = 268435456;
```

### Observation
- mmap maps database files into memory.
- It can improve read performance.

---

## Query Time Without mmap

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

### Example Output

```text
real    0m0.005s
user    0m0.002s
sys     0m0.001s
```

---

## Query Time With mmap

```bash
time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;"
```

### Example Output

```text
real    0m0.003s
user    0m0.001s
sys     0m0.001s
```

### Observation
- Query execution is slightly faster with mmap enabled.

---

## Check SQLite Processes

```bash
ps aux | grep sqlite
```

---

# 2. PostgreSQL (PSQL) Setup

## Installation

```bash
sudo apt update
sudo apt install postgresql postgresql-contrib
```

---

## Start PostgreSQL Service

```bash
sudo systemctl start postgresql
```

---

## Open PostgreSQL Shell

```bash
sudo -u postgres psql
```

---

## Create Database

```sql
CREATE DATABASE labdb;
```

## Connect Database

```sql
\c labdb
```

---

## Create Table

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    age INT
);
```

---

## Insert Data

```sql
INSERT INTO users (name, age) VALUES
('Alice',22),
('Bob',25),
('Charlie',30);
```

---

## Find PostgreSQL Page Size

```sql
SHOW block_size;
```

### Example Output

```text
8192
```

### Observation
- PostgreSQL default block size is 8192 bytes.

---

## Find Page Count

```sql
SELECT relpages FROM pg_class WHERE relname='users';
```

### Observation
- `relpages` shows estimated page count used by the table.

---

## Query Performance

```sql
EXPLAIN ANALYZE SELECT * FROM users;
```

### Example Output

```text
Execution Time: 0.120 ms
```

### Observation
- PostgreSQL provides detailed execution analysis.

---

## Check PostgreSQL Processes

```bash
ps aux | grep postgres
```

---

# 3. Comparison Report

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Type | Embedded DB | Client-Server DB |
| Storage | Single File | Multiple Files |
| Default Page Size | 4096 Bytes | 8192 Bytes |
| Query Speed | Fast for small apps | Better for large systems |
| mmap Support | Yes | Internal caching |
| Multi-user Support | Limited | Strong |
| Best Use Case | Small/local apps | Large scalable apps |

---

# 4. mmap Impact

| Condition | Performance |
|---|---|
| Without mmap | Slightly slower |
| With mmap | Faster reads |

### Observation
- mmap reduces disk I/O operations and improves read performance.

---

# 5. Final Conclusion

- SQLite3 is lightweight, simple, and easy to use.
- PostgreSQL is more powerful and scalable.
- SQLite works best for local/small projects.
- PostgreSQL is better for enterprise and multi-user systems.
- mmap improves SQLite query performance for read-heavy workloads.

---

# 6. Commands Summary

## SQLite3 Commands

```bash
sqlite3 sample.db
ls -lh
time sqlite3 sample.db "SELECT * FROM users;"
ps aux | grep sqlite
```

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
PRAGMA mmap_size = 268435456;
```

---

## PostgreSQL Commands

```bash
sudo systemctl start postgresql
sudo -u postgres psql
ps aux | grep postgres
```

```sql
SHOW block_size;
SELECT relpages FROM pg_class WHERE relname='users';
EXPLAIN ANALYZE SELECT * FROM users;
```
