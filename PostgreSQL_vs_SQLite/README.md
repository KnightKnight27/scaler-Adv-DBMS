# PostgreSQL vs SQLite: A Study of Two Relational Database Architectures

> Advanced DBMS · System Design Discussion · Topic 1

Although PostgreSQL and SQLite are both relational databases that support SQL and provide ACID guarantees, they are designed for fundamentally different environments. PostgreSQL is a standalone database server intended to support many users and applications simultaneously, whereas SQLite is an embedded database library that runs directly inside an application process.

The easiest way to understand their architectural differences is to ask a simple question: **where does the database engine execute?** The answer influences nearly every aspect of the design, including process management, storage organization, concurrency control, and the workloads for which each system is optimized.

---

## 1. Motivation

The term *relational database* encompasses systems built for very different requirements. Consider two common scenarios:

### Large Multi-User Applications

Imagine a cloud-hosted application serving thousands of users through multiple application servers. Many clients may read and modify the same data concurrently. The database must support network access, strong isolation, security controls, replication, backup, and independent administration.

### Embedded and Local Applications

Now consider a mobile application, web browser, desktop utility, or IoT device. Here, the database exists solely to store application data locally. There is no dedicated server, administrator, or network communication. The priorities are minimal overhead, easy deployment, and a small footprint.

PostgreSQL was created to address the first scenario, while SQLite was engineered for the second. Neither design is inherently superior; each represents a different balance between scalability and simplicity.

This comparison explores how the initial architectural decision—server-based versus embedded—propagates through every layer of the system.

---

## 2. Architectural Overview

The key distinction between PostgreSQL and SQLite lies in where the database engine resides relative to the application.

### SQLite: Database as a Library

SQLite operates as a library linked directly into the application. When a program opens a database using SQLite, no external service is launched and no network communication occurs.

Components such as the SQL parser, query planner, virtual machine, B-tree manager, and pager all execute within the application's own process. Database operations ultimately translate into direct file reads and writes on the local machine.

Because of this design, SQLite is often viewed as an enhanced file format rather than a traditional database server.

### PostgreSQL: Database as a Service

PostgreSQL follows a client-server architecture. A dedicated server process manages the database independently of applications.

Clients communicate with the server through TCP or Unix-domain sockets using PostgreSQL's wire protocol. Applications may reside on different machines, use different programming languages, and connect concurrently to the same database instance.

The database remains active regardless of whether clients are currently connected.

### Comparing the Models

SQLite embeds the database inside the application process, whereas PostgreSQL separates the database into an independent service.

This single design choice affects deployment, concurrency, security, maintenance, and scalability throughout the system.

---

## 3. Internal Architecture

### Storage Organization

#### SQLite

SQLite stores the entire database in a single portable file. Tables and indexes coexist inside that file as B-tree structures distributed across fixed-size pages.

The schema itself is stored within the database file, making databases self-contained and easy to move between systems.

The default page size is typically 4 KB.

#### PostgreSQL

PostgreSQL stores data in a directory hierarchy known as a database cluster.

Each table and index is represented by separate files, and additional files maintain metadata such as free-space information and visibility tracking. Transaction logs are stored independently in the WAL directory.

Pages are typically 8 KB in size.

As a result, a single logical table may correspond to multiple physical files on disk.

---

### Table Storage

The systems differ significantly in how they organize row data.

#### PostgreSQL Heap Storage

PostgreSQL stores records in heap files. Rows are not physically ordered according to any index key. Instead, indexes maintain references to row locations using tuple identifiers (ctids).

Large attribute values are moved into auxiliary TOAST tables when necessary, keeping primary table pages compact.

#### SQLite B-Tree Storage

SQLite stores table rows directly inside B-tree structures. A conventional table is organized by an automatically generated rowid, while `WITHOUT ROWID` tables are clustered according to their primary key.

Large values that exceed page capacity spill into overflow pages linked to the original record.

---

### Index Structures

Both databases rely primarily on B-tree indexes for standard lookups.

SQLite focuses on simplicity and mainly supports B-tree indexes, with specialized extensions available for full-text search and spatial data.

PostgreSQL provides a broader ecosystem of index types, including:

* B-tree
* Hash
* GIN
* GiST
* BRIN

These access methods allow PostgreSQL to efficiently support full-text search, JSON documents, geometric data, and analytical workloads.

---

### Concurrency Control

This is arguably the most significant architectural difference.

#### PostgreSQL and MVCC

PostgreSQL implements Multi-Version Concurrency Control (MVCC).

Instead of modifying rows in place, updates create new row versions. Transactions determine visibility using metadata stored alongside each tuple.

The result is that readers and writers rarely block each other, enabling high levels of concurrent access.

The downside is the accumulation of obsolete row versions, which must eventually be removed through VACUUM.

#### SQLite Locking

SQLite uses file-based locking rather than row-versioning.

Only one writer may modify the database at a time, although many readers can access it simultaneously.

In rollback-journal mode, modified pages are copied into a journal before updates occur. In WAL mode, changes are appended to a separate log file and later merged back into the database through checkpointing.

While WAL mode improves reader-writer concurrency, SQLite still maintains a single-writer limitation.

---

### Durability

Both systems achieve ACID durability through logging mechanisms.

PostgreSQL records changes in a centralized Write-Ahead Log before committing transactions. During recovery, WAL records are replayed to restore a consistent state.

SQLite offers two durability approaches:

* Rollback Journal (undo-based recovery)
* WAL Mode (redo-based recovery)

In both cases, transaction commits are acknowledged only after the necessary log data has been safely written to disk.

---

## 4. Major Trade-Offs

| Category       | PostgreSQL                     | SQLite                   |
| -------------- | ------------------------------ | ------------------------ |
| Deployment     | Dedicated server               | Embedded library         |
| Concurrency    | Many readers and writers       | Many readers, one writer |
| Networking     | Supported                      | Not supported            |
| Storage        | Multiple files                 | Single file              |
| Administration | Requires setup and maintenance | Zero configuration       |
| Extensibility  | High                           | Minimal                  |
| Resource Usage | Higher                         | Very small               |
| Best Use Case  | Multi-user systems             | Embedded/local storage   |

### Why SQLite Excels in Embedded Systems

SQLite minimizes operational complexity. Applications can bundle a database without requiring installation, configuration, or maintenance.

Its single-file architecture simplifies deployment and backup, while in-process execution avoids network overhead.

These characteristics make SQLite ideal for mobile applications, browsers, desktop software, and edge devices.

### Why PostgreSQL Excels in Server Environments

PostgreSQL's architecture is optimized for concurrent, networked workloads.

MVCC enables large numbers of simultaneous users, while sophisticated indexing, extensibility, replication, and security mechanisms support demanding enterprise applications.

Although this introduces administrative overhead, it provides capabilities that embedded databases cannot realistically offer.

---

## Key Takeaways

* The most important architectural distinction is where the database engine runs: inside the application or as an independent service.
* PostgreSQL prioritizes scalability, concurrency, and operational flexibility.
* SQLite prioritizes simplicity, portability, and minimal resource consumption.
* PostgreSQL uses MVCC to support many concurrent writers, while SQLite relies on file-level locking and permits only one writer at a time.
* Storage organization reflects each system's goals: PostgreSQL uses a collection of specialized files, whereas SQLite stores everything in a single database file.
* Neither database is universally superior; the appropriate choice depends entirely on workload requirements and deployment constraints.

