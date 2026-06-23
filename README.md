# SQLite3 vs PostgreSQL Comparison Report

# SQLite3

## Create Database

```bash
sqlite3 stock-db
```

---

## Create Table

```sql
CREATE TABLE stocks (
    stock_id INT PRIMARY KEY,
    company_name TEXT NOT NULL,
    stock_price REAL NOT NULL,
    market TEXT NOT NULL
);
```

---

## Insert Data

```sql
INSERT INTO stocks VALUES (1, "Tesla", 172.45, "NASDAQ");
```

---

# File Size Check

```bash
ls -lh stock-db
```

### Output

```bash
-rw-r--r-- 1 om-malviya om-malviya 12K May  9 18:46 stock-db
```

---

# Page Size & Page Count

## Check Page Size

```sql
PRAGMA page_size;
```

### Output

```sql
SQLite version 3.45.1 2024-01-30 16:01:20
Enter ".help" for usage hints.
sqlite> PRAGMA page_size;
4096
sqlite>
```

---

## Check Page Count

```sql
PRAGMA page_count;
```

### Output

```sql
sqlite> PRAGMA page_count;
3
sqlite>
```

### Observation

The database file size is 12KB because SQLite uses:

* 3 pages
* Each page size = 4KB

So,

3 \times 4\text{KB} = 12\text{KB}

---

# mmap Experiment (SQLite)

## Check mmap Size

```sql
PRAGMA mmap_size;
```

### Output

```sql
0
```

---

## Enable mmap

```sql
PRAGMA mmap_size = 268435456;
```

This enables 256MB memory mapping.

---

# Query Time Comparison

```bash
time sqlite3 stock-db "SELECT * FROM stocks;"
```

---

## mmap OFF

```bash
1|Tesla|172.45|NASDAQ
2|Apple|198.32|NASDAQ
3|Amazon|184.11|NASDAQ
4|Google|165.28|NASDAQ
5|Microsoft|421.17|NASDAQ
6|NVIDIA|902.66|NASDAQ
7|Meta|512.30|NASDAQ
8|Netflix|611.92|NASDAQ
9|Intel|36.45|NASDAQ
10|AMD|158.73|NASDAQ

real    0m0.007s
user    0m0.005s
sys     0m0.003s
```

---

## mmap ON

```bash
1|Tesla|172.45|NASDAQ
2|Apple|198.32|NASDAQ
3|Amazon|184.11|NASDAQ
4|Google|165.28|NASDAQ
5|Microsoft|421.17|NASDAQ
6|NVIDIA|902.66|NASDAQ
7|Meta|512.30|NASDAQ
8|Netflix|611.92|NASDAQ
9|Intel|36.45|NASDAQ
10|AMD|158.73|NASDAQ

real    0m0.006s
user    0m0.003s
sys     0m0.003s
```

### Observation

When mmap was enabled, query execution became slightly faster:

* mmap OFF → 0.007s
* mmap ON → 0.006s

Memory mapping improves performance by allowing SQLite to access the database file directly through virtual memory instead of regular file I/O operations.

---

# PostgreSQL Experiment

# Create Database

```sql
CREATE DATABASE stock_db;
\c stock_db
```

---

# Create Table

```sql
CREATE TABLE stocks (
    stock_id INT PRIMARY KEY,
    company_name TEXT NOT NULL,
    stock_price REAL NOT NULL,
    market TEXT NOT NULL
);
```

---

# Insert Data

```sql
INSERT INTO stocks VALUES (1, 'Tesla', 172.45, 'NASDAQ');
```

---

# Page Size

```sql
SELECT current_setting('block_size');
```

### Output

```sql
 current_setting
-----------------
 8192
(1 row)
```

### Observation

PostgreSQL uses 8KB pages for storage management.

---

# Query Timing

```sql
\timing
SELECT * FROM stocks;
```

### Output

```sql
Timing is on.

 stock_id | company_name | stock_price | market
----------+--------------+-------------+---------
        1 | Tesla        |      172.45 | NASDAQ
(1 row)

Time: 0.185 ms
```

### Observation

The query executed very quickly for a single row dataset.

---

# System Architecture Check

# SQLite Process

```bash
ps aux | grep sqlite3
```

### Output

```bash
om-malviya   37129  0.0  0.0  18980  2344 pts/3    S+   18:42   0:00 grep --color=auto sqlite3
```

### Observation

SQLite works as an embedded database library and does not require a separate server process.

---

# PostgreSQL Process

```bash
ps aux | grep postgres
```

### Output

```bash
postgres   20324  0.0  0.1 235296 31808 ?        SNs  18:03   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
postgres   20325  0.0  0.2 235588 32936 ?        SNs  18:03   0:00 postgres: 16/main: checkpointer
postgres   20326  0.0  0.0 235452  8180 ?        SNs  18:03   0:00 postgres: 16/main: background writer
postgres   20328  0.0  0.0 235296 10792 ?        SNs  18:03   0:00 postgres: 16/main: walwriter
postgres   20329  0.0  0.0 236908  9420 ?        SNs  18:03   0:00 postgres: 16/main: autovacuum launcher
postgres   20330  0.0  0.0 236880  8588 ?        SNs  18:03   0:00 postgres: 16/main: logical replication launcher
```

### Observation

PostgreSQL runs as a multi-process server architecture with dedicated background processes such as:

* Checkpointer
* WAL Writer
* Background Writer
* Autovacuum Launcher
* Logical Replication Launcher

---

# Comparison Table

| Feature      | SQLite3            | PostgreSQL                  |
| ------------ | ------------------ | --------------------------- |
| Type         | Embedded Database  | Client-Server Database      |
| Page Size    | 4KB                | 8KB                         |
| Storage      | Single File        | Managed Database System     |
| mmap Support | Supported          | Not Directly Exposed        |
| Performance  | Lightweight & Fast | High Performance & Scalable |
| Concurrency  | Limited            | High Concurrency            |
| Architecture | Serverless         | Multi-Process Server        |

---

# Conclusion

SQLite3 is lightweight and easy to use because it stores the entire database inside a single file and does not require a database server. In this experiment, SQLite used 4KB pages, and enabling mmap slightly improved query performance.

PostgreSQL is a powerful client-server database system designed for large-scale and multi-user applications. It uses 8KB pages and provides advanced background services such as WAL writing, autovacuum, and replication support.

Overall:

* SQLite3 is suitable for small applications, local storage, and embedded systems.
* PostgreSQL is more suitable for enterprise applications, large datasets, and systems requiring high concurrency and reliability.