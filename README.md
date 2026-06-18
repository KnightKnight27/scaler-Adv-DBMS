# SQLite3 V/S PostgreSQL Comparison

## Objective

The objective of this lab is to explore and compare SQLite3 and PostgreSQL with respect to:

* Database storage structure
* Page size and page count
* Query execution performance
* Memory-mapped I/O (mmap) in SQLite
* Process architecture
* Overall performance comparison

---

# Part 1: SQLite3 Exploration

## Installation

SQLite3 was installed on Ubuntu (WSL) using:

```bash
sudo apt update
sudo apt install sqlite3 -y
```

Verification:

```bash
sqlite3 --version
```

Output:

```text
3.37.2 2022-01-06 13:25:41
```

---

## Creating Sample Database

Database Creation:

```bash
sqlite3 test.db
```

Table Creation:

```sql
CREATE TABLE users(
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT
);
```

Data Insertion:

```sql
INSERT INTO users (name, email) VALUES
('Alice', 'alice@example.com'),
('Bob', 'bob@example.com'),
('Charlie', 'charlie@example.com');
```

Verify Data:

```sql
SELECT * FROM users;
```

Output:

```text
1|Alice|alice@example.com
2|Bob|bob@example.com
3|Charlie|charlie@example.com
```

---

## Database File Size

Command:

```bash
ls -lh test.db
```

Output:

```text
-rw-r--r-- 1 nirbhay nirbhay 8.0K May 9 21:28 test.db
```

Observation:

* SQLite stores the entire database in a single file.
* Current database size is approximately 8 KB.

---

## Page Information

Check Page Size:

```sql
PRAGMA page_size;
```

Output:

```text
4096
```

Check Page Count:

```sql
PRAGMA page_count;
```

Output:

```text
2
```

Database Size Calculation:

```text
Database Size = Page Size × Page Count
              = 4096 × 2
              = 8192 Bytes (8 KB)
```

Observation:

* SQLite uses a default page size of 4096 bytes (4 KB).
* The database currently occupies 2 pages.
* Database size is determined by page size multiplied by page count.

---

## Database Metadata

Database List:

```sql
PRAGMA database_list;
```

Output:

```text
0|main|/home/nirbhay/test.db
```

Observation:

* Confirms the location of the SQLite database file.
* SQLite stores all data within a single database file.

---

## Table Schema Information

Command:

```sql
PRAGMA table_info(users);
```

Output:

```text
0|id|INTEGER|0||1
1|name|TEXT|0||0
2|email|TEXT|0||0
```

Observation:

* Displays metadata about the table structure.
* Shows column names, data types, and primary key information.

---

## Memory-Mapped I/O (mmap)

Check Current mmap Size:

```sql
PRAGMA mmap_size;
```

Output:

```text
0
```

Enable mmap:

```sql
PRAGMA mmap_size = 1000000;
```

Output:

```text
1000000
```

Verify:

```sql
PRAGMA mmap_size;
```

Output:

```text
1000000
```

Observation:

* mmap was successfully enabled with a size of 1,000,000 bytes.
* Memory-mapped I/O allows SQLite to access database pages directly through virtual memory.
* mmap can improve read performance for larger databases.

---

## Query Execution Time

Command:

```bash
time sqlite3 test.db "SELECT * FROM users;"
```

Output:

```text
1|Alice|alice@example.com
2|Bob|bob@example.com
3|Charlie|charlie@example.com

real    0m0.002s
user    0m0.000s
sys     0m0.002s
```

Observation:

* Query execution time is extremely low because of the small dataset.
* mmap benefits are not noticeable on very small databases.

---

## SQLite Process Information

Command:

```bash
ps aux | grep sqlite
```

Output:

```text
nirbhay      591  0.0  0.0   4028  2316 pts/0    S+   11:32   0:00 grep --color=auto sqlite
```

Observation:

* No dedicated SQLite server process exists.
* SQLite operates as an embedded database engine inside the application process.
* This demonstrates SQLite's serverless architecture.

---

# Part 2: PostgreSQL Exploration

## Installation

PostgreSQL was installed using:

```bash
sudo apt update
sudo apt install postgresql postgresql-contrib -y
```

Start PostgreSQL Service:

```bash
sudo service postgresql start
```

Connect:

```bash
sudo -i -u postgres
psql
```

---

## Database Creation

Create Database:

```sql
CREATE DATABASE testdb;
```

Output:

```text
ERROR: database "testdb" already exists
```

Connect to Existing Database:

```sql
\c testdb
```

Output:

```text
You are now connected to database "testdb" as user "postgres".
```

---

## Creating Sample Table

Table Creation:

```sql
CREATE TABLE users(
    id SERIAL PRIMARY KEY,
    name TEXT,
    email TEXT
);
```

Insert Data:

```sql
INSERT INTO users(name,email)
VALUES
('Alice','alice@example.com'),
('Bob','bob@example.com'),
('Charlie','charlie@example.com');
```

Verify Data:

```sql
SELECT * FROM users;
```

Output:

```text
 id |  name   |        email
----+---------+---------------------
  1 | Alice   | alice@example.com
  2 | Bob     | bob@example.com
  3 | Charlie | charlie@example.com
(3 rows)
```

---

## PostgreSQL Page Size

Command:

```sql
SHOW block_size;
```

Output:

```text
8192
```

Observation:

* PostgreSQL uses a fixed page size of 8192 bytes (8 KB).

---

## Table Size

Command:

```sql
SELECT pg_size_pretty(
       pg_total_relation_size('users')
);
```

Output:

```text
32 kB
```

Observation:

* PostgreSQL stores data with additional metadata and system overhead.
* Storage size is larger than SQLite for the same dataset.

---

## Query Execution Time

Enable Timing:

```sql
\timing
```

Output:

```text
Timing is on.
```

Run Query:

```sql
SELECT * FROM users;
```

Output:

```text
Time: 0.493 ms
```

Observation:

* PostgreSQL query execution is fast.
* PostgreSQL is optimized for larger datasets and concurrent workloads.

---

## PostgreSQL Process Architecture

Command:

```bash
ps aux | grep postgres
```

Relevant Processes Observed:

```text
postgres
checkpointer
background writer
walwriter
autovacuum launcher
stats collector
logical replication launcher
```

Observation:

* PostgreSQL follows a client-server architecture.
* Multiple background processes manage storage, logging, maintenance, statistics, and replication.

---

# SQLite3 vs PostgreSQL Comparison

| Feature           | SQLite3               | PostgreSQL                 |
| ----------------- | --------------------- | -------------------------- |
| Architecture      | Serverless            | Client-Server              |
| Storage           | Single File           | Multiple Files             |
| Default Page Size | 4096 Bytes            | 8192 Bytes                 |
| Page Count        | 2                     | Dynamic                    |
| Database Size     | 8 KB                  | 32 KB Table                |
| Memory Mapping    | Supported (mmap)      | Internal Buffer Management |
| Query Time        | 0.002 s               | 0.493 ms                   |
| Concurrent Reads  | Yes                   | Yes                        |
| Concurrent Writes | Limited               | High                       |
| Server Process    | Not Required          | Required                   |
| Scalability       | Low to Medium         | High                       |
| Best Use Case     | Embedded Applications | Enterprise Applications    |

---

# Analysis of mmap Impact

SQLite supports memory-mapped I/O through:

```sql
PRAGMA mmap_size;
```

Benefits:

* Reduces system calls
* Direct access to database pages
* Potentially faster read performance

Experimental Result:

```text
Before mmap: 0
After mmap : 1000000
```

Observation:

* mmap was successfully enabled.
* No measurable improvement was observed because the database contains only three records.
* mmap becomes more beneficial with larger datasets and read-heavy workloads.

PostgreSQL does not expose mmap configuration directly because it relies on:

* Shared Buffers
* Buffer Cache
* Internal Memory Management



# Observations

## SQLite Advantages

* Lightweight and simple
* No server installation required
* Single-file database
* Very small storage footprint
* Fast for small applications

### SQLite Limitations

* Limited write concurrency
* Less suitable for large-scale applications



## PostgreSQL Advantages

* High scalability
* Excellent concurrency support
* Advanced indexing and optimization
* Strong transactional support
* Suitable for enterprise applications

### PostgreSQL Limitations

* Requires a server process
* Higher storage and resource overhead



# Conclusion

SQLite3 and PostgreSQL are designed for different use cases.

SQLite uses a lightweight serverless architecture and stores the entire database inside a single file. It uses a default page size of 4096 bytes and supports memory-mapped I/O through `PRAGMA mmap_size`. In this experiment, the database occupied only 8 KB and executed queries in approximately 0.002 seconds.

PostgreSQL follows a client-server architecture with multiple background processes such as the checkpointer, WAL writer, autovacuum launcher, and logical replication launcher. It uses a fixed page size of 8192 bytes and managed the same dataset with a table size of 32 KB. Query execution time was approximately 0.493 milliseconds.

Therefore:

* SQLite is best suited for lightweight, embedded, and standalone applications.
* PostgreSQL is better suited for enterprise systems requiring scalability, reliability, and concurrent access.

The experiment demonstrates that SQLite prioritizes simplicity and portability, while PostgreSQL prioritizes scalability, concurrency, and advanced database features.

This version uses the actual values you collected during the lab and is ready to save as `README.md`.
