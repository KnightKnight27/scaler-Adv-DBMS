# PostgreSQL vs SQLite Architecture Comparison

# PostgreSQL vs SQLite: An Architectural Comparison

## 1. Problem Background

Database systems have evolved to solve different classes of problems. While all relational databases provide mechanisms for storing and querying structured data, they are designed with different priorities depending on their intended use cases.

PostgreSQL is an enterprise-grade relational database management system designed for highly concurrent, multi-user environments. It provides advanced features such as Multi-Version Concurrency Control (MVCC), extensibility, sophisticated query optimization, and crash recovery mechanisms, making it suitable for large-scale applications.

SQLite, in contrast, is an embedded relational database engine designed to be lightweight, portable, and serverless. Instead of running as a separate process, SQLite becomes part of the application itself and directly accesses the database file. Its architecture minimizes deployment complexity and resource consumption, making it ideal for mobile devices, desktop applications, browsers, and embedded systems.

Although both databases implement the SQL standard, their internal architectures reflect fundamentally different engineering goals. PostgreSQL prioritizes scalability and concurrency, whereas SQLite prioritizes simplicity and portability.

---

# 2. Historical Context

SQLite was created in 2000 by D. Richard Hipp to provide a zero-configuration database engine that could be embedded directly into applications. The primary objective was to eliminate the operational complexity associated with database servers while maintaining ACID compliance.

PostgreSQL traces its origins to the POSTGRES research project at the University of California, Berkeley. Over several decades, it evolved into one of the world's most feature-rich open-source database systems, supporting complex workloads, large datasets, and thousands of concurrent users.

The historical evolution of both systems explains many of their architectural decisions. PostgreSQL was designed for enterprise servers, whereas SQLite was designed for applications that require local data storage without administration.

---

# 3. Architecture Overview

## PostgreSQL Architecture

PostgreSQL follows a client-server architecture.

```
                Client Applications

          psql     API     Web Server

                     │

              TCP/IP Connection

                     │

              PostgreSQL Server

         ┌──────────────────────────┐

         │      Query Planner       │

         │      Query Executor      │

         │      Buffer Manager      │

         │      Lock Manager        │

         │      WAL Manager         │

         └──────────────────────────┘

                     │

             Storage Manager

                     │

        Heap Files      Index Files

                     │

                  Physical Disk
```

A dedicated PostgreSQL server process accepts client connections, executes queries, manages transactions, coordinates concurrent users, and maintains durability through Write Ahead Logging (WAL).

---

## SQLite Architecture

SQLite follows an embedded architecture.

```
            Application

       Browser / Mobile App

               │

        SQLite Library

               │

      SQL Parser & Executor

               │

        Pager Module

               │

      B-Tree Storage Engine

               │

         Single Database File
```

No server process exists.

The application links directly against the SQLite library and accesses the database file without network communication.

This dramatically reduces deployment complexity.

---

# 4. Client-Server vs Embedded Design

The most significant architectural difference lies in execution model.

## PostgreSQL

* Separate server process
* Multiple client connections
* Shared memory architecture
* Process management
* Background workers
* Network communication

Advantages:

* Excellent concurrency
* High scalability
* Centralized security
* Shared caching
* Efficient resource management

Disadvantages:

* Requires installation
* Higher memory consumption
* Administrative overhead
* More complex deployment

---

## SQLite

* Embedded library
* Runs inside application process
* No server installation
* No network protocol
* Direct file access

Advantages:

* Zero configuration
* Extremely lightweight
* Simple deployment
* Small binary size
* Low memory usage

Disadvantages:

* Limited write concurrency
* Single database file
* Less suitable for many simultaneous writers

---

# 5. Storage Engine Architecture

## PostgreSQL Storage

PostgreSQL stores tables as heap files composed of fixed-size pages.

```
Heap File

+--------+
|Page 1  |
+--------+

|Page 2  |

+--------+

|Page 3  |

+--------+
```

Each page contains multiple tuples (rows).

Indexes are stored separately and point to tuple locations inside heap pages.

This design allows rows to move independently from indexes during updates.

---

## SQLite Storage

SQLite stores the entire database inside a single file.

```
Database File

+-----------+

Header

+-----------+

B-Tree Page

+-----------+

B-Tree Page

+-----------+

B-Tree Page

+-----------+
```

Tables and indexes are both represented internally as B-Tree structures.

This unified storage model simplifies implementation and improves portability.

---

# 6. Database File Organization

## PostgreSQL

A database consists of multiple physical files.

* Heap files
* WAL files
* Index files
* System catalogs
* Visibility maps
* Free space maps

Separating components allows PostgreSQL to optimize each storage structure independently.

---

## SQLite

Everything resides inside one portable database file.

Advantages include:

* Easy backup
* Easy copying
* Easy synchronization
* Simple deployment

However, corruption of the single file may affect the entire database.

---

# 7. Table Storage

## PostgreSQL Heap Storage

Rows are appended to heap pages.

When a row is updated, PostgreSQL creates a new tuple version rather than overwriting the old one.

This enables MVCC.

```
Update

Old Tuple

salary = 100

↓

New Tuple

salary = 150

↓

Old tuple marked obsolete
```

Dead tuples are later reclaimed by VACUUM.

---

## SQLite Table Storage

SQLite organizes tables using B-Tree pages.

Rows remain inside the tree structure itself.

Updates generally modify the stored record directly while journaling mechanisms ensure atomicity.

This approach reduces storage overhead but relies on locking rather than tuple versioning for concurrency.

---

# 8. Index Implementation

## PostgreSQL

Indexes are separate structures.

```
Index

50

↓

Heap Pointer

↓

Heap Page

↓

Actual Row
```

Advantages:

* Flexible storage
* Multiple index types
* Independent optimization

Trade-off:

Requires an additional heap lookup after index traversal.

---

## SQLite

Tables themselves are B-Trees.

The primary storage structure directly contains records.

Secondary indexes reference row identifiers.

This design minimizes indirection and keeps implementation compact.

---

# 9. Transaction Management

Both PostgreSQL and SQLite provide ACID transactions.

PostgreSQL supports thousands of concurrent transactions using MVCC.

SQLite supports transactions through journaling and file locking.

Although both guarantee consistency, PostgreSQL emphasizes concurrency while SQLite emphasizes simplicity.

---

# 10. Concurrency Control

## PostgreSQL

PostgreSQL implements Multi-Version Concurrency Control (MVCC).

Readers never block writers.

Writers rarely block readers.

Each tuple stores metadata identifying transaction visibility.

Advantages:

* High concurrency
* Reduced blocking
* Excellent performance under mixed workloads

Disadvantages:

* Dead tuples accumulate
* VACUUM becomes necessary

---

## SQLite

SQLite primarily relies on file locking.

Only one writer can modify the database at a time, although many readers can access it concurrently.

Advantages:

* Simple implementation
* Minimal metadata
* Small codebase

Disadvantages:

* Write contention
* Reduced scalability under write-heavy workloads

---

# 11. Durability Mechanisms

## PostgreSQL

PostgreSQL uses Write Ahead Logging (WAL).

```
Transaction

↓

Write WAL

↓

Flush WAL

↓

Modify Data Page

↓

Commit
```

If a crash occurs, WAL records replay changes during recovery.

This guarantees durability.

---

## SQLite

SQLite uses rollback journals or Write-Ahead Logging mode.

The rollback journal stores previous page contents before modification.

If the application crashes, the journal restores consistency.

Newer WAL mode improves concurrency by separating readers from writers.

---

# 12. Scalability Implications

| Feature            | PostgreSQL | SQLite    |
| ------------------ | ---------- | --------- |
| Concurrent Users   | Excellent  | Limited   |
| Write Throughput   | High       | Moderate  |
| Horizontal Scaling | Possible   | Difficult |
| Replication        | Supported  | External  |
| Extensions         | Extensive  | Limited   |
| Administration     | Required   | Minimal   |

PostgreSQL scales to enterprise workloads involving hundreds or thousands of simultaneous clients.

SQLite performs exceptionally well for single-user or lightly concurrent applications but becomes a bottleneck under heavy write contention.

---

# 13. Real-World Use Cases

## PostgreSQL

* Banking systems
* Enterprise applications
* E-commerce platforms
* SaaS products
* Analytics platforms
* Large web applications

These environments require high concurrency, complex queries, and advanced transactional guarantees.

---

## SQLite

* Android applications
* iOS applications
* Desktop software
* Embedded devices
* Browser storage
* IoT systems

Its lightweight architecture and zero-configuration deployment make it ideal where simplicity and portability are more important than scalability.

---

# 14. Design Trade-offs

| Design Decision            | Advantage                   | Trade-off               |
| -------------------------- | --------------------------- | ----------------------- |
| Client-server architecture | High concurrency            | Operational complexity  |
| Embedded architecture      | Easy deployment             | Limited scalability     |
| MVCC                       | Readers don't block writers | Requires VACUUM         |
| Heap storage               | Flexible updates            | Additional heap lookup  |
| Single database file       | Easy portability            | Single point of storage |
| WAL                        | Strong durability           | Extra write overhead    |

These choices illustrate that database systems are fundamentally collections of engineering trade-offs rather than universally superior designs.

---

# 15. Suggested Experiments and Observations

1. Create identical schemas in PostgreSQL and SQLite.
2. Execute concurrent read and write workloads.
3. Measure latency under increasing numbers of writers.
4. Observe locking behavior.
5. Compare execution times for indexed and non-indexed queries.
6. Measure database file growth after repeated updates.

Expected observations:

* PostgreSQL maintains stable performance under concurrent workloads due to MVCC.
* SQLite exhibits excellent single-user performance but experiences write serialization under heavy concurrent updates.
* PostgreSQL incurs additional storage overhead because obsolete tuple versions remain until cleanup.
* SQLite's embedded architecture minimizes communication overhead and startup latency.

---

# 16. Key Learnings

* Architectural goals determine database design.
* PostgreSQL prioritizes scalability and concurrent access through a client-server architecture.
* SQLite prioritizes portability and simplicity through an embedded architecture.
* MVCC improves concurrency but introduces storage overhead and maintenance requirements.
* Heap storage and separate indexes provide flexibility at the cost of additional lookups.
* Embedded databases eliminate deployment complexity but sacrifice write scalability.
* There is no universally superior database system; the optimal choice depends on workload characteristics and engineering requirements.

---

# Conclusion

PostgreSQL and SQLite represent two distinct philosophies in database system design. PostgreSQL embraces complexity to maximize scalability, concurrency, and extensibility, making it suitable for enterprise applications. SQLite embraces simplicity to provide a lightweight, serverless, and highly portable database engine for embedded environments.

Studying their architectures demonstrates that database systems are shaped by deliberate engineering trade-offs. Understanding these trade-offs enables engineers to choose the right database for a given workload rather than assuming one system is universally better than another.

---

# References

* PostgreSQL Documentation
* SQLite Documentation
* PostgreSQL Source Code (`src/backend`)
* SQLite Architecture Documentation
* "Database System Concepts" by Silberschatz, Korth, and Sudarshan
* "Readings in Database Systems"
* PostgreSQL Wiki and Developer Guides
