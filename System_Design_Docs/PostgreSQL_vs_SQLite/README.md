# PostgreSQL vs SQLite: An Architecture Comparison

**Advanced Database Management Systems (ADBMS)**
**System Design Discussion – Topic 1**

**Author:** Praneeth Budati
**Roll No:** 24BCS10081

---

## Introduction

PostgreSQL and SQLite are both relational database systems, but they are designed for completely different use cases.

SQLite is an embedded database library that runs inside an application process, while PostgreSQL is a client-server database system designed to support many concurrent users. Most architectural differences between the two systems originate from this fundamental design decision.

This report compares their architecture, storage model, concurrency mechanisms, transaction processing, and performance characteristics.

---

## Background

### SQLite

SQLite was created to provide reliable local data storage without requiring a separate database server.

Key characteristics:

* Embedded directly into applications
* Zero configuration
* Single database file
* Lightweight and portable
* Commonly used in mobile and desktop applications

### PostgreSQL

PostgreSQL originated from the POSTGRES research project at Berkeley.

Key characteristics:

* Client-server architecture
* Multi-user support
* Advanced query optimization
* Extensible database features
* Enterprise-grade reliability

---

## High-Level Architecture

### SQLite Architecture

Application
↓
SQLite Library
↓
Database File

Features:

* No separate server process
* Direct file access
* Minimal resource consumption

### PostgreSQL Architecture

Client Applications
↓
PostgreSQL Server
↓
Shared Memory + Background Processes
↓
Database Storage

Features:

* Dedicated database server
* Multiple concurrent clients
* Shared buffer management
* Background maintenance processes

---

## Storage Design

### SQLite

SQLite stores the entire database inside a single file.

Advantages:

* Easy backup and transfer
* Simple deployment
* Minimal administration

Limitations:

* Limited concurrency
* Entire database managed through one file

### PostgreSQL

PostgreSQL stores tables, indexes, and metadata in separate files.

Advantages:

* Better scalability
* Efficient storage management
* Supports large datasets

---

## Memory Management

| Feature     | SQLite           | PostgreSQL                |
| ----------- | ---------------- | ------------------------- |
| Cache Type  | Local Page Cache | Shared Buffer Pool        |
| Sharing     | Per Connection   | Shared Across Connections |
| Scalability | Limited          | High                      |

PostgreSQL's shared buffer architecture enables multiple users to access the same cached pages efficiently.

---

## Indexing

### SQLite

* Uses B-Tree indexes
* Tables are organized around rowids
* Simple and lightweight design

### PostgreSQL

Supports multiple index types:

* B-Tree
* Hash
* GIN
* GiST
* BRIN
* SP-GiST

This flexibility allows PostgreSQL to optimize different workloads.

---

## Concurrency Control

### SQLite

SQLite allows:

* Multiple readers
* Only one writer at a time

This approach keeps implementation simple but limits write scalability.

### PostgreSQL

PostgreSQL uses Multi-Version Concurrency Control (MVCC).

Benefits:

* Readers do not block writers
* Writers do not block readers
* Better performance under heavy concurrent workloads

---

## MVCC in PostgreSQL

Every row stores transaction metadata:

* xmin → creating transaction
* xmax → deleting transaction

Instead of modifying rows directly, PostgreSQL creates new row versions during updates.

Advantages:

* Consistent snapshots
* High concurrency

Disadvantage:

* Dead tuples accumulate over time

This is why PostgreSQL requires VACUUM operations.

---

## Durability and Recovery

Both databases use Write-Ahead Logging (WAL).

### SQLite

1. Changes are written to the WAL file.
2. Data is later checkpointed into the main database file.

### PostgreSQL

1. WAL records are generated.
2. WAL is flushed during commit.
3. Data pages are written later by background processes.

This approach ensures crash recovery and transaction durability.

---

## Major Differences

| Aspect           | SQLite              | PostgreSQL                  |
| ---------------- | ------------------- | --------------------------- |
| Architecture     | Embedded Library    | Client-Server               |
| Storage          | Single File         | Multiple Files              |
| Concurrency      | One Writer          | Many Concurrent Writers     |
| MVCC             | No                  | Yes                         |
| Query Planner    | Basic               | Advanced Cost-Based Planner |
| Parallel Queries | No                  | Yes                         |
| Replication      | Not Built-In        | Supported                   |
| Best Use Case    | Mobile/Desktop Apps | Multi-User Systems          |

---

## Advantages and Limitations

### SQLite Advantages

* Lightweight
* Easy deployment
* No server administration
* Portable

### SQLite Limitations

* Single-writer restriction
* Limited scalability
* Fewer enterprise features

### PostgreSQL Advantages

* High concurrency
* Rich indexing support
* Advanced query optimization
* Replication and high availability

### PostgreSQL Limitations

* More complex setup
* Requires administration
* Higher resource consumption

---

## Conclusion

SQLite and PostgreSQL are designed for different environments.

SQLite excels when an application requires a lightweight embedded database with minimal setup. PostgreSQL is better suited for applications requiring high concurrency, advanced querying, scalability, and enterprise-level reliability.

The most important takeaway is that architectural choices drive all other design decisions. SQLite prioritizes simplicity and portability, whereas PostgreSQL prioritizes concurrency, extensibility, and performance for shared workloads.

---

## References

1. SQLite Official Documentation
2. PostgreSQL 16 Documentation
3. The Design of POSTGRES (Stonebraker)
4. The Internals of PostgreSQL
5. SQLite WAL Documentation
