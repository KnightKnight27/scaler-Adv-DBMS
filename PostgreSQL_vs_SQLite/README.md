# PostgreSQL vs SQLite Architecture Comparison

## Name: Varun Uday Shet

## Topic: PostgreSQL vs SQLite Architecture Comparison

---

# 1. Problem Background

Databases are used to store, retrieve, and manage data efficiently. While both PostgreSQL and SQLite are relational databases that support SQL, they were designed with very different goals in mind.

SQLite was created as a lightweight, embedded database that can be integrated directly into an application without requiring a separate server process. Its primary goal is simplicity, portability, and ease of deployment.

PostgreSQL, on the other hand, was designed as a full-featured database server capable of handling large amounts of data, multiple users, complex queries, and high levels of concurrency. It focuses on scalability, reliability, and advanced database functionality.

Although both systems support SQL, their internal architecture, storage mechanisms, and concurrency models differ significantly. Understanding these differences helps explain why SQLite is commonly used in mobile applications while PostgreSQL is preferred for large-scale backend systems.

---

# 2. Architecture Overview

## PostgreSQL Architecture

PostgreSQL follows a client-server architecture.

```text
+-------------+
|   Client    |
+-------------+
       |
       v
+------------------+
| PostgreSQL Server|
+------------------+
       |
       +----------------+
       | Buffer Manager |
       +----------------+
       |
       +----------------+
       | Query Planner  |
       +----------------+
       |
       +----------------+
       | Storage Engine |
       +----------------+
       |
       v
+------------------+
| Data Files + WAL |
+------------------+
```

Clients communicate with the PostgreSQL server through TCP or Unix sockets. The server is responsible for query processing, transaction management, concurrency control, caching, and storage.

---

## SQLite Architecture

SQLite follows an embedded database architecture.

```text
+--------------------------+
|      Application         |
|                          |
|   +------------------+   |
|   | SQLite Library   |   |
|   +------------------+   |
|            |             |
|            v             |
|      Database File       |
+--------------------------+
```

Instead of running as a separate process, SQLite is linked directly into the application. The application interacts with the database through library function calls, and SQLite reads and writes directly to the database file.

This design removes network communication overhead and makes deployment extremely simple.

---

# 3. Internal Design

## Storage Engine

### PostgreSQL

PostgreSQL stores data in heap-organized tables. Data is divided into fixed-size pages, typically 8 KB each. Multiple pages form relation files stored on disk.

When a query accesses data, pages are first loaded into shared buffers. Frequently accessed pages remain in memory to improve performance.

PostgreSQL also maintains a Write Ahead Log (WAL) which records changes before they are written to the main data files.

### SQLite

SQLite stores the entire database inside a single file. The file is organized into fixed-size pages connected using B-Tree structures.

During experimentation, the following values were observed:

| PRAGMA Command | Result |
| -------------- | ------ |
| page_size      | 4096   |
| page_count     | 2      |
| journal_mode   | delete |
| cache_size     | 2000   |

These observations show that SQLite stores information in fixed-size 4 KB pages and uses a page cache to reduce disk access.

---

## Index Organization

### PostgreSQL

PostgreSQL supports multiple index types including:

- B-Tree
- Hash
- GIN
- GiST
- BRIN

The default and most commonly used index is the B-Tree index. It provides efficient searching, insertion, and deletion operations.

### SQLite

SQLite primarily uses B-Tree structures for both table storage and indexes. Since the database is stored in a single file, indexes are maintained inside the same file structure.

This keeps the implementation simple while providing efficient lookup performance.

---

## Transaction Management

### PostgreSQL

PostgreSQL uses Multi-Version Concurrency Control (MVCC).

Instead of modifying rows directly, updates create new versions of rows. Older versions remain available to transactions that started earlier.

This allows:

- High concurrency
- Consistent reads
- Reduced locking

Changes are first written to the WAL before being applied to the data files, ensuring durability.

### SQLite

SQLite supports ACID transactions through journaling.

By default, SQLite uses a rollback journal. Before modifying data, it records the original state so changes can be reversed if a crash occurs.

SQLite can also operate in WAL mode, which improves concurrency by allowing readers and writers to work simultaneously in many cases.

---

## Concurrency Control

### PostgreSQL

PostgreSQL is designed for multi-user environments.

It supports:

- Many concurrent readers
- Many concurrent writers
- Snapshot isolation
- MVCC-based concurrency

This makes PostgreSQL suitable for web applications and enterprise systems where thousands of users may access the database simultaneously.

### SQLite

SQLite supports multiple concurrent readers but allows only one writer at a time.

This limitation exists because all changes ultimately affect the same database file.

For small applications this is usually not a problem, but write-heavy workloads can experience contention.

---

## Durability Mechanisms

### PostgreSQL

PostgreSQL uses Write Ahead Logging (WAL).

The process is:

1. Change is recorded in WAL.
2. WAL is flushed to disk.
3. Data pages are updated later.

If a crash occurs, PostgreSQL replays WAL records to restore consistency.

### SQLite

SQLite uses rollback journals or WAL mode.

Rollback journals store the previous state of data before modification. If a failure occurs, SQLite restores the previous state using the journal.

This ensures transactional consistency even when the application terminates unexpectedly.

---

# 4. Design Trade-Offs

| Feature        | PostgreSQL            | SQLite              |
| -------------- | --------------------- | ------------------- |
| Architecture   | Client-Server         | Embedded            |
| Deployment     | Requires server setup | Single file         |
| Concurrency    | High                  | Limited writes      |
| Scalability    | Excellent             | Moderate            |
| Resource Usage | Higher                | Very low            |
| Administration | Requires management   | Minimal             |
| Typical Usage  | Backend systems       | Mobile/Desktop apps |

---

## Advantages of PostgreSQL

- Excellent support for concurrent users.
- Advanced indexing options.
- Rich SQL feature set.
- Strong transaction support.
- Highly scalable.
- Extensive extension ecosystem.

---

## Advantages of SQLite

- Extremely lightweight.
- No separate installation required.
- Entire database stored in a single file.
- Low memory usage.
- Very easy deployment.
- Ideal for embedded applications.

---

## Limitations of PostgreSQL

- More complex setup.
- Requires server administration.
- Higher resource consumption.
- Greater operational overhead.

---

## Limitations of SQLite

- Single-writer limitation.
- Not suitable for large multi-user systems.
- Limited scalability compared to PostgreSQL.
- Fewer advanced database features.

---

# 5. Experiments and Observations

## SQLite Experiment

A small database was created with a student table containing five records.

Commands executed:

```sql
CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT,
    marks INTEGER
);

INSERT INTO students VALUES
(1,'Tanmay',90),
(2,'Rahul',85),
(3,'Aman',95),
(4,'Priya',88),
(5,'Riya',92);

PRAGMA page_size;
PRAGMA page_count;
PRAGMA journal_mode;
PRAGMA cache_size;
```

Results:

```text
page_size   = 4096
page_count  = 2
journal_mode = delete
cache_size  = 2000
```

Observations:

- SQLite stores data in 4 KB pages.
- The database occupied only two pages for this small dataset.
- Rollback journaling was enabled by default.
- SQLite maintains an internal page cache for performance.

---

## PostgreSQL Experiment

A similar table was created in PostgreSQL and analyzed using EXPLAIN ANALYZE.

Query:

```sql
EXPLAIN ANALYZE
SELECT * FROM students
WHERE marks > 88;
```

Output:

```text
Seq Scan on students
Rows Returned: 3
Rows Removed by Filter: 2
Planning Time: 1.150 ms
Execution Time: 0.020 ms
```

Observations:

- PostgreSQL selected a Sequential Scan because the table was very small.
- Query planning is performed before execution.
- The optimizer determined that scanning the entire table was cheaper than using an index.
- This demonstrates PostgreSQL's cost-based query planning system.

---

# 6. Why PostgreSQL and SQLite Chose Different Architectures

SQLite was designed for simplicity and portability. By embedding the database directly inside the application, deployment becomes extremely easy. This approach works well when a small number of users access the database and write operations are relatively infrequent.

PostgreSQL was designed for large-scale multi-user environments. Its client-server architecture introduces additional complexity but enables advanced features such as MVCC, sophisticated query optimization, fine-grained access control, and high concurrency.

The architectural differences are a direct result of the problems each system was designed to solve.

---

# 7. Key Learnings

- Database architecture significantly affects performance and scalability.
- SQLite achieves simplicity through its embedded design and single-file storage model.
- PostgreSQL achieves scalability through a client-server architecture and MVCC.
- Write Ahead Logging is a critical component of PostgreSQL's durability guarantees.
- SQLite is ideal for mobile, desktop, and embedded applications.
- PostgreSQL is better suited for large backend systems with many concurrent users.
- Different architectural choices represent different engineering trade-offs rather than one system being universally better than the other.

---

# References

1. PostgreSQL Official Documentation
2. SQLite Official Documentation
3. PostgreSQL Storage and WAL Documentation
4. SQLite PRAGMA Documentation
