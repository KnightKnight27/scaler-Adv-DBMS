# Topic 1: PostgreSQL vs SQLite Architecture Comparison

## 1. Problem Background
PostgreSQL and SQLite represent two fundamentally different approaches to database system design. 
- **PostgreSQL** was designed as an enterprise-grade, highly extensible, client-server relational database capable of handling massive concurrency and large volumes of data.
- **SQLite**, in contrast, was built to provide a lightweight, embedded relational database without the need for a separate server process or complex configuration.

## 2. Architecture Overview
### PostgreSQL (Client-Server Model)
- **Process Model**: Uses a multi-process architecture. A central `postmaster` process listens for connections and forks a new backend process for each client.
- **Components**: Shared memory buffers, background writer, WAL writer, autovacuum daemon.

### SQLite (Embedded Model)
- **Process Model**: Runs as a library linked directly into the host application. It shares the application's process space.
- **Components**: OS Interface (VFS), B-Tree, Pager, SQL Compiler, Core API.

## 3. Internal Design
### Database File Organization
- **PostgreSQL**: Uses multiple files. Each database is a directory, and each table/index is stored as a separate file (or multiple 1GB segment files).
- **SQLite**: Stores the entire database (tables, indexes, triggers) in a single cross-platform disk file.

### Concurrency Control & Transaction Management
- **PostgreSQL**: Implements Multi-Version Concurrency Control (MVCC) where readers don't block writers and vice versa. It manages concurrent access robustly using row-level locks and transaction ID snapshots.
- **SQLite**: Historically used database-level locking, meaning only one writer at a time. It later introduced Write-Ahead Logging (WAL) mode to allow concurrent readers and a single writer.

## 4. Design Trade-Offs
- **PostgreSQL Advantages**: Excellent concurrency, robust security/role management, scalability for large datasets, network-accessible.
- **PostgreSQL Limitations**: High memory/CPU overhead, requires DBA setup and maintenance.
- **SQLite Advantages**: Zero-configuration, very fast for read-heavy local workloads, highly portable (single file).
- **SQLite Limitations**: Poor concurrency for write-heavy workloads, lacks user management, size limits tied to the host disk/memory.

## 5. Experiments / Observations
- **Use Cases**: 
  - *SQLite* is the standard for mobile apps (iOS/Android), local caching, and IoT devices.
  - *PostgreSQL* is the standard for web backends, data warehousing, and enterprise software requiring ACID compliance across many concurrent users.

## 6. Key Learnings
The architectural choice between client-server and embedded dictates almost every downstream internal design decision. SQLite sacrifices fine-grained concurrency and network access to achieve unparalleled simplicity and portability, while PostgreSQL sacrifices lightweight deployment to achieve massive scalability and multi-user performance.
