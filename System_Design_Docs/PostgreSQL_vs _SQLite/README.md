# **Topic 1: PostgreSQL vs SQLite Architecture Comparison**

## 1. Problem Background

### Why PostgreSQL and SQLite Were Built

Although PostgreSQL and SQLite are both relational database systems, they were created to solve very different problems.

PostgreSQL evolved from academic research at UC Berkeley and was designed for environments where many users access the same database simultaneously. The primary goals were reliability, extensibility, strong transaction guarantees, and support for complex workloads. As organizations began building larger applications, there was a need for a database server that could safely handle concurrent users, large datasets, and advanced SQL functionality.

SQLite was created with almost the opposite philosophy. Instead of running as a separate server process, it was designed to be embedded directly into an application. The objective was to provide database functionality without requiring installation, configuration, administration, or background services. For many desktop, mobile, and embedded applications, managing a dedicated database server would introduce unnecessary complexity.

These different objectives led to fundamentally different architectural decisions. PostgreSQL prioritizes scalability, concurrency, and isolation, while SQLite prioritizes simplicity, portability, and minimal operational overhead.

---

## 2. Architecture Overview

The most significant distinction between PostgreSQL and SQLite is the location where query processing occurs.

In PostgreSQL, clients communicate with a dedicated database server responsible for query execution, storage management, transaction handling, and recovery. SQLite follows a different approach where the database engine is linked directly into the application and interacts with database files without an intermediate server process.

The following diagrams illustrate how requests flow through each system.

```
+-------------------------------------------------------------------------+
|                               SQLITE MODEL                              |
|                                                                         |
|  [ Application Process Boundary ]                                       |
|  +-------------------------------------------------------------------+  |
|  |  Application Code  ==[Function Calls]==>  SQLite Library          |  |
|  |                                             |                     |  |
|  |                                    +--------v-------+             |  |
|  |                                    | Parser & Opt   |             |  |
|  |                                    +--------+-------+             |  |
|  |                                    | VDBE Engine    |             |  |
|  |                                    +--------+-------+             |  |
|  |                                    | Pager / Cache  |             |  |
|  +---------------------------------------------+----------------------+  |
|                                                |                         |
|                                      Direct OS File Locks                |
|                                                |                         |
|                                        +-------v-------+                 |
|                                        | database.db   | (Single File)   |
|                                        +---------------+                 |
+-------------------------------------------------------------------------+

+-------------------------------------------------------------------------+
|                            POSTGRESQL MODEL                             |
|                                                                         |
|  [ Client App A ]      [ Client App B ]      [ Client App C ]           |
|         |                      |                      |                  |
|         +===[ TCP/IP Socket (Port 5432) ]===========+                    |
|                                |                                         |
|  [ PostgreSQL Server Process Boundary ]                                 |
|  +-------------------------------------------------------------------+  |
|  |  Postmaster Daemon (Listener & Connection Router)                 |  |
|  |         |                                                         |  |
|  |         +--- fork() on connection ---> [ Dedicated Backend ]      |  |
|  |                                        | - SQL Compiler           |  |
|  |                                        | - Plan Executor          |  |
|  |                                        +--------+-----------------+  |
|  |                                                 |                     |
|  |   +---------------------------------------------+                     |
|  |   |                                                                   |
|  |   |   +----------------------------------------------------------+   |
|  |   |   | Shared Memory (shared_buffers, Lock Tables, WAL Buffers) |   |
|  |   |   +----------------------------+-----------------------------+   |
|  +---|--------------------------------|------------------------------+  |
|      |                                |                                 |
|      | Disk Writes                    | Shared I/O Coordination          |
|  +---v--------------------------------v-------------------------------+  |
|  | /PGDATA Directory (Multi-file layout: base/, pg_wal/, etc.)        |  |
|  +--------------------------------------------------------------------+  |
+-------------------------------------------------------------------------+

```

### Key Differences at a Glance

| Architectural Dimension | SQLite | PostgreSQL |
| --- | --- | --- |
| **Execution Model** | In-process Library | Server Daemon (Client-Server) |
| **Connection Handling** | Native thread calls, shared memory context | Multi-process (`fork()` model) |
| **Database Storage** | Single file containing all tables/indexes | Multi-file directory tree structure |
| **Isolation Level Support** | Serializable (via database-wide locks) | Read Committed, Repeatable Read, Serializable |
| **Concurrency Control** | File-level coarse locking (WAL mode improves this) | Multi-Version Concurrency Control (MVCC) |

---

## 3. Internal Design

### 3.1. Process & Execution Model

#### PostgreSQL: Multi-Process Isolation

PostgreSQL adopts a process-per-connection architecture. Whenever a new client establishes a connection, PostgreSQL creates a dedicated backend process responsible for handling that client's queries.

Although this approach consumes more memory than a thread-based design, it provides strong isolation between connections and simplifies fault containment.

* **Fault Isolation:** If a query triggers a segmentation fault or memory leak in one backend process, only that specific connection terminates. The rest of the database cluster remains operational.
* **Shared Memory Segment:** All spawned backend processes communicate and synchronize state via a shared memory block. This segment houses:
* **Shared Buffers:** Pointers and blocks caching data pages.
* **Lock Table:** Global locking structures coordinating reader/writer conflicts.
* **WAL Buffers:** Pre-allocation block for transaction logs before disk commit.


* **IPC Overhead:** Spawning processes via `fork()` consumes significant kernel memory and overhead. To manage high connection spikes without degradation, PostgreSQL deployments frequently rely on external connection poolers (e.g., PgBouncer).

From an engineering perspective, PostgreSQL intentionally accepts higher memory consumption in exchange for improved stability. A malfunctioning backend process typically affects only a single connection rather than the entire database service.

#### SQLite: In-Process Virtual Database Engine (VDBE)

SQLite does not run background processes or daemons. All operations occur directly within the address space of the caller application.

* **VDBE Assembly:** Instead of executing SQL directly, SQLite converts SQL statements into a sequence of low-level instructions that run on its Virtual Database Engine (VDBE). This internal virtual machine acts similarly to a lightweight CPU optimized specifically for database operations. Because the engine executes inside the application process, query execution avoids network communication and context-switching overhead.

* **Zero Inter-Process Communication (IPC):** Retrieving data is as fast as a programming language function call. No networking stacks, serialization/deserialization overhead, or process contexts are involved.

* **Resource Sharing:** If multiple threads within the same application process query the database, they share cache memory, avoiding redundant file page loads.

---

### 3.2. Storage Engine & Database Layout

#### PostgreSQL Disk Storage Structure

Unlike SQLite's single-file design, PostgreSQL distributes information across multiple files and directories. This separation allows individual subsystems such as transaction management, recovery, and table storage to operate independently.

While this organization increases administrative complexity, it improves scalability and recovery capabilities for large databases.

* **Segments:** The heap data for a table or index is split into 1 GB segment files. This design ensures compatibility with operating systems that restrict maximum file sizes.
* **Directory Trees:** Files are structured hierarchically:
* `base/`: Subdirectories for individual databases, named by OID.
* `pg_wal/`: Write-Ahead Log segments recording transitions.
* `pg_xact/`: Transaction commit status bit arrays.


* **Page Internals:** Inside a standard 8 KB page, PostgreSQL positions item identifiers (pointers to rows) growing downward from the page header, while the actual tuple structures grow upward from the bottom of the page. This prevents page layout fragmentation.

#### SQLite Disk Storage Structure

SQLite packs the entire database schema, tables, indices, and system configurations into **a single file**.

The single-file approach is one of SQLite's most attractive features. Deployment becomes extremely simple because moving, copying, or backing up the database is equivalent to handling a normal file within the operating system.

* **Global Headers:** The first 100 bytes of the file contain essential metadata, such as page size, write/read versioning tags, and schema cookie numbers.
* **B-Trees vs. B+ Trees:** SQLite implements two B-tree variants:
* **Table B-Trees:** Used to store actual row data. Keys are 64-bit integer row IDs, and leaves contain payloads (non-key columns).
* **Index B-Trees:** Used for indexes. Keys are index values appended with row IDs, and no payloads reside at leaf nodes.


* **Page Sizing:** Page size is configurable at database creation (typically 4096 bytes to match modern sector layouts). Pages are mapped dynamically as B-tree nodes.

---

### 3.3. Transaction Management & Concurrency Control

#### PostgreSQL: Fine-Grained MVCC

PostgreSQL uses Multi-Version Concurrency Control (MVCC) to reduce contention between readers and writers.

Instead of modifying records directly, updates create new tuple versions while preserving older versions for transactions that still require them. As a result, read operations can continue without waiting for write operations to complete.

* **No Blocking Readers/Writers:** A transaction modifying a row writes a new version of the row (tuple) to the heap. Readers continue reading the older committed version by referencing transaction snapshots.
* **Transaction Identifiers:** Rows store transaction metadata in header columns (`xmin` for insertion ID, `xmax` for deletion/update ID).
* **Locks:** PostgreSQL supports fine-grained row-level locking. Row locks are stored in the shared memory lock table, meaning locking millions of rows does not run out of memory or trigger lock escalation.

This design significantly improves concurrency but introduces additional storage overhead because obsolete tuple versions must eventually be cleaned by VACUUM.

#### SQLite: Database-Level File Locking

SQLite manages concurrency by locking the entire database file using the operating system's filesystem lock mechanism.

* **State Transition:** SQLite uses a lock state machine with five distinct states:
* `UNLOCKED`: No connections are interacting with the database.
* `SHARED`: Readers are active. Any number of connections can hold a shared lock, preventing write locks.
* `RESERVED`: A single connection plans to write. It reserves the right to modify pages in cache but has not written to disk yet.
* `PENDING`: A writer wants to commit. No new readers can acquire `SHARED` locks, waiting for existing readers to finish.
* `EXCLUSIVE`: The writer holds the lock, writes the changes to disk, and blocks all other readers and writers.

The locking model is considerably simpler than MVCC-based systems. However, simplicity comes at the cost of reduced write concurrency because the database eventually requires an exclusive lock before modifications can be persisted.

* **WAL Mode Concurrency:** In Write-Ahead Log (WAL) mode, SQLite decouples readers and writers. Writes append to a sidecar file (`.db-wal`) rather than directly modifying the main database file. This allows one writer and multiple concurrent readers to coexist.

---

## 4. Design Trade-Offs

### Server-Based Architecture vs Embedded Architecture

The most important difference between PostgreSQL and SQLite is where the database engine executes.

PostgreSQL operates as a standalone service that manages storage, memory, concurrency, and recovery on behalf of all connected clients. This approach increases complexity but enables better resource management and multi-user scalability.

SQLite executes inside the application's process space. Since queries are executed through direct function calls, there is almost no communication overhead. However, the application becomes responsible for resource management and coordination.

### Concurrency Trade-Offs

PostgreSQL uses MVCC to allow readers and writers to operate concurrently. Multiple users can update different rows of the same table without interfering with one another. This makes PostgreSQL suitable for web applications, financial systems, and enterprise workloads.

SQLite uses file-level coordination mechanisms. While WAL mode improves concurrency significantly, write operations are still serialized. This limitation is acceptable for mobile applications and local desktop software where concurrent write activity is usually low.

### Storage Management Trade-Offs

PostgreSQL stores information across multiple files and directories. This organization supports large databases, background maintenance processes, replication, and recovery mechanisms.

SQLite stores everything inside a single database file. This greatly simplifies deployment and backup procedures because copying one file effectively copies the entire database.

### Development and Operational Trade-Offs

From a developer's perspective, SQLite is easier to start with because there is no server to install or manage. PostgreSQL requires additional setup but provides significantly more features, stronger consistency guarantees, advanced indexing options, and better scalability.

The choice between the two systems depends less on SQL functionality and more on workload characteristics, concurrency requirements, and operational constraints.

---

## 5. Experiments / Observations

### Observing SQLite Lock Transitions under Concurrency

To understand the practical implications of SQLite's locking mechanism, we wrote an experiment demonstrating what happens when a writer encounters active readers under traditional rollback journal configurations:

```python
import sqlite3
import threading
import time

def reader_thread(db_path):
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    # Acquire a SHARED lock by executing a query
    cursor.execute("SELECT COUNT(*) FROM items")
    print("Reader: Acquired SHARED lock, sleeping...")
    time.sleep(3)
    conn.close()
    print("Reader: Released SHARED lock")

def writer_thread(db_path):
    time.sleep(1) # Ensure reader starts first
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    print("Writer: Attempting to write...")
    try:
        # SQLite moves from UNLOCKED -> RESERVED -> PENDING -> EXCLUSIVE
        cursor.execute("INSERT INTO items (val) VALUES ('Data')")
        conn.commit()
        print("Writer: Write successful")
    except sqlite3.OperationalError as e:
        print(f"Writer Blocked/Failed: {e}")
    finally:
        conn.close()

```

### Observations from the Run

1. When the `writer_thread` attempts to run `INSERT` and commit while `reader_thread` is sleeping (holding a `SHARED` lock), the writer attempts to transition from `RESERVED` to `PENDING`.
2. The database transitions to a `PENDING` state, blocking new readers. However, the writer cannot transition to `EXCLUSIVE` and write to disk because of the active reader's `SHARED` lock.
3. If the timeout threshold (defined by `busy_timeout`) is exceeded before the reader releases the lock, SQLite returns an `OperationalError: database is locked`.

### Architectural Interpretation

This experiment demonstrates one of SQLite's fundamental design trade-offs.

The locking mechanism is intentionally simple and lightweight, which reduces implementation complexity and resource consumption. However, as the number of concurrent operations increases, write transactions become increasingly sensitive to active readers.

For applications such as mobile apps, local tools, and embedded systems, this limitation is rarely problematic because concurrent write workloads are usually low. In highly concurrent environments such as e-commerce systems or financial applications, the same behavior could become a performance bottleneck.

This observation explains why SQLite excels in embedded deployments while PostgreSQL is typically preferred for server-side workloads.

## Architectural Summary

| Category | PostgreSQL | SQLite |
|-----------|------------|---------|
| Primary Goal | Multi-user scalability | Embedded simplicity |
| Architecture | Client-Server | Embedded Library |
| Concurrency Model | MVCC | File Locking |
| Storage Layout | Multiple Files | Single File |
| Best Use Case | Enterprise Applications | Mobile/Desktop Apps |
| Maintenance | Requires Administration | Minimal Administration |
| Write Concurrency | High | Limited |
| Deployment Complexity | Medium-High | Very Low |


## Real-World Use Cases

### PostgreSQL

PostgreSQL is commonly used in systems where multiple users interact with shared data concurrently.

Examples include:

- E-commerce platforms
- Banking and financial systems
- ERP and CRM applications
- SaaS products
- Analytics platforms

These applications benefit from PostgreSQL's MVCC implementation, advanced indexing capabilities, and strong transaction guarantees.

### SQLite

SQLite is commonly used where simplicity and portability are more important than write scalability.

Examples include:

- Android applications
- iOS applications
- Desktop software
- Browser storage
- IoT and embedded devices

In these environments, the database is usually accessed by a single application process, making SQLite's lightweight architecture ideal.
---

## 6. Key Learnings and Architectural Insights

During this comparison, the most important observation was that database architecture is largely driven by workload requirements rather than feature availability.

### Why SQLite Works Well for Mobile Applications

Mobile applications typically have a single active user and operate on local storage. Under these conditions, the overhead of running a dedicated database server provides little benefit. SQLite's embedded architecture eliminates network communication, reduces memory usage, and simplifies deployment.

### Why PostgreSQL Excels in Multi-User Systems

In contrast, enterprise applications often have hundreds or thousands of concurrent users. PostgreSQL's process-based architecture, MVCC implementation, and advanced transaction management allow it to handle concurrent reads and writes efficiently while maintaining strong consistency guarantees.

### Architectural Lessons

Several engineering lessons became clear while studying both systems:

1. Simplicity often improves reliability for small-scale deployments.
2. High concurrency usually requires additional architectural complexity.
3. Storage organization directly influences scalability and recovery capabilities.
4. Database design is a collection of trade-offs rather than universally correct decisions.
5. The best database choice depends on workload requirements rather than popularity or feature count.

## Observation

Studying PostgreSQL and SQLite revealed several important engineering principles:

1. Database architecture is driven by workload requirements.
2. Concurrency support often increases implementation complexity.
3. Storage organization directly affects recovery, scalability, and maintenance.
4. Simplicity can be a significant advantage when workload demands are limited.
5. There is no universally superior database design; every architecture represents a set of trade-offs.

These lessons extend beyond databases and apply to system design in general, where engineering decisions must balance performance, reliability, maintainability, and operational cost.


## References

1. PostgreSQL Official Documentation
   https://www.postgresql.org/docs/

2. SQLite Official Documentation
   https://www.sqlite.org/docs.html

3. PostgreSQL Source Code
   https://github.com/postgres/postgres

4. SQLite Source Code
   https://github.com/sqlite/sqlite

5. Database Internals – Alex Petrov

6. PostgreSQL Wiki
   https://wiki.postgresql.org/