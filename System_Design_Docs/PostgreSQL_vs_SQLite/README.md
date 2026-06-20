# PostgreSQL vs SQLite Architecture Comparison

## Introduction

Relational Database Management Systems (RDBMS) can be broadly categorized into embedded databases and client-server databases. SQLite and PostgreSQL are popular examples of these two architectural approaches.

The objective of this report is to compare PostgreSQL and SQLite in terms of architecture, process model, storage organization, indexing, transaction management, concurrency control, durability, scalability, and real-world use cases.

---

# 1. Overall Architecture

## SQLite

SQLite is an **embedded database engine**. The database engine is linked directly into the application and accesses a single database file without requiring a separate server process.

```text
Application
    │
SQLite Library
    │
sample.db
```

### Characteristics

* No dedicated server process
* Entire database stored in one file
* Lightweight and portable
* Minimal resource consumption

---

## PostgreSQL

PostgreSQL follows a **client-server architecture** where clients communicate with a dedicated database server.

```text
Client
   │
PostgreSQL Server
   │
Database Files
```

### Characteristics

* Dedicated database server
* Supports multiple clients simultaneously
* Centralized administration
* Enterprise-grade features

---

# 2. Process Model

## SQLite

SQLite runs inside the application's process space.

### Observations

* No separate database server
* No background processes
* Very low memory and CPU usage

### Advantages

* Lightweight execution
* Simple deployment

### Disadvantages

* Limited support for concurrent operations

---

## PostgreSQL

Using the command:

```bash
ps aux | grep postgres
```

the following PostgreSQL processes were observed:

* Main Server Process
* Checkpointer
* Background Writer
* WAL Writer
* Autovacuum Launcher
* Logical Replication Launcher
* Client Backend Process

### Advantages

* Better concurrency support
* Automatic maintenance
* Improved reliability
* Crash recovery support

### Disadvantages

* Higher memory and CPU usage

---

# 3. Client-Server vs Embedded Design

## Why SQLite Uses Embedded Design

SQLite was designed for:

* Mobile applications
* Desktop software
* Embedded systems
* Local data storage

### Benefits

* Zero configuration
* Small memory footprint
* Easy deployment
* Portable database file

---

## Why PostgreSQL Uses Client-Server Design

PostgreSQL was designed for:

* Enterprise applications
* Web applications
* Multi-user systems

### Benefits

* Multiple simultaneous users
* Centralized security and management
* Network accessibility
* Better scalability

---

# 4. Storage Engine Architecture

## SQLite

Experimental Results:

| Metric        | Value      |
| ------------- | ---------- |
| Page Size     | 4096 bytes |
| Page Count    | 63         |
| Database Size | 252 KB     |

Storage Calculation:

```text
63 × 4096 = 258,048 bytes ≈ 252 KB
```

SQLite stores tables and indexes using B-tree structures inside a single database file.

---

## PostgreSQL

Experimental Results:

| Metric     | Value         |
| ---------- | ------------- |
| Block Size | 8192 bytes    |
| relpages   | 73            |
| Table Size | 598,016 bytes |

Storage Calculation:

```text
73 × 8192 = 598,016 bytes
```

PostgreSQL uses larger pages and stores additional metadata required for MVCC and transaction management.

---

# 5. Database File Organization

## SQLite

SQLite stores everything inside a single database file:

```text
sample.db
```

The file contains:

* Tables
* Indexes
* Metadata
* Transaction information

### Benefits

* Easy backup
* Easy migration
* Highly portable

---

## PostgreSQL

PostgreSQL stores data across multiple directories:

```text
base/
global/
pg_wal/
pg_xact/
```

### Benefits

* Better scalability
* Better crash recovery
* Advanced storage management

---

# 6. Table Storage and Page Layout

## SQLite

Tables are stored as B-tree pages.

### Page Layout

```text
Page Header
Cell Pointer Array
Records
Free Space
```

### Page Size

```text
4096 bytes
```

---

## PostgreSQL

Tables are stored as heap pages.

### Page Layout

```text
Page Header
Item Pointers
Tuple Data
Free Space
```

### Page Size

```text
8192 bytes
```

---

# 7. Index Implementation

## SQLite

Index Creation:

```sql
CREATE INDEX idx_name ON users(name);
```

Query Plan:

```text
SEARCH users USING INDEX idx_name (name=?)
```

This confirms that SQLite uses the index instead of performing a full table scan.

---

## PostgreSQL

Index Creation:

```sql
CREATE INDEX idx_name ON users(name);
```

Execution Plan:

```text
Index Scan using idx_name on users
Execution Time: 0.082 ms
```

This confirms that PostgreSQL successfully used the index for efficient lookup.

### Additional PostgreSQL Index Types

* B-tree
* Hash
* GiST
* SP-GiST
* GIN
* BRIN

SQLite primarily supports B-tree indexes.

---

# 8. Transaction Management

## SQLite

SQLite supports ACID transactions using:

* Rollback Journal
* Write-Ahead Logging (WAL)

Observed Journal Mode:

```text
delete
```

SQLite allows only one active writer at a time.

---

## PostgreSQL

PostgreSQL uses:

* Write-Ahead Logging (WAL)
* Multi-Version Concurrency Control (MVCC)

This enables better concurrency and transaction isolation.

---

# 9. Concurrency Control

## SQLite

Experiment:

### Session 1

```sql
BEGIN TRANSACTION;
UPDATE users SET age = 30 WHERE id = 1;
```

### Session 2

```sql
UPDATE users SET age = 31 WHERE id = 2;
```

Output:

```text
Runtime error: database is locked (5)
```

### Conclusion

SQLite follows a:

```text
Many Readers
One Writer
```

concurrency model.

---

## PostgreSQL

PostgreSQL uses **MVCC (Multi-Version Concurrency Control)**.

### Benefits

* Multiple readers
* Multiple concurrent transactions
* Reduced locking overhead
* Better multi-user performance

This makes PostgreSQL more suitable for large-scale applications.

---

# 10. Durability Mechanisms

## SQLite

Durability is provided through:

* Rollback Journal
* WAL Mode
* fsync operations

These mechanisms ensure committed transactions survive crashes.

---

## PostgreSQL

Durability is provided through:

* WAL Writer
* Checkpointer
* Crash Recovery System

Observed background processes:

* WAL Writer
* Checkpointer

These mechanisms provide strong durability guarantees.

---

# 11. Experimental Results Summary

| Metric               | SQLite     | PostgreSQL               |
| -------------------- | ---------- | ------------------------ |
| Version              | 3.45.1     | 16.14                    |
| Architecture         | Embedded   | Client-Server            |
| Page Size            | 4096 B     | 8192 B                   |
| Pages                | 63         | 73                       |
| Storage              | 252 KB     | 598 KB                   |
| Rows                 | 10,005     | 10,005                   |
| Query Time           | 17 ms      | Indexed Lookup: 0.082 ms |
| Index Usage          | Yes        | Yes                      |
| Background Processes | None       | Multiple                 |
| Concurrency          | One Writer | MVCC                     |
| Resource Usage       | Low        | Higher                   |

---

# 12. Scalability Implications

## SQLite

Suitable for:

* Mobile applications
* Desktop software
* Embedded systems
* Local storage

### Limitations

* Single-writer model
* Limited scalability
* Not ideal for heavy multi-user workloads

---

## PostgreSQL

Suitable for:

* Enterprise applications
* Banking systems
* ERP platforms
* Cloud services
* Large web applications

### Advantages

* High concurrency
* Better scalability
* Advanced optimization features

---

# 13. Real-World Use Cases

## SQLite

Commonly used in:

* Android applications
* iOS applications
* Embedded devices
* Browser storage

### Why?

* Lightweight
* Serverless
* Portable

---

## PostgreSQL

Commonly used in:

* Enterprise software
* SaaS platforms
* Financial systems
* E-commerce applications

### Why?

* Reliability
* Scalability
* High concurrency

---

# 14. Answers to Suggested Questions

## Why does SQLite work well for mobile applications?

SQLite is lightweight, serverless, requires very little memory, and stores all data in a single file. These characteristics make it ideal for mobile and embedded environments.

---

## Why is PostgreSQL preferred for large multi-user systems?

PostgreSQL supports concurrent transactions using MVCC, provides advanced indexing, strong durability guarantees, and scales efficiently to large workloads.

---

## What architectural decisions lead to these differences?

SQLite prioritizes simplicity, portability, and low resource usage through an embedded architecture.

PostgreSQL prioritizes concurrency, reliability, and scalability through a client-server architecture with multiple background processes, MVCC, WAL, and advanced storage management.

---

# Conclusion

SQLite and PostgreSQL are both powerful relational databases, but they are designed for different use cases.

SQLite is optimized for simplicity, portability, and lightweight deployments, making it ideal for mobile applications and embedded systems.

PostgreSQL is optimized for concurrency, scalability, reliability, and enterprise workloads, making it a preferred choice for large multi-user applications.

The experimental results demonstrated that SQLite uses a lightweight embedded architecture with single-writer concurrency, while PostgreSQL uses a client-server architecture with multiple background processes, advanced indexing, and superior scalability.
