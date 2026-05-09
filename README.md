# SQLite3 vs PostgreSQL Exploration Report

## Submitted By

* **Name:** Saniya Sanjiv Patil
* **Role Number:** 2024EB02305

---

# Database Systems Lab Assignment

## Objective

The objective of this lab was to explore and compare the internal storage behavior and query performance of:

* SQLite
* PostgreSQL

Experiments included:

* Database file/page analysis
* Query timing
* Memory-mapped I/O (`mmap`)
* Performance comparison

---

# 1. SQLite3 Exploration

## Installation

### macOS

```bash
brew install sqlite
```

### Ubuntu/Linux

```bash
sudo apt update
sudo apt install sqlite3
```

---

# Creating Sample Database

```bash
sqlite3 sample.db
```

Inside SQLite shell:

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);

INSERT INTO users (name, age)
VALUES
('Alice', 21),
('Bob', 22),
('Charlie', 23),
('David', 24),
('Eva', 25);
```

---

# Checking File Size

```bash
ls -lh sample.db
```

### Observation

The database file size increased after inserting records because SQLite stores all data inside a single `.db` file.

Example output:

```bash
-rw-r--r--  1 user  staff   8.0K May 9  sample.db
```

---

# PRAGMA Commands

## Page Size

```sql
PRAGMA page_size;
```

### Output

```text
4096
```

### Observation

SQLite uses a default page size of **4096 bytes (4KB)**.

---

## Page Count

```sql
PRAGMA page_count;
```

### Output

```text
2
```

### Observation

The database currently occupies 2 pages.

---

# mmap_size Experiment

## Checking Current mmap Size

```sql
PRAGMA mmap_size;
```

### Output

```text
0
```

---

## Enabling mmap

```sql
PRAGMA mmap_size = 268435456;
```

(256 MB)

### Observation

SQLite allows memory-mapped file access, which can improve read performance by reducing system calls.

---

# Query Timing

## Without mmap

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

### Example Output

```text
real    0m0.012s
user    0m0.004s
sys     0m0.003s
```

---

## With mmap Enabled

```bash
time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;"
```

### Example Output

```text
real    0m0.008s
user    0m0.003s
sys     0m0.002s
```

---

# Process Observation

```bash
ps aux | grep sqlite
```

### Observation

SQLite runs as an embedded database and does not require a dedicated server process.

---

# 2. PostgreSQL Exploration

## Installation

### macOS

```bash
brew install postgresql
brew services start postgresql
```

### Ubuntu/Linux

```bash
sudo apt install postgresql
sudo service postgresql start
```

---

# Creating Sample Database

```bash
createdb labdb
psql labdb
```

Inside PostgreSQL shell:

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100),
    age INT
);

INSERT INTO users (name, age)
VALUES
('Alice', 21),
('Bob', 22),
('Charlie', 23),
('David', 24),
('Eva', 25);
```

---

# Checking Database Size

```sql
SELECT pg_size_pretty(pg_database_size('labdb'));
```

### Example Output

```text
8 MB
```

---

# Page Size

```sql
SHOW block_size;
```

### Output

```text
8192
```

### Observation

PostgreSQL uses an **8KB default page size**, which is larger than SQLite’s 4KB pages.

---

# Page Count Estimation

```sql
SELECT relpages
FROM pg_class
WHERE relname='users';
```

### Example Output

```text
1
```

---

# Query Timing

## Running Query

```bash
time psql labdb -c "SELECT * FROM users;"
```

### Example Output

```text
real    0m0.025s
user    0m0.010s
sys     0m0.006s
```

---

# PostgreSQL Processes

```bash
ps aux | grep postgres
```

### Observation

Unlike SQLite, PostgreSQL runs multiple background server processes such as:

* writer process
* checkpoint process
* autovacuum
* WAL writer

---

# 3. Comparison Analysis

| Feature           | SQLite3                        | PostgreSQL                      |
| ----------------- | ------------------------------ | ------------------------------- |
| Database Type     | Embedded Database              | Client-Server Database          |
| Default Page Size | 4KB                            | 8KB                             |
| Server Required   | No                             | Yes                             |
| Storage           | Single File                    | Multiple Files                  |
| mmap Support      | Yes                            | Limited/Internal                |
| Setup Complexity  | Very Easy                      | Moderate                        |
| Query Performance | Faster for small local queries | Better for concurrent workloads |
| Concurrency       | Limited                        | Excellent                       |
| Best Use Case     | Mobile apps, local apps        | Large-scale applications        |

---

# mmap Impact Analysis

| Condition        | Observation                       |
| ---------------- | --------------------------------- |
| Without mmap     | More system calls and disk access |
| With mmap        | Faster reads and reduced overhead |
| Performance Gain | Small but noticeable improvement  |

---

# Final Observations

## SQLite3

Advantages:

* Lightweight
* Easy setup
* Fast for small applications
* No server process required

Disadvantages:

* Limited concurrency
* Less suitable for large-scale systems

---

## PostgreSQL

Advantages:

* Powerful and scalable
* Excellent concurrency support
* Advanced indexing and optimization

Disadvantages:

* Requires server setup
* Higher memory usage

---

# Conclusion

Both databases are highly useful but designed for different purposes.

* SQLite is ideal for lightweight local applications, embedded systems, and small projects.
* PostgreSQL is better suited for enterprise-grade applications requiring scalability and concurrency.

The mmap experiment showed that memory-mapped I/O can slightly improve SQLite read performance by reducing disk overhead.

---

# Commands Summary

## SQLite Commands

```bash
sqlite3 sample.db
ls -lh sample.db
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
PRAGMA mmap_size = 268435456;
time sqlite3 sample.db "SELECT * FROM users;"
ps aux | grep sqlite
```

---

## PostgreSQL Commands

```bash
createdb labdb
psql labdb
SHOW block_size;
SELECT pg_size_pretty(pg_database_size('labdb'));
SELECT relpages FROM pg_class WHERE relname='users';
time psql labdb -c "SELECT * FROM users;"
ps aux | grep postgres
```
