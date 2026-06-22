# PostgreSQL vs SQLite Architecture Comparison

## 1. Problem Background

Databases are designed for different environments and workloads. PostgreSQL and SQLite are both relational databases, but they solve different problems.

SQLite was designed as a lightweight embedded database that can be directly linked into an application without requiring a separate server process. It is commonly used in mobile devices, browsers, IoT devices, and desktop applications.

PostgreSQL was designed as a full-featured database server capable of handling multiple concurrent users, large datasets, advanced queries, and enterprise workloads.

The architectural differences between the two systems originate from their target use cases.

---

## 2. Architecture Overview

### PostgreSQL Architecture

Application
↓
PostgreSQL Server
↓
Buffer Manager
↓
Storage Manager
↓
Data Files + WAL

Characteristics:

* Client-server architecture
* Dedicated backend process per connection
* Shared memory buffers
* Supports multiple concurrent users

### SQLite Architecture

Application
↓
SQLite Library
↓
Database File

Characteristics:

* Embedded architecture
* No separate server process
* Database accessed directly through library calls
* Single database file

### Architectural Motivation

PostgreSQL separates clients from storage to support scalability, security, and concurrent access.

SQLite removes the server layer entirely to reduce complexity, memory consumption, deployment effort, and latency.

---

## 3. Internal Design Analysis

### Process Model

#### PostgreSQL

* Separate server process
* Multiple clients connect simultaneously
* Shared buffer pool
* Background workers manage checkpoints and maintenance

Advantages:

* Better concurrency
* Centralized resource management

Trade-off:

* Higher memory overhead
* More operational complexity

#### SQLite

* Runs inside the application process
* No network communication
* No dedicated server

Advantages:

* Extremely lightweight
* Easy deployment

Trade-off:

* Limited concurrent writes

---

### Storage Engine Architecture

#### PostgreSQL

Data is stored in pages (typically 8 KB).

Tables:

* Heap-organized storage
* Multiple files on disk

Indexes:

* Primarily B-Tree indexes

Durability:

* Write Ahead Logging (WAL)

#### SQLite

Single database file.

Storage structure:

* Fixed-size pages
* B-Tree based tables
* B-Tree based indexes

Durability:

* Rollback journal or WAL mode

---

### Concurrency Control

#### PostgreSQL

Uses MVCC (Multi-Version Concurrency Control).

Readers:

* Do not block writers

Writers:

* Create new tuple versions

Benefits:

* High concurrency
* Better performance for multi-user systems

Cost:

* Requires VACUUM cleanup

#### SQLite

Uses file-level locking.

Lock states:

* Shared
* Reserved
* Pending
* Exclusive

Benefits:

* Simpler implementation

Cost:

* Write contention under heavy workloads

---

### Transaction Management

PostgreSQL:

* ACID compliant
* MVCC-based transactions
* WAL-based recovery

SQLite:

* ACID compliant
* Journal-based recovery
* Simpler transaction system

---

## 4. Design Trade-Offs

| Area               | PostgreSQL | SQLite      |
| ------------------ | ---------- | ----------- |
| Deployment         | Complex    | Very simple |
| Concurrency        | Excellent  | Limited     |
| Memory Usage       | Higher     | Very low    |
| Scalability        | High       | Moderate    |
| Administration     | Required   | Minimal     |
| Multi-user Support | Strong     | Limited     |
| Embedded Use       | Poor fit   | Excellent   |

### Why PostgreSQL Uses Client-Server Architecture

PostgreSQL targets environments where:

* Many users access data simultaneously
* Security controls are required
* Large datasets are common
* Advanced query optimization is important

The server architecture enables centralized management and high concurrency.

### Why SQLite Uses Embedded Architecture

SQLite targets environments where:

* Simplicity matters
* Resources are limited
* The database is used by a single application

Removing the server process reduces complexity and deployment overhead.

---

## 5. Practical Observations

### Observation 1: Mobile Applications

SQLite performs well because:

* No server installation required
* Low memory usage
* Database stored locally
* Minimal battery and resource consumption

Examples:

* Android apps
* iOS apps
* Embedded devices

### Observation 2: Enterprise Systems

PostgreSQL performs better because:

* Supports many concurrent users
* Handles large datasets efficiently
* Provides advanced indexing and optimization

Examples:

* E-commerce platforms
* Banking systems
* SaaS applications

---

## 6. Key Learnings

1. Architecture is driven by workload requirements.
2. SQLite prioritizes simplicity and portability.
3. PostgreSQL prioritizes concurrency and scalability.
4. MVCC enables PostgreSQL to support many simultaneous users.
5. SQLite's embedded design makes it ideal for mobile and desktop applications.
6. There is no universally better database; the correct choice depends on workload and operational requirements.

## References

* PostgreSQL Documentation
* SQLite Documentation
* PostgreSQL Source Code
* SQLite Architecture Documentation
