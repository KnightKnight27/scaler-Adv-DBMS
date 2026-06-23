# PostgreSQL vs SQLite: An Architectural Comparison

## 1. Problem Background

### Why These Databases Were Created

Although PostgreSQL and SQLite are both relational database systems, they were built for very different environments and usage patterns.

PostgreSQL was developed to support applications that require centralized data management, multiple simultaneous users, strong consistency guarantees, and the ability to process large volumes of data. Its architecture focuses on scalability, reliability, and advanced database functionality.

SQLite, in contrast, was designed with simplicity as the primary goal. Instead of requiring a separate database server, SQLite operates as a lightweight library that can be embedded directly into an application. This makes it particularly useful for devices and applications that need local data storage without additional infrastructure.

### Problems Addressed by Each System

PostgreSQL addresses the challenge of managing shared data across many users and applications at the same time. It provides mechanisms for concurrency control, transaction management, security, and recovery, making it suitable for enterprise systems and cloud-based services.

SQLite solves a different problem. Many applications need a reliable way to store structured information locally but do not require a dedicated database server. SQLite allows developers to store data using SQL while avoiding installation, configuration, and maintenance overhead.

As a result, both systems target different use cases despite supporting similar SQL operations.

---

## 2. Architecture Overview

### PostgreSQL Architecture

PostgreSQL follows a traditional database server model. A central server process manages database resources and communicates with multiple clients over a network connection.

When a client establishes a connection, PostgreSQL creates a dedicated backend process responsible for handling requests from that client.

```text id="8n5lvb"
Client
   |
Network
   |
PostgreSQL Server
   |
Backend Process
   |
Shared Memory
   |
Storage Files
```

This architecture enables many users to access the same database simultaneously while maintaining consistency and isolation.

### SQLite Architecture

SQLite takes a fundamentally different approach.

Instead of running as a separate server, SQLite is compiled directly into the application itself. Database operations become function calls inside the application process.

```text id="vd4iyv"
Application
      |
SQLite Library
      |
Operating System
      |
Database File
```

There is no server process, network communication, or connection management layer.

This design significantly reduces complexity and deployment effort.

---

## 3. Internal Design

### Process Model

One of the largest architectural differences between PostgreSQL and SQLite is how they handle execution.

#### PostgreSQL

Each client connection is managed by an independent backend process.

Advantages include:

* Strong isolation between connections
* Better fault containment
* Efficient support for many concurrent users

The trade-off is higher memory and CPU overhead because each process consumes system resources.

#### SQLite

SQLite runs entirely inside the host application.

Advantages include:

* Minimal resource consumption
* No server startup time
* Simpler deployment

However, concurrency capabilities are significantly more limited.

---

### Storage Engine Design

#### PostgreSQL

PostgreSQL manages its own memory through a Shared Buffer Cache and uses background processes to coordinate writes and recovery.

Key features include:

* Shared Buffers
* Write-Ahead Logging (WAL)
* Background Writer
* Checkpointer

This architecture is optimized for large, multi-user workloads.

#### SQLite

SQLite relies heavily on the operating system for caching and file management.

The database is typically represented by a single file on disk, making storage highly portable.

Because SQLite delegates much of the caching responsibility to the operating system, its internal design remains relatively simple.

---

### File Organization

#### PostgreSQL

Database objects are stored across multiple files and directories.

Characteristics include:

* Separate files for tables and indexes
* Internal metadata storage
* Segmentation of large files

This approach supports extremely large databases and complex storage management.

#### SQLite

A SQLite database generally exists as a single file.

All tables, indexes, and metadata are stored within that file using a page-oriented structure.

Advantages include:

* Easy backups
* Simple file transfers
* Straightforward deployment

The downside is reduced flexibility for very large deployments.

---

### Transaction Management and Concurrency

#### PostgreSQL: MVCC

PostgreSQL uses Multi-Version Concurrency Control.

Instead of overwriting existing rows, updates create new row versions. Transactions see a consistent snapshot of the database while changes occur in parallel.

Benefits include:

* Readers do not block writers
* Writers do not block readers
* Excellent scalability under mixed workloads

This design is one of the primary reasons PostgreSQL performs well in environments with thousands of concurrent users.

#### SQLite: Lock-Based Coordination

Historically, SQLite relied on file-level locking mechanisms provided by the operating system.

In its traditional mode:

* Multiple readers can access the database simultaneously.
* Only one writer can modify the database at a time.

SQLite's WAL mode improves concurrency by allowing reads and writes to occur simultaneously in many situations, but write operations are still fundamentally serialized.

As concurrency increases, this limitation becomes more noticeable.

---

## 4. Design Trade-Offs

### Strengths of PostgreSQL

* Handles large numbers of concurrent users effectively.
* Provides advanced indexing and optimization features.
* Supports complex analytical queries.
* Offers extensive security and access-control mechanisms.
* Highly extensible through custom functions and data types.

### Strengths of SQLite

* No installation or server administration required.
* Small memory footprint.
* Easy portability through a single database file.
* Excellent performance for local application storage.
* Minimal operational complexity.

### Limitations of PostgreSQL

* Requires dedicated system resources.
* More complex configuration and maintenance.
* Connection management becomes important at scale.

### Limitations of SQLite

* Limited write concurrency.
* Not designed for distributed access across many machines.
* Fewer advanced database administration features.
* Less suitable for large enterprise workloads.

### Why SQLite Is Common in Mobile Applications

Mobile applications typically serve a single user and operate within resource-constrained environments.

SQLite fits these requirements because:

* It runs inside the application process.
* No separate server must be maintained.
* Data remains available even without network connectivity.
* Battery and memory consumption remain low.

These characteristics explain its widespread adoption in smartphones, browsers, and embedded systems.

### Why PostgreSQL Is Popular in Enterprise Systems

Large applications must support many users interacting with the same dataset simultaneously.

Examples include:

* Banking systems
* E-commerce platforms
* SaaS applications
* Analytics platforms

PostgreSQL's MVCC architecture, transaction guarantees, and sophisticated query engine allow it to maintain performance and consistency even under heavy workloads.

---

## 5. Experiments and Observations

### Concurrent Write Workload

A useful comparison involves multiple threads attempting to insert data simultaneously.

#### PostgreSQL

With many concurrent clients, PostgreSQL distributes work across separate backend processes and coordinates access using MVCC and locking mechanisms.

The result is smooth transaction processing even when write activity is high.

#### SQLite

Under the same workload, write requests are effectively serialized.

When many threads attempt to write at once:

* Some requests wait.
* Contention increases.
* Busy timeout errors may occur.

This illustrates the practical impact of SQLite's single-writer design.

---

### Query Latency Comparison

For very small local queries such as:

```sql id="3x9hio"
SELECT 1;
```

SQLite often performs exceptionally well because execution occurs within the same process.

PostgreSQL introduces additional overhead from:

* Client-server communication
* Context switching
* Connection handling

Although the difference is small for individual queries, it demonstrates how architectural decisions influence performance characteristics.

---

## 6. Key Learnings

### 1. Architecture Follows Intended Usage

PostgreSQL and SQLite were designed for different environments. Their architectures reflect the problems they were built to solve rather than competing directly with each other.

### 2. Concurrency Comes With Complexity

PostgreSQL's ability to support thousands of concurrent operations requires sophisticated mechanisms such as MVCC, shared memory management, and background processes.

SQLite avoids much of this complexity by restricting write concurrency.

### 3. Simplicity Can Be a Major Advantage

SQLite demonstrates that a simpler architecture can outperform more sophisticated systems in the right context. For local storage and embedded applications, eliminating network communication and server management provides substantial benefits.

### 4. There Is No Universal Best Database

Choosing between PostgreSQL and SQLite depends entirely on workload requirements. PostgreSQL excels when scalability and concurrency are critical, while SQLite shines when simplicity, portability, and minimal overhead are the primary concerns.

The comparison highlights a broader lesson in system design: engineering decisions are always driven by the problems a system is intended to solve.
