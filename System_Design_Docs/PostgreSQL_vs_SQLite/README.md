# PostgreSQL vs SQLite Architecture Comparison

## 1. Problem Background

Relational databases are designed to store, manage, and retrieve structured data reliably. However, different applications impose different requirements. Some applications require high concurrency, distributed access, and scalability, while others prioritize simplicity, portability, and minimal resource consumption.

PostgreSQL and SQLite are both relational database management systems, but they were built with fundamentally different goals.

PostgreSQL is an enterprise-grade database designed for multi-user environments where multiple clients may access and modify data simultaneously. It follows a client-server architecture and provides advanced features such as MVCC, Write-Ahead Logging (WAL), sophisticated query optimization, and support for large-scale workloads.

SQLite was designed as an embedded database engine. Instead of running as a separate server, SQLite is linked directly into the application process and stores the entire database in a single file. It prioritizes simplicity, portability, and low operational overhead.

The architectural decisions taken by both systems directly influence their storage design, concurrency model, scalability characteristics, and performance behavior.

---

# 2. Architecture Overview

## PostgreSQL Architecture

```text
+--------------------+
| Client Application |
+--------------------+
          |
          v
+--------------------+
| PostgreSQL Server  |
+--------------------+
          |
   +------+------+
   |             |
   v             v
Buffer Manager Query Executor
   |             |
   +------+------+
          |
          v
      Storage
```

Characteristics:

* Dedicated server process
* Multiple client connections
* Shared memory architecture
* Background workers
* WAL subsystem
* MVCC-based concurrency

---

## SQLite Architecture

```text
+------------------+
| Application Code |
+------------------+
          |
          v
+------------------+
| SQLite Library   |
+------------------+
          |
          v
   Database File
```

Characteristics:

* No separate server
* Embedded library
* Direct file access
* Single database file
* Minimal memory footprint

---

# 3. Internal Design Analysis

## 3.1 Process Model

### PostgreSQL

PostgreSQL follows a client-server architecture.

Important processes include:

* Postmaster
* Backend worker processes
* Checkpointer
* WAL Writer
* Background Writer
* Autovacuum Workers

Advantages:

* Supports multiple users simultaneously
* Better isolation
* Centralized management

Trade-offs:

* Higher memory consumption
* Additional IPC overhead

---

### SQLite

SQLite executes inside the application's process space.

Advantages:

* No server installation
* No network overhead
* Extremely lightweight

Trade-offs:

* Limited concurrency
* Not suitable for high-write workloads

---

## 3.2 Storage Architecture

### PostgreSQL

Data is stored using heap-organized tables.

Default page size:

```text
8 KB
```

Each page contains:

```text
Page Header
Item Pointers
Free Space
Tuple Data
```

Tables and indexes are stored separately.

---

### SQLite

SQLite stores the entire database inside a single file.

Observed:

```text
Page Size = 4096 bytes
Page Count = 751
Database Size ≈ 3 MB
```

SQLite organizes data using B-Tree pages.

Advantages:

* Easy backup
* Easy portability
* Simpler storage management

---

## 3.3 Index Organization

### PostgreSQL

Index created:

```sql
CREATE INDEX idx_users_age
ON users(age);
```

Query:

```sql
EXPLAIN ANALYZE
SELECT *
FROM users
WHERE age = 50;
```

Execution Plan:

```text
Bitmap Heap Scan
    -> Bitmap Index Scan
```

Observation:

PostgreSQL first used the index to identify matching row locations and then fetched the actual tuples from heap pages.

This occurs because PostgreSQL stores table data separately from indexes.

Benefits:

* Efficient for large result sets
* Reduced random I/O

Trade-off:

* Additional heap lookup required

---

### SQLite

Query:

```sql
EXPLAIN QUERY PLAN
SELECT *
FROM users
WHERE age = 50;
```

Output:

```text
SEARCH users USING INDEX idx_users_age (age=?)
```

Observation:

SQLite directly navigated its B-Tree index structure to locate matching rows.

Benefits:

* Simpler access path
* Lower implementation complexity

Trade-off:

* Fewer advanced optimization techniques compared to PostgreSQL

---

## 3.4 Transaction Management

### PostgreSQL

Uses:

* MVCC
* WAL
* Snapshot Isolation

Tuple metadata:

```text
xmin
xmax
```

Each update creates a new tuple version.

Readers access a consistent snapshot without blocking writers.

---

### SQLite

Uses:

* Rollback Journals
* WAL Mode (optional)

Concurrency primarily relies on file locking.

Simpler design but less scalable.

---

## 3.5 Concurrency Control

This experiment clearly demonstrates the biggest architectural difference between PostgreSQL and SQLite.

---

### PostgreSQL Experiment

Terminal 1:

```sql
BEGIN;

UPDATE users
SET age = age + 1
WHERE id = 1;
```

Transaction remained uncommitted.

Terminal 2:

```sql
SELECT *
FROM users
WHERE id = 1;
```

Output:

```text
id | name   | age
1  | User_1 | 1
```

### Observation

The query succeeded immediately.

The reader observed the previously committed version of the row instead of waiting for the update transaction to complete.

### Explanation

PostgreSQL uses Multi-Version Concurrency Control (MVCC).

When the update occurs:

```text
Old Version -> Remains Visible
New Version -> Visible After Commit
```

Readers and writers operate independently.

Advantages:

* High concurrency
* No reader-writer blocking
* Excellent scalability

---

### SQLite Experiment

Terminal 1:

```sql
BEGIN TRANSACTION;

UPDATE users
SET age = age + 1
WHERE id = 1;
```

Transaction remained open.

Terminal 2:

```sql
UPDATE users
SET age = age + 1
WHERE id = 2;
```

Output:

```text
Runtime error: database is locked (5)
```

### Observation

The second writer was blocked.

### Explanation

SQLite allows multiple readers but only one writer at a time.

This behavior results from SQLite's file-level locking mechanism.

Advantages:

* Simpler implementation
* Minimal metadata overhead

Trade-off:

* Limited write concurrency

---

# 4. Experiments and Observations

## Dataset

Table:

```sql
users(id, name, age)
```

Records inserted:

```text
100,000
```

in both PostgreSQL and SQLite.

---

## Experiment 1: Index-Based Lookup

Query:

```sql
SELECT *
FROM users
WHERE age = 50;
```

### PostgreSQL

Execution Plan:

```text
Bitmap Index Scan
Bitmap Heap Scan
```

Execution Time:

```text
6.655 ms
```

Rows Returned:

```text
983
```

Observation:

The planner estimated 1033 rows and found 983 rows.

The estimate was very close, indicating accurate statistics.

---

### SQLite

Execution Plan:

```text
SEARCH users USING INDEX idx_users_age
```

Observation:

SQLite successfully used the index and avoided a full table scan.

---

## Experiment 2: Storage Comparison

### PostgreSQL

Database Size:

```text
15 MB
```

Table Size:

```text
8064 KB
```

---

### SQLite

Database File Size:

```text
~3 MB
```

Page Size:

```text
4096 bytes
```

Page Count:

```text
751
```

Observation:

SQLite required significantly less storage space due to its simpler architecture and absence of server-side metadata.

PostgreSQL consumes more space because of:

* MVCC metadata
* WAL support
* System catalogs
* Additional storage structures

---

## Experiment 3: Concurrent Transactions

### PostgreSQL

Result:

```text
Read operation succeeded during uncommitted update.
```

### SQLite

Result:

```text
database is locked
```

Observation:

This experiment clearly demonstrates PostgreSQL's superior concurrency model.

---

# 5. Design Trade-Offs

| Aspect           | PostgreSQL         | SQLite           |
| ---------------- | ------------------ | ---------------- |
| Architecture     | Client-Server      | Embedded         |
| Storage          | Multiple files     | Single file      |
| Concurrency      | MVCC               | File Locks       |
| Multiple Writers | Yes                | No               |
| Scalability      | High               | Limited          |
| Resource Usage   | Higher             | Very Low         |
| Administration   | Required           | Minimal          |
| Query Optimizer  | Advanced           | Simpler          |
| Best Workload    | Multi-user systems | Embedded systems |

---

# 6. Real-World Use Cases

## PostgreSQL

Suitable for:

* Banking systems
* E-commerce platforms
* ERP systems
* SaaS products
* Analytics systems

Reason:

High concurrency and scalability requirements.

---

## SQLite

Suitable for:

* Mobile applications
* Desktop applications
* Browser storage
* Embedded devices
* Local caches

Reason:

Low operational overhead and portability.

---

# 7. Key Learnings

1. Database architecture is driven by workload requirements rather than raw performance.

2. PostgreSQL sacrifices simplicity to achieve scalability and concurrency.

3. SQLite sacrifices concurrency to achieve portability and ease of deployment.

4. MVCC is the primary reason PostgreSQL supports large numbers of concurrent users.

5. SQLite's single-file design makes it ideal for embedded environments.

6. The concurrency experiment was the clearest demonstration of architectural differences: PostgreSQL continued serving reads during an uncommitted update, whereas SQLite blocked a second writer with a database lock.

7. There is no universally superior database. The appropriate choice depends entirely on workload characteristics and operational requirements.

# References

1. PostgreSQL Documentation
2. SQLite Documentation
3. PostgreSQL Source Code
4. SQLite Source Code
5. Database System Concepts – Silberschatz, Korth, Sudarshan
