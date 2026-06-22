# PostgreSQL vs SQLite: Architecture Comparison and Design Trade-offs

## 1. Problem Background

Relational Database Management Systems (RDBMS) are essential for storing, managing, and retrieving structured data efficiently. Different database systems are designed with different goals and constraints, leading to distinct architectural decisions and trade-offs.

PostgreSQL and SQLite are two widely used open-source relational database systems that represent fundamentally different approaches to database design.

PostgreSQL is an enterprise-grade database management system designed for scalability, reliability, high concurrency, and advanced SQL functionality. It follows a client-server architecture and is commonly used in enterprise applications, web services, analytics platforms, and large-scale systems.

SQLite is a lightweight embedded database engine designed to operate without a separate server process. It is commonly used in mobile applications, desktop software, browsers, IoT devices, and embedded systems where simplicity and portability are important.

This study compares PostgreSQL and SQLite from an architectural perspective, focusing on their storage mechanisms, concurrency models, transaction processing, durability guarantees, and real-world use cases.

---

# 2. Architecture Overview

## PostgreSQL Architecture

PostgreSQL follows a client-server architecture where applications communicate with a dedicated database server process.

```text
Application
      |
      v
PostgreSQL Server
      |
      +----------------+
      | Query Planner  |
      +----------------+
      | Query Executor |
      +----------------+
      | Buffer Manager |
      +----------------+
      | Storage Engine |
      +----------------+
               |
               v
             Disk
```

### Key Components

* PostgreSQL Server Process
* Query Planner and Optimizer
* Query Executor
* Shared Buffer Manager
* Write Ahead Log (WAL)
* Storage Engine

This architecture enables multiple users and applications to access the database concurrently.

---

## SQLite Architecture

SQLite follows an embedded architecture.

```text
Application
      |
      v
 SQLite Library
      |
      v
 Database File
```

### Key Components

* SQLite Library
* Pager Module
* B-Tree Manager
* Database File

The SQLite engine is linked directly into the application, eliminating the need for a separate database server.

---

## Architecture Comparison

| Feature            | PostgreSQL      | SQLite    |
| ------------------ | --------------- | --------- |
| Architecture       | Client-Server   | Embedded  |
| Deployment         | Separate Server | No Server |
| Multi-User Support | Excellent       | Limited   |
| Resource Usage     | Higher          | Lower     |
| Administration     | Required        | Minimal   |
| Scalability        | High            | Moderate  |

---

# 3. Internal Design

## Storage Engine

### PostgreSQL

PostgreSQL stores data in page-based heap files. Data is organized into fixed-size pages (typically 8 KB) and managed through a shared buffer pool.

Features:

* Page-based storage
* Heap-organized tables
* Shared buffer cache
* Write Ahead Logging (WAL)

### SQLite

SQLite stores the entire database inside a single file. Pages are managed through a pager subsystem that handles caching and disk operations.

Features:

* Single database file
* Page-based storage
* Embedded storage engine
* Simplified administration

---

## Indexing

### PostgreSQL

PostgreSQL primarily uses B-Tree indexes.

Supported operations:

* Search
* Insert
* Delete
* Range Queries

Indexes are stored separately from table data and are heavily utilized by the query planner.

### SQLite

SQLite also uses B-Tree indexes internally.

Features:

* Efficient primary-key lookups
* Fast range searches
* Lightweight implementation

Both systems rely on B-Tree structures because they provide efficient logarithmic-time search performance.

---

## Transaction Management

### PostgreSQL

PostgreSQL implements Multi-Version Concurrency Control (MVCC).

Benefits:

* Readers do not block writers.
* Writers do not block readers.
* High concurrency.

A consequence of MVCC is the accumulation of old row versions, which are cleaned through VACUUM operations.

### SQLite

SQLite uses file-level locking and transactional journaling.

Benefits:

* Simpler implementation.
* Lower overhead.
* Easy deployment.

Limitations:

* Reduced write concurrency.
* Higher contention under multi-user workloads.

---

## Durability Mechanisms

### PostgreSQL

PostgreSQL uses Write Ahead Logging (WAL).

Workflow:

1. Changes are first written to WAL.
2. Data pages are updated.
3. WAL records are used during recovery.

This guarantees durability and crash recovery.

### SQLite

SQLite supports:

* Rollback Journals
* WAL Mode

These mechanisms ensure ACID-compliant transactions while maintaining simplicity.

---

# 4. Design Trade-Offs

## PostgreSQL Advantages

* Excellent concurrency support
* Strong ACID guarantees
* Advanced SQL features
* High scalability
* Suitable for enterprise workloads

## PostgreSQL Limitations

* More complex deployment
* Higher memory consumption
* Requires server administration

---

## SQLite Advantages

* Zero configuration
* Extremely lightweight
* Single-file database
* Minimal resource usage
* Easy integration into applications

## SQLite Limitations

* Limited concurrent write support
* Reduced scalability
* Less suitable for large multi-user systems

---

## Architectural Trade-Off Analysis

The most significant difference between PostgreSQL and SQLite is their architecture.

PostgreSQL accepts additional complexity and resource consumption in exchange for better scalability, stronger concurrency, and advanced database features.

SQLite prioritizes simplicity, portability, and ease of deployment. By embedding the database engine directly inside the application, it eliminates server overhead and simplifies application development.

As a result:

* PostgreSQL is better suited for enterprise and web-scale systems.
* SQLite is better suited for local, embedded, and mobile applications.

---

# 5. Experiments and Observations

## Experimental Setup

Environment:

* Ubuntu 24.04 (WSL)
* PostgreSQL 16.14
* SQLite 3.45.1

Dataset:

* Table: users
* Records: 100,000

Schema:

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

---

## Query 1: Primary Key Lookup

Query:

```sql
SELECT * FROM users WHERE id = 50000;
```

### Results

| Database   | Execution Time |
| ---------- | -------------- |
| PostgreSQL | 0.039 ms       |
| SQLite     | < 1 ms         |

### PostgreSQL Execution

![PostgreSQL Query Results](images/postgresql_query.png)

### SQLite Execution

![SQLite Query Results](images/sqlite_query.png)

### Observation

Both database systems returned the requested row almost instantly. Since the query filters on the primary key, both systems utilized their B-Tree indexes instead of scanning the entire table.

---

## Query Plan Analysis

### PostgreSQL

The `EXPLAIN ANALYZE` output showed:

```text
Index Scan using users_pkey on users
Index Cond: (id = 50000)
```

This indicates that PostgreSQL used the primary-key index to locate the row efficiently.

### SQLite

The `EXPLAIN QUERY PLAN` output showed:

```text
SEARCH users USING INTEGER PRIMARY KEY (rowid=?)
```

This indicates that SQLite also used its primary-key index.

### Observation

Both systems successfully chose an index-based access path, avoiding a full table scan. This significantly reduced query execution time and demonstrates the effectiveness of B-Tree indexing in relational databases.

---

## Query 2: Aggregate Query

Query:

```sql
SELECT COUNT(*) FROM users;
```

### Results

| Database   | Execution Time |
| ---------- | -------------- |
| PostgreSQL | 16.526 ms      |
| SQLite     | 5 ms           |

### Observation

SQLite showed lower latency for this workload because it operates as an embedded library and avoids the overhead associated with a client-server architecture.

However, this experiment involved:

* A single user
* A relatively small dataset
* No concurrent transactions

Therefore, these results should not be interpreted as evidence that SQLite is universally faster. PostgreSQL is designed to support significantly larger workloads and concurrent access patterns.

---

# 6. Key Learnings

This comparison highlighted how architectural decisions influence database behavior, performance, and scalability.

Key takeaways include:

1. Database architecture has a major impact on performance characteristics.
2. PostgreSQL prioritizes scalability and concurrency through a client-server model.
3. SQLite prioritizes simplicity and portability through an embedded design.
4. Both systems use B-Tree indexing to achieve efficient query execution.
5. Query optimizers in both databases successfully selected index-based access paths.
6. There is no universally superior database system; the optimal choice depends on workload requirements and deployment constraints.

---

# References

1. PostgreSQL Official Documentation
2. SQLite Official Documentation
3. PostgreSQL MVCC Documentation
4. SQLite File Format Documentation
5. PostgreSQL WAL Documentation
