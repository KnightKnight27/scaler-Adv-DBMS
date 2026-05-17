# scaler-Adv-DBMS
# SQLite3 vs PostgreSQL Comparison

This repository contains a hands-on comparison between **SQLite3** and **PostgreSQL**, demonstrating database setup, query performance, memory usage, and process architecture.


## SQLite3 Experiments

### Commands Used

```bash
# Open SQLite database
sqlite3 ~/test.db

# Create table
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT
);

# Insert sample data
INSERT INTO users (name, email) VALUES
('Alice', 'alice@example.com'),
('Bob', 'bob@example.com'),
('Charlie', 'charlie@example.com');

# Verify data
SELECT * FROM users;

# Check database info
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;

# Set memory-mapped I/O to 256 MB
PRAGMA mmap_size = 268435456;

# Query performance
time sqlite3 ~/test.db "PRAGMA mmap_size=0; SELECT * FROM users;"
time sqlite3 ~/test.db "PRAGMA mmap_size=268435456; SELECT * FROM users;"

# Check running SQLite processes
ps aux | grep sqlite
```

### Observations

* **Database File Size:** 8 KB
* **Page Size:** 4096 bytes (4 KB)
* **Page Count:** 2 pages
* **mmap_size:** Default 0, temporarily set to 256 MB; connection-specific
* **Query Performance:**

| mmap  | real   | user   | sys    |
| ----- | ------ | ------ | ------ |
| 0     | 0.001s | 0.000s | 0.001s |
| 256MB | 0.001s | 0.000s | 0.000s |

* **Process Observation:** Single lightweight process; very low memory and CPU usage
* **Conclusion:** SQLite is lightweight, embedded in a single file, and mmap slightly improves query performance.

---

## PostgreSQL Experiments

### Commands Used

```bash
sudo service postgresql start
sudo -u postgres psql

-- Create a test database
CREATE DATABASE testdb;

-- Connect to the database
\c testdb

-- Create table
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    email TEXT
);

-- Insert sample data
INSERT INTO users (name, email) VALUES
('Alice', 'alice@example.com'),
('Bob', 'bob@example.com'),
('Charlie', 'charlie@example.com');

-- Verify data
SELECT * FROM users;

-- Database info
SHOW block_size;

-- Table pages
SELECT relpages FROM pg_class WHERE relname = 'users';

-- Analyze query execution
EXPLAIN ANALYZE SELECT * FROM users;

-- Check running PostgreSQL processes
ps aux | findstr postgres
```

### Observations

* **Block Size:** 8192 bytes (8 KB)
* **Page Count:** 0 (small table, minimal pages used initially)
* **Query Execution:**

```text
Seq Scan on users  (cost=0.00..18.50 rows=850 width=68) 
(actual time=0.014..0.015 rows=3 loops=1)
Planning Time: 0.064 ms
Execution Time: 0.039 ms
```

* **Process Observation:** Multiple background processes exist:

  * `checkpointer`, `background writer`, `walwriter`, `autovacuum launcher`, `logical replication launcher`
* PostgreSQL follows a **client-server architecture**, unlike SQLite.



## Comparison Analysis

| Feature             | SQLite3                    | PostgreSQL                 |
| ------------------- | -------------------------- | -------------------------- |
| Architecture        | Embedded database          | Client-server database     |
| Storage             | Single file                | Multiple internal files    |
| Page Size           | 4096 bytes                 | 8192 bytes                 |
| Page Count          | 2                          | 0 (small table)            |
| mmap Support        | Direct mmap support        | Internal shared buffers    |
| Query Timing        | 0.001s                     | 0.039 ms                   |
| Processes           | Single lightweight process | Multiple server processes  |
| Setup Complexity    | Very simple                | More complex               |
| Concurrency Support | Limited                    | Strong concurrency support |
| Resource Usage      | Very low                   | Higher                     |



## Conclusion

* **SQLite3:** Lightweight, easy to set up, minimal resources, suitable for embedded apps or local testing.
* **PostgreSQL:** Full-featured, multi-process, advanced query planner, strong concurrency, suitable for production and multi-user environments.

From the experiments:

* SQLite was easier to configure and inspect.
* PostgreSQL had a more advanced architecture.
* mmap slightly improved SQLite performance.
* PostgreSQL demonstrated efficient query execution and process management.

---
