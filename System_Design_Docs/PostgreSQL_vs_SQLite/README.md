# PostgreSQL vs SQLite: Architectural Comparison & System Design Analysis

This document provides a comprehensive system-level comparison of PostgreSQL and SQLite. While both systems implement the SQL standard and provide relational database capabilities, they are architected under fundamentally divergent paradigms to serve distinct operational requirements.

---

## 1. Problem Background

### Why These Database Systems Exist

To understand the architecture of PostgreSQL and SQLite, we must first understand the core problems they were designed to address:

#### 1. PostgreSQL (Enterprise-Grade Multi-User Daemon)
Originating from the INGRES project led by Michael Stonebraker at UC Berkeley in the 1980s, PostgreSQL was built as an extensible, object-relational database management system. It was designed from the beginning for **high concurrency, multi-user access, complex analytical queries, and reliable data integrity in enterprise environments**. It runs as a continuous system service (daemon), mediating all access through network interfaces or local sockets, and isolating user connections from one another.

#### 2. SQLite (Zero-Configuration Embedded Library)
Created by D. Richard Hipp in 2000, SQLite was designed for a completely different scenario: to provide a SQL database engine that requires **zero administration, zero installation, and zero server infrastructure**. Its original use case was for software on Navy destroyers where database server crashes could halt critical machinery. The key goal was to build a system where the database engine is compiled directly into the host application process, and the entire database state resides in a single, standard disk file.

---

## 2. Architecture Overview

The following diagram illustrates the fundamental architectural difference between an embedded database model (SQLite) and a client-server, process-per-connection model (PostgreSQL):

```
+-------------------------------------------------------------------------+
|                              SQLITE MODEL                               |
|                                                                         |
|  [ Application Process Boundary ]                                       |
|  +-------------------------------------------------------------------+  |
|  |  Application Code  ==[Function Calls]==>  SQLite Library          |  |
|  |                                            |                      |  |
|  |                                    +-------v-------+              |  |
|  |                                    | Parser & Opt  |              |  |
|  |                                    +-------+-------+              |  |
|  |                                    | VDBE Engine   |              |  |
|  |                                    +-------+-------+              |  |
|  |                                    | Pager / Cache |              |  |
|  +--------------------------------------------+----------------------+  |
|                                               |                         |
|                                     Direct OS File Locks                |
|                                               |                         |
|                                       +-------v-------+                 |
|                                       | database.db   | (Single File)   |
|                                       +---------------+                 |
+-------------------------------------------------------------------------+

+-------------------------------------------------------------------------+
|                            POSTGRESQL MODEL                             |
|                                                                         |
|  [ Client App A ]      [ Client App B ]      [ Client App C ]           |
|        |                     |                     |                    |
|        +===[ TCP/IP Socket (Port 5432) ]===========+                    |
|                              |                                          |
|  [ PostgreSQL Server Process Boundary ]                                 |
|  +-------------------------------------------------------------------+  |
|  |  Postmaster Daemon (Listener & Connection Router)                 |  |
|  |         |                                                         |  |
|  |         +--- fork() on connection ---> [ Dedicated Backend ]      |  |
|  |                                        | - SQL Compiler           |  |
|  |                                        | - Plan Executor          |  |
|  |                                        +--------+-----------------+  |
|  |                                                 |                    |
|  |   +---------------------------------------------+                    |
|  |   |                                                                  |
|  |   |   +----------------------------------------------------------+   |
|  |   |   | Shared Memory (shared_buffers, Lock Tables, WAL Buffers) |   |
|  |   |   +----------------------------+-----------------------------+   |
|  +---|--------------------------------|------------------------------+  |
|      |                                |                                 |
|      | Disk Writes                    | Shared I/O Coordination         |
|  +---v--------------------------------v-------------------------------+  |
|  | /PGDATA Directory (Multi-file layout: base/, pg_wal/, etc.)        |  |
|  +--------------------------------------------------------------------+  |
+-------------------------------------------------------------------------+
```

### Key Differences at a Glance

| Architectural Dimension | SQLite | PostgreSQL |
| :--- | :--- | :--- |
| **Execution Model** | In-process Library | Server Daemon (Client-Server) |
| **Connection Handling** | Native thread calls, shared memory context | Multi-process (`fork()` model) |
| **Database Storage** | Single file containing all tables/indexes | Multi-file directory tree structure |
| **Isolation Level Support** | Serialisable (via database-wide locks) | Read Committed, Repeatable Read, Serialisable |
| **Concurrency Control** | File-level coarse locking (WAL mode improves this) | Multi-Version Concurrency Control (MVCC) |

---

## 3. Internal Design

### 3.1 Process & Execution Model

#### PostgreSQL: Multi-Process Isolation
PostgreSQL handles concurrent client requests using a process-based model. When the master process (known as `postmaster`) accepts a connection, it invokes the `fork()` system call to spawn a dedicated backend process for that client.
* **Fault Isolation**: If a query triggers a segmentation fault or memory leak in one backend process, only that connection terminates. The rest of the database cluster remains functional.
* **Shared Memory**: All spawned backend processes communicate and synchronize state via a shared memory segment. This segment houses:
  * **Shared Buffers**: Pointers and blocks caching data pages.
  * **Lock Table**: Global locking structures coordinating reader/writer conflicts.
  * **WAL Buffers**: Pre-allocation block for transaction logs before disk commit.
* **IPC Overhead**: Spawning processes via `fork()` consumes significant kernel memory and overhead. To manage high connection rates, PostgreSQL deployments frequently rely on external connection poolers (e.g., PgBouncer).

#### SQLite: In-Process Virtual Database Engine (VDBE)
SQLite does not run background processes or daemons. All operations occur within the address space of the caller application.
* **VDBE Assembly**: SQL statements are compiled into assembly-like instructions for SQLite’s internal register-based virtual machine, the **Virtual Database Engine (VDBE)**.
* **Zero Inter-Process Communication (IPC)**: Retrieving data is as fast as a programming language function call. No networking stacks, serialize/deserialize overhead, or process contexts are involved.
* **Resource Sharing**: If multiple threads within the same application process query the database, they share cache memory, avoiding redundant file page loads.

---

### 3.2 Storage Engine & Database Layout

#### PostgreSQL Disk Storage Structure
PostgreSQL organizes data within a designated configuration directory (`PGDATA`).
* **Segments**: The heap data for a table or index is split into 1 GB segment files. This design ensures compatibility with operating systems that restrict maximum file sizes.
* **Directory Trees**: Files are structured hierarchically:
  * `base/`: Subdirectories for individual databases, named by OID.
  * `pg_wal/`: Write-Ahead Log segments recording transitions.
  * `pg_xact/`: Transaction commit status bit arrays.
* **Page Internals**: Inside a standard 8 KB page, PostgreSQL positions item identifiers (pointers to rows) growing downward from the page header, while the actual tuple structures grow upward from the bottom of the page. This prevents page layout fragmentation.

#### SQLite Disk Storage Structure
SQLite packs the entire database schema, tables, indices, and system configurations into **a single file**.
* **Global Headers**: The first 100 bytes of the file contain essential metadata, such as page size, write/read versioning tags, and schema cookie numbers.
* **B-Trees vs. B+ Trees**: SQLite implements two B-tree variants:
  * **Table B-Trees**: Used to store actual row data. Keys are 64-bit integer row IDs, and leaves contain payloads (non-key columns).
  * **Index B-Trees**: Used for indexes. Keys are index values appended with row IDs, and no payloads reside at leaf nodes.
* **Page Sizing**: Page size is configurable at database creation (typically 4096 bytes to match modern sector layouts). Pages are mapped dynamically as B-tree nodes.

---

### 3.3 Transaction Management & Concurrency Control

#### PostgreSQL: Fine-Grained MVCC
PostgreSQL achieves concurrent read/write isolation using Multi-Version Concurrency Control (MVCC).
* **No Blocking Readers/Writers**: A transaction modifying a row writes a new version of the row (tuple) to the heap. Readers continue reading the older committed version by referencing transaction snapshots.
* **Transaction Identifiers**: Rows store transaction metadata in header columns (`xmin` for insertion ID, `xmax` for deletion/update ID).
* **Locks**: PostgreSQL supports fine-grained row-level locking. Row locks are stored in the shared memory lock table, meaning locking millions of rows does not run out of memory or trigger lock escalation.

#### SQLite: Database-Level File Locking
SQLite manages concurrency by locking the entire database file using the operating system's filesystem lock mechanism.
* **State Transition**: SQLite uses a lock state machine with five distinct states:
  * `UNLOCKED`: No connections are interacting with the database.
  * `SHARED`: Readers are active. Any number of connections can hold a shared lock, preventing write locks.
  * `RESERVED`: A single connection plans to write. It reserves the right to modify pages in cache but has not written to disk yet.
  * `PENDING`: A writer wants to commit. No new readers can acquire `SHARED` locks, waiting for existing readers to finish.
  * `EXCLUSIVE`: The writer holds the lock, writes the changes to disk, and blocks all other readers and writers.
* **WAL Mode Concurrency**: In Write-Ahead Log (WAL) mode, SQLite decouples readers and writers. Writes append to a sidecar file (`.db-wal`) rather than directly modifying the main database file. This allows one writer and multiple concurrent readers to coexist.

---

## 4. Design Trade-Offs

### 1. Scaling vs. Simplicity
* **PostgreSQL** prioritizes horizontal and vertical scale. Spawning processes allows it to fully utilize multiple CPU cores for parallel query execution. However, this model carries administrative overhead, requiring memory tuning (`work_mem`, `maintenance_work_mem`) and replication configurations.
* **SQLite** prioritizes zero-configuration. The engine is maintenance-free. There are no daemons to monitor or ports to open. The trade-off is that SQLite cannot exploit multi-core systems for a single query, nor can it naturally distribute load across different host processes.

### 2. Lock Granularity & Write Concurrency
* **PostgreSQL** handles high-concurrency workloads. Hundreds of active connections can write to different rows in the same table simultaneously.
* **SQLite** forces write serialization. In standard rollback journal mode, only one connection can write to the entire database at a time. Even in WAL mode, write transactions are serialized database-wide.

### 3. Data Integrity & Type Safety
* **PostgreSQL** enforces strict type constraints, checks, foreign key validations, and schemas. Attempts to insert invalid types fail at compile-time.
* **SQLite** historically features dynamic typing (or "type affinity"). You can insert string values into an integer column. While this provides flexibility, it places data validation logic on the application code.

---

## 5. Experiments / Observations

### Observing SQLite Lock Transitions under Concurrency

To understand the practical implications of SQLite's locking mechanism, we can write a short experiment demonstrating what happens when a writer encounters readers under traditional rollback journal configurations:

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

#### Observations from the Run
1. When the `writer_thread` attempts to run `INSERT` and commit while `reader_thread` is sleeping (holding a `SHARED` lock), the writer attempts to transition from `RESERVED` to `PENDING`.
2. The database transitions to a `PENDING` state, blocking new readers. However, the writer cannot transition to `EXCLUSIVE` and write to disk because of the active reader's `SHARED` lock.
3. If the timeout threshold (defined by `busy_timeout`) is exceeded before the reader releases the lock, SQLite returns an `OperationalError: database is locked`.

---

## 6. Key Learnings & Architectural Answers

### Why SQLite works exceptionally well for mobile applications:
SQLite is optimized for local single-user access. In a mobile environment (iOS or Android), there is only one user, and network latency is highly undesirable. Having an embedded engine compiled within the mobile binary avoids network overhead, fits into a small memory footprint (under 1 MB compiled size), and simplifies backups to a single file.

### Why PostgreSQL is preferred for large multi-user systems:
PostgreSQL's multi-process isolation ensures that high-volume writes, heavy read queries, and complex analytical operations are executed concurrently without blocking. Its locking architecture operates at the row level, and its process isolation guards against system-wide failures if a single transaction crashes.

### The underlying architectural decisions leading to these differences:
It is a choice of **centralized safety** versus **decentralized convenience**. PostgreSQL treats the database as a long-lived service that owns the underlying hardware. SQLite treats the database as an application document, leaving resource scheduling, networking, and security up to the operating process that hosts it.
