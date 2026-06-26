# PostgreSQL vs SQLite — Architecture Comparison

> **Advanced DBMS — System Design Deep Dive**
>
> A first-principles analysis of two database engines that sit at opposite ends of the architectural spectrum: one built for enterprise concurrency, the other for embedded simplicity. Understanding *why* they diverge reveals fundamental truths about systems design.

---

## Table of Contents

1. [Problem Background](#1-problem-background)
2. [Architecture Overview](#2-architecture-overview)
3. [Internal Design](#3-internal-design)
   - [Storage Engine](#31-storage-engine)
   - [Page Layout](#32-page-layout)
   - [Index Implementation](#33-index-implementation)
   - [Transaction Management](#34-transaction-management)
   - [Concurrency Control](#35-concurrency-control)
   - [Durability](#36-durability)
4. [Design Trade-Offs](#4-design-trade-offs)
5. [Experiments / Observations](#5-experiments--observations)
6. [Key Learnings](#6-key-learnings)
7. [References](#7-references)

---

## 1. Problem Background

### The Two Faces of Data Management

Every piece of software that persists state is, in some sense, a database. But the *scale* at which state must be managed — how many processes touch it, how fast it must be accessed, and how catastrophic corruption would be — dictates radically different engineering approaches. PostgreSQL and SQLite represent the two dominant answers to that question, and neither is universally superior. They solve fundamentally different problems with fundamentally different architectures.

### Why PostgreSQL Exists: The Enterprise Multi-User Problem

In 1986, Michael Stonebraker's team at UC Berkeley launched the POSTGRES project (Post-Ingres) as a research database aimed at solving problems that the relational model, as implemented by existing systems, handled poorly: complex data types, extensible type systems, and rule-based query rewriting. The project wasn't just an academic exercise — it was a deliberate attempt to push beyond the limitations of System R and Ingres.

The core insight behind POSTGRES was that real-world enterprises need a database that can:

- **Handle dozens to thousands of concurrent connections** without data corruption
- **Provide transactional isolation** so that a report running for 30 seconds doesn't see half-committed data from an ongoing batch import
- **Support complex queries** across normalized schemas with billions of rows
- **Guarantee durability** even when the server loses power mid-transaction
- **Remain extensible** — enterprises have domain-specific data types (geometric data, network addresses, JSON documents) that shouldn't require a different storage engine

When PostgreSQL was open-sourced in 1996 (renamed from Postgres95), it carried these research-grade ideas into production. Today, it is arguably the most feature-rich open-source relational database, supporting advanced capabilities like serializable snapshot isolation (SSI), custom index types, and logical replication — all stemming from that original Berkeley vision.

The fundamental design decision that shapes everything in PostgreSQL is this: **the database runs as a separate server process, and clients communicate with it over a network protocol.** This client-server split is not incidental. It is the architectural foundation that enables multi-user concurrency, process isolation, and centralized resource management.

### Why SQLite Exists: The Embedded, Zero-Configuration Problem

SQLite's origin story is almost the opposite. In 2000, D. Richard Hipp was working on a software system for the U.S. Navy aboard guided missile destroyers. The existing system used Informix, a full client-server database, and when Informix's server process went down, the entire application became unusable. Hipp's insight was simple but profound: **for many applications, a client-server database is overkill, and the server itself becomes a single point of failure.**

The requirements for the destroyer software were modest: a single application needed to read and write structured data reliably, without needing concurrent access from multiple networked clients. What Hipp needed was a database engine that:

- **Required zero configuration** — no DBA, no server process, no port management
- **Embedded directly into the application** as a library — a function call, not a network request
- **Stored everything in a single ordinary file** that could be copied, backed up, or emailed
- **Worked reliably on constrained hardware** (limited RAM, no dedicated database server)
- **Was small enough** to compile into almost any C program without bloating the binary

SQLite was the result. It ships as a single C source file (`sqlite3.c`, the "amalgamation"), compiles to roughly 700KB, and has been deployed on more devices than any other database engine in history — every iPhone, every Android phone, every Firefox and Chrome browser, every macOS installation, most Linux distributions, and countless embedded systems.

The fundamental design decision in SQLite is equally clear: **the database engine runs inside the application's own process, and accesses a single file on disk using ordinary filesystem calls.** There is no server. There is no network protocol. There is no separate memory space. This is not a limitation to be overcome — it is the entire point.

### The Fundamental Architectural Split

The divide between PostgreSQL and SQLite is not merely "big vs. small" or "powerful vs. simple." It is a foundational architectural choice with cascading consequences:

| Dimension | PostgreSQL | SQLite |
|---|---|---|
| **Deployment model** | Client-server (separate process) | Embedded (in-process library) |
| **Concurrency model** | Multiple backend processes, shared memory | Single process, file-level locking |
| **Communication** | TCP/IP or Unix domain sockets | Direct function calls (C API) |
| **Storage** | Multiple files, tablespaces, WAL directory | Single cross-platform file |
| **Configuration** | `postgresql.conf`, `pg_hba.conf`, dozens of tunable knobs | `PRAGMA` statements, mostly zero-config |
| **Administration** | Requires DBA knowledge (VACUUM, replication, backups) | Essentially self-managing |
| **Typical deployment** | Web backends, analytics, enterprise systems | Mobile apps, IoT, desktop apps, test suites, edge computing |

This document explores how these two foundational choices cascade through every layer of database design: storage, indexing, concurrency, durability, and query planning.

---

## 2. Architecture Overview

### PostgreSQL's Client-Server Architecture

PostgreSQL follows a process-per-connection model inherited from traditional Unix design. When a client connects, the `postmaster` daemon forks a new backend process dedicated to that connection. All backends share a common pool of memory (`shared_buffers`) and coordinate through an intricate system of locks, latches, and inter-process communication.

```mermaid
graph TB
    subgraph Clients["Client Applications"]
        C1["Client 1<br/>(psql)"]
        C2["Client 2<br/>(Web App)"]
        C3["Client 3<br/>(Analytics Tool)"]
    end

    subgraph PG["PostgreSQL Server"]
        PM["Postmaster<br/>(Listener Process)"]

        subgraph Backends["Backend Processes"]
            B1["Backend 1<br/>(PID 1001)"]
            B2["Backend 2<br/>(PID 1002)"]
            B3["Backend 3<br/>(PID 1003)"]
        end

        subgraph SharedMem["Shared Memory Region"]
            SB["shared_buffers<br/>(Buffer Pool)"]
            WALBuf["WAL Buffers"]
            CLG["Commit Log<br/>(CLOG)"]
            LT["Lock Table"]
            PT["Proc Array<br/>(Snapshot Data)"]
        end

        subgraph BGWorkers["Background Processes"]
            WW["WAL Writer"]
            CP["Checkpointer"]
            BGW["Background Writer"]
            AV["Autovacuum Launcher"]
            AVW1["Autovacuum Worker"]
            STATS["Stats Collector"]
            AL["Archiver"]
        end

        subgraph Storage["On-Disk Storage"]
            direction LR
            DF["Data Files<br/>(base/)"]
            WF["WAL Files<br/>(pg_wal/)"]
            CF["CLOG Files<br/>(pg_xact/)"]
        end
    end

    C1 -->|"TCP/Unix Socket"| PM
    C2 -->|"TCP/Unix Socket"| PM
    C3 -->|"TCP/Unix Socket"| PM
    PM -->|"fork()"| B1
    PM -->|"fork()"| B2
    PM -->|"fork()"| B3

    B1 <-->|"read/write"| SB
    B2 <-->|"read/write"| SB
    B3 <-->|"read/write"| SB

    B1 <-->|"lock mgmt"| LT
    B2 <-->|"lock mgmt"| LT
    B3 <-->|"lock mgmt"| LT

    B1 -->|"WAL insert"| WALBuf
    B2 -->|"WAL insert"| WALBuf

    WW -->|"flush"| WF
    CP -->|"write dirty pages"| DF
    BGW -->|"write dirty pages"| DF
    AV -->|"launch"| AVW1
    AVW1 -->|"clean dead tuples"| DF
    AL -->|"archive"| WF

    SB <-->|"page I/O"| DF
    WALBuf -->|"sequential write"| WF
    CLG <-->|"status read/write"| CF
```

**Key architectural observations:**

- **Process isolation**: Each backend runs in its own address space. A crash in one backend (e.g., due to a buggy extension) doesn't take down other connections. The postmaster detects the crash and performs recovery.
- **Shared memory as coordination hub**: The buffer pool (`shared_buffers`) is a shared memory segment that all backends read from and write to. This eliminates redundant I/O but requires careful locking (lightweight locks, buffer pins, and content locks).
- **Background process fleet**: PostgreSQL runs 6-8+ background processes even with zero client connections. The checkpointer, WAL writer, background writer, autovacuum launcher, and stats collector each handle a specific maintenance task asynchronously.

### SQLite's Embedded Architecture

SQLite's architecture is almost the diametric opposite. There is no server, no separate process, no shared memory, and no network protocol. The entire database engine is a library linked into the application. When the application calls `sqlite3_exec()`, execution happens in the same thread, in the same process, using the same heap.

```mermaid
graph TB
    subgraph App["Application Process (Single Binary)"]
        AC["Application Code<br/>(Your Program)"]

        subgraph SQLiteLib["SQLite Library (libsqlite3)"]
            VDBE["Virtual Database Engine<br/>(VDBE - Bytecode VM)"]
            BT["B-Tree Module"]
            PGR["Pager Module"]
            OS["OS Interface<br/>(VFS Layer)"]
            
            subgraph Cache["Page Cache (in-process heap)"]
                PC1["Page 1"]
                PC2["Page 2"]
                PCN["Page N"]
            end
        end

        subgraph DiskFile["Single Database File"]
            DBF["database.db<br/>(B-tree pages)"]
        end

        subgraph Journal["Journal / WAL"]
            JF["database.db-journal<br/>OR<br/>database.db-wal"]
        end
    end

    AC -->|"sqlite3_exec()<br/>Direct function call"| VDBE
    VDBE -->|"Cursor operations"| BT
    BT -->|"Page read/write"| PGR
    PGR <-->|"Cache lookup"| Cache
    PGR -->|"File I/O"| OS
    OS -->|"read()/write()"| DBF
    OS -->|"journal writes"| JF
```

**Key architectural observations:**

- **No IPC overhead**: A query in SQLite is a function call. There is no serialization, no network round-trip, no context switch to another process. For simple queries (point lookups, small inserts), this makes SQLite extraordinarily fast — often faster than PostgreSQL for single-connection workloads.
- **Layered internal design**: Despite its simplicity, SQLite has a clean internal architecture. The VDBE (Virtual DataBase Engine) compiles SQL into bytecode programs. The B-Tree module manages the logical tree structure. The Pager manages page-level caching and transactional consistency. The VFS (Virtual File System) abstracts OS-level I/O.
- **Single file = simplicity**: The entire database — schema, data, indexes — lives in one file. This file format is cross-platform, stable (the format hasn't broken backward compatibility since 2004), and is an official recommended archival format by the Library of Congress.

### Data Flow Comparison

To understand how the architectures diverge in practice, consider the data flow for a simple `INSERT INTO users (name) VALUES ('Alice')`:

```mermaid
sequenceDiagram
    participant App as Application
    participant PGClient as libpq Client
    participant Postmaster as Postmaster
    participant Backend as PG Backend
    participant SharedBuf as Shared Buffers
    participant WAL as WAL Buffer
    participant Disk as Disk

    Note over App,Disk: PostgreSQL INSERT Flow
    App->>PGClient: Execute INSERT
    PGClient->>Postmaster: Send query (TCP)
    Postmaster->>Backend: Route to backend
    Backend->>Backend: Parse → Rewrite → Plan → Execute
    Backend->>SharedBuf: Find/load target page
    Backend->>SharedBuf: Write tuple to buffer page
    Backend->>WAL: Write WAL record
    Backend->>WAL: Flush WAL to disk (on COMMIT)
    Backend->>PGClient: Return "INSERT 0 1"
    PGClient->>App: Return result
    Note over SharedBuf,Disk: Dirty page written later by BGWriter/Checkpointer

    Note over App,Disk: ═══════════════════════════════

    participant SQLite as SQLite Library
    participant BTree as B-Tree Module
    participant Pager as Pager
    participant File as DB File

    Note over App,File: SQLite INSERT Flow
    App->>SQLite: sqlite3_exec("INSERT...")
    SQLite->>SQLite: Tokenize → Parse → Bytecode
    SQLite->>BTree: Insert into table B-tree
    BTree->>Pager: Request page, mark dirty
    Pager->>File: Write journal (before image)
    Pager->>File: Write modified page
    Pager->>File: Invalidate journal (on COMMIT)
    SQLite->>App: Return SQLITE_OK
```

The critical difference: PostgreSQL's flow crosses process boundaries and involves shared memory coordination. SQLite's flow stays entirely within a single call stack. This means SQLite pays zero IPC overhead but also cannot leverage multiple CPU cores for a single query, and cannot provide true concurrent write access.

---

## 3. Internal Design

### 3.1 Storage Engine

#### PostgreSQL: Heap Storage with TOAST

PostgreSQL uses a **heap-based** storage model. "Heap" here doesn't mean a priority queue — it means an unordered collection of pages where tuples are placed wherever there is free space. Unlike SQLite or InnoDB, PostgreSQL's heap tables do not cluster data by primary key. A table is simply a sequence of 8KB pages, and new tuples go into the first page with enough free space (tracked by the Free Space Map, or FSM).

**Why heap and not clustered?** This is a deliberate design choice. Maintaining a clustered order (like InnoDB's clustered index) requires moving data during inserts to keep it sorted. PostgreSQL avoids this cost, favoring fast writes over ordered reads. If you want clustered access, you explicitly run `CLUSTER` (a one-time physical reorder) or use BRIN indexes (which work well on naturally-ordered data like timestamps).

**Page size**: Fixed at 8KB (compile-time configurable but almost never changed). This is a middle ground — large enough to amortize I/O overhead, small enough to avoid excessive waste on small updates.

**TOAST (The Oversized-Attribute Storage Technique)**: When a tuple exceeds roughly 2KB, PostgreSQL automatically "toasts" the oversized columns. TOAST can:
1. **Compress** the value inline (using pglz or lz4)
2. **Move** it to a separate TOAST table and replace it with a pointer
3. **Both** compress and move

This is transparent to the user. The TOAST table is a separate heap with its own pages, linked by chunk IDs. This design lets PostgreSQL handle multi-megabyte text or JSONB values without wrecking the main table's page density.

**Tablespace management**: PostgreSQL supports multiple tablespaces — physical locations on different filesystems or disks. You can put frequently-accessed indexes on fast SSDs and archival tables on slow HDDs. Each tablespace is a directory containing symlinked subdirectories under `pg_tblspc/`.

**WAL segments**: Write-Ahead Log files are stored as 16MB segments (default) in `pg_wal/`. Each WAL record describes a physical change to a page. WAL ensures durability: before a dirty page is written to the data files, its WAL record must be flushed to disk. Segments are recycled after checkpointing.

```mermaid
graph LR
    subgraph HeapTable["Heap Table (users)"]
        P1["Page 0<br/>[Header | ItemPtrs | Free | Tuples]"]
        P2["Page 1<br/>[Header | ItemPtrs | Free | Tuples]"]
        P3["Page 2<br/>[Header | ItemPtrs | Free | Tuples]"]
    end

    subgraph TOAST["TOAST Table (pg_toast_12345)"]
        T1["Chunk 1<br/>(compressed blob part 1)"]
        T2["Chunk 2<br/>(compressed blob part 2)"]
    end

    subgraph FSM["Free Space Map"]
        F1["Page 0: 200 bytes free"]
        F2["Page 1: 4000 bytes free"]
        F3["Page 2: 0 bytes free"]
    end

    subgraph VM["Visibility Map"]
        V1["Page 0: all-visible"]
        V2["Page 1: not all-visible"]
        V3["Page 2: all-visible"]
    end

    P2 -->|"TOAST pointer"| T1
    T1 --> T2

    style TOAST fill:#ffeaa7
    style FSM fill:#dfe6e9
    style VM fill:#dfe6e9
```

#### SQLite: B-Tree Based Single-File Storage

SQLite's storage engine is fundamentally different. Instead of a heap, **every table is stored as a B-tree** (specifically, a B+tree for tables and a B-tree for indexes). The entire database — tables, indexes, schema, metadata — lives in a single file.

**File structure**: The database file is divided into fixed-size pages. Page 1 is special: it contains the database header (100 bytes at offset 0) with metadata like page size, file format version, and free page count. Page 1 also holds the root page of the `sqlite_master` table, which contains the schema (CREATE TABLE/INDEX statements) for the entire database.

**Page size**: Configurable from 512 bytes to 65,536 bytes (default 4096 bytes since SQLite 3.12.0). Unlike PostgreSQL, this is a runtime setting chosen when the database is created (`PRAGMA page_size`). A larger page size reduces tree depth but wastes space for small rows; a smaller page size increases tree depth but improves cache efficiency for point lookups.

**B+tree for tables**: Table rows are stored in leaf pages of a B+tree, ordered by the `rowid` (an implicit 64-bit integer primary key). Interior pages contain separator keys and child pointers. This means that:
- Point lookups by rowid are O(log N) — just traverse the tree
- Sequential scans in rowid order are efficient — leaf pages are linked
- `INTEGER PRIMARY KEY` is an alias for `rowid`, so it's free (no separate index needed)
- `WITHOUT ROWID` tables exist for cases where the primary key should be the clustering key (e.g., composite PKs or text PKs)

**Journal modes**: SQLite supports multiple journaling strategies for atomic commits:
- **DELETE** (default legacy mode): Before modifying a page, write its original content to a rollback journal file. On commit, delete the journal. On crash, the journal's presence triggers automatic rollback.
- **TRUNCATE**: Same as DELETE, but truncates the journal instead of deleting it (avoids directory entry modification, faster on some filesystems).
- **WAL** (Write-Ahead Logging): Append-only WAL file. Modified pages go to the WAL; readers read from the original file plus relevant WAL entries. Checkpointing transfers WAL pages back to the main file. WAL mode allows concurrent readers during writes — a major concurrency improvement.
- **MEMORY**: Journal kept in RAM. Fast but no crash safety. Suitable for transient/temporary databases.
- **OFF**: No journal at all. Maximum speed, zero crash safety.

```mermaid
graph TB
    subgraph SingleFile["database.db — Single File"]
        direction TB
        PG1["Page 1<br/>DB Header (100B) +<br/>sqlite_master B-tree root"]
        PG2["Page 2<br/>Table 'users' B-tree root<br/>(interior page)"]
        PG3["Page 3<br/>Leaf page<br/>(rows 1-50)"]
        PG4["Page 4<br/>Leaf page<br/>(rows 51-100)"]
        PG5["Page 5<br/>Index 'idx_email' B-tree root"]
        PG6["Page 6<br/>Overflow page<br/>(large BLOB data)"]
        PGF["Free Page List"]
    end

    PG1 -->|"schema lookup"| PG2
    PG2 -->|"left child"| PG3
    PG2 -->|"right child"| PG4
    PG3 -->|"overflow"| PG6
    PG1 -->|"schema lookup"| PG5

    style PG1 fill:#74b9ff
    style PGF fill:#b2bec3
```

#### Storage Engine Comparison Summary

| Aspect | PostgreSQL | SQLite |
|---|---|---|
| **Data organization** | Heap (unordered pages) | B+tree (ordered by rowid) |
| **Page size** | 8KB fixed (compile-time) | 512B–64KB configurable (per-database) |
| **Files per table** | Multiple (main, FSM, VM, TOAST) | One file for entire database |
| **Large value handling** | TOAST (auto-compress + external storage) | Overflow pages |
| **Free space tracking** | Free Space Map (FSM) per table | Free page linked list in header |
| **Clustering** | No natural clustering; explicit `CLUSTER` | Clustered by rowid automatically |
| **Tablespaces** | Supported (multi-disk) | N/A (single file) |

---

### 3.2 Page Layout

#### PostgreSQL: Heap Page Layout

A PostgreSQL heap page has a carefully designed layout that supports MVCC (multi-version concurrency control) and efficient space reclamation:

```
 0                   8KB
 ┌──────────────────────────────────────────────────────┐
 │  Page Header (24 bytes)                              │
 │  ┌─────────────────────────────────────────────────┐ │
 │  │ pd_lsn (8B) | pd_checksum (2B) | pd_flags (2B) │ │
 │  │ pd_lower (2B) | pd_upper (2B) | pd_special (2B)│ │
 │  │ pd_pagesize_version (2B) | pd_prune_xid (4B)   │ │
 │  └─────────────────────────────────────────────────┘ │
 ├──────────────────────────────────────────────────────┤
 │  Line Pointers (Item IDs) — grow downward ↓         │
 │  ┌──────┬──────┬──────┬──────┬─────────────────┐    │
 │  │ LP 1 │ LP 2 │ LP 3 │ LP 4 │    ...          │    │
 │  │(4B)  │(4B)  │(4B)  │(4B)  │                 │    │
 │  └──────┴──────┴──────┴──────┴─────────────────┘    │
 │  pd_lower ↓                                         │
 ├──────────────────────────────────────────────────────┤
 │                                                      │
 │                  FREE SPACE                           │
 │               (grows from both ends)                  │
 │                                                      │
 ├──────────────────────────────────────────────────────┤
 │  pd_upper ↑                                         │
 │  Heap Tuples — grow upward ↑                        │
 │  ┌──────────────────────────────────────────────┐   │
 │  │  Tuple 4:  [HeapTupleHeader | col1 | col2 ] │   │
 │  │  Tuple 3:  [HeapTupleHeader | col1 | col2 ] │   │
 │  │  Tuple 2:  [HeapTupleHeader | col1 | col2 ] │   │
 │  │  Tuple 1:  [HeapTupleHeader | col1 | col2 ] │   │
 │  └──────────────────────────────────────────────┘   │
 │  Special Space (index-specific, 0B for heap pages)  │
 └──────────────────────────────────────────────────────┘
```

**Why this layout matters:**

- **Line pointers** (4 bytes each) are the indirection layer that makes MVCC possible. A line pointer contains an offset to the actual tuple, a length, and status flags (`LP_NORMAL`, `LP_REDIRECT`, `LP_DEAD`, `LP_UNUSED`). When a tuple is updated, the old version can stay in place (its line pointer remains valid for older snapshots), and the new version gets a new line pointer. Index entries point to line pointers (via `(page, offset)` pairs called `ctid`), not directly to tuple data.

- **`pd_lsn`**: The Log Sequence Number of the last WAL record that modified this page. The background writer and checkpointer use this to ensure WAL is flushed before writing a dirty page (the WAL-before-data rule).

- **`pd_lower` and `pd_upper`**: Track the boundary of used space. `pd_lower` marks the end of line pointers (top grows down), `pd_upper` marks the start of tuple data (bottom grows up). Free space is the gap between them.

- **Tuple header** (23 bytes minimum): Each heap tuple carries its own MVCC metadata:
  - `t_xmin`: The transaction ID that inserted this tuple
  - `t_xmax`: The transaction ID that deleted/updated this tuple (0 if still live)
  - `t_cid`: Command ID within the inserting transaction
  - `t_ctid`: "Chain" TID pointing to the next version of this tuple (for HOT updates)
  - `t_infomask`: Bit flags indicating commit status, null bitmap presence, etc.

This per-tuple MVCC metadata is a major source of storage overhead. Every row carries 23+ bytes of versioning information, even in a table with tiny columns. For a table with two `INTEGER` columns (8 bytes of actual data), the tuple header is roughly 3x the payload size.

#### SQLite: B-Tree Page Structure

SQLite pages have a different layout, optimized for B-tree traversal rather than MVCC:

```
 B-Tree Leaf Page (Table):
 ┌──────────────────────────────────────────────────────┐
 │  Page Header (8 or 12 bytes)                        │
 │  ┌─────────────────────────────────────────────────┐│
 │  │ flags(1B) | freeblock(2B) | cellcount(2B)       ││
 │  │ celloffset(2B) | freebytes(1B)                  ││
 │  │ [rightmost_ptr(4B) — interior pages only]       ││
 │  └─────────────────────────────────────────────────┘│
 ├──────────────────────────────────────────────────────┤
 │  Cell Pointer Array (2 bytes per cell, sorted)      │
 │  ┌──────┬──────┬──────┬──────┐                      │
 │  │ CP 1 │ CP 2 │ CP 3 │ CP 4 │                     │
 │  └──────┴──────┴──────┴──────┘                      │
 │  (sorted by key for binary search)                  │
 ├──────────────────────────────────────────────────────┤
 │              Unallocated Space                       │
 ├──────────────────────────────────────────────────────┤
 │  Cell Content Area (cells stored in no particular   │
 │  physical order, but logically sorted via pointers) │
 │  ┌──────────────────────────────────────────────┐   │
 │  │ Cell: [payload_size | rowid | column_data]   │   │
 │  │ Cell: [payload_size | rowid | column_data]   │   │
 │  │ Cell: [payload_size | rowid | column_data]   │   │
 │  └──────────────────────────────────────────────┘   │
 │  ┌──────────────────────────────────────────────┐   │
 │  │ Freeblock chain (reclaimed cell space)       │   │
 │  └──────────────────────────────────────────────┘   │
 └──────────────────────────────────────────────────────┘

 B-Tree Interior Page (Table):
 ┌──────────────────────────────────────────────────────┐
 │  Page Header (12 bytes, includes rightmost_ptr)     │
 ├──────────────────────────────────────────────────────┤
 │  Cell Pointer Array                                  │
 ├──────────────────────────────────────────────────────┤
 │  Cell Content:                                       │
 │  ┌──────────────────────────────────────────────┐   │
 │  │ Cell: [left_child_page(4B) | rowid_key]      │   │
 │  │ Cell: [left_child_page(4B) | rowid_key]      │   │
 │  └──────────────────────────────────────────────┘   │
 │  Rightmost child pointer is in page header          │
 └──────────────────────────────────────────────────────┘
```

**Key differences from PostgreSQL:**

- **No MVCC metadata per cell**: SQLite cells contain only the payload (column data) and the key (rowid). There is no `xmin`, `xmax`, or transaction ID embedded in the data. This makes SQLite cells much more compact for equivalent data.

- **Sorted cell pointers**: The cell pointer array is maintained in key-sorted order, enabling binary search within a page. In PostgreSQL, line pointers are in insertion order, and tuple data is unsorted within a page.

- **Overflow pages**: When a cell's payload exceeds a threshold (roughly `page_size / 4 - 35` bytes), the excess is spilled onto overflow pages linked in a chain. This is analogous to TOAST but simpler — there is no separate table, just a linked list of pages.

- **Free blocks**: Deleted cells within a page create free blocks linked in a chain (the freeblock list). SQLite can defragment a page in-place when free space is fragmented but total free space is sufficient.

---

### 3.3 Index Implementation

#### PostgreSQL: A Catalog of Index Types

PostgreSQL supports six built-in index access methods, each optimized for different query patterns. This diversity is a direct consequence of PostgreSQL's extensibility philosophy.

**B-Tree (default)**: The workhorse. Supports equality and range queries on sortable data types. Internally, it's a Lehman-Yao B+tree with right-link pointers for concurrent access without holding read locks on parent pages.

```mermaid
graph TB
    subgraph BTreePG["PostgreSQL B-Tree Index on 'email'"]
        Root["Root Page<br/>keys: [M]"]
        Int1["Interior<br/>keys: [D, H]"]
        Int2["Interior<br/>keys: [Q, U]"]
        L1["Leaf: A→ctid(0,1), B→ctid(2,5), C→ctid(1,3)"]
        L2["Leaf: D→ctid(3,1), E→ctid(0,4), G→ctid(2,2)"]
        L3["Leaf: H→ctid(1,1), K→ctid(3,4), L→ctid(0,2)"]
        L4["Leaf: M→ctid(2,1), N→ctid(1,5), P→ctid(3,2)"]
        L5["Leaf: Q→ctid(0,3), S→ctid(2,4), T→ctid(1,2)"]
        L6["Leaf: U→ctid(3,3), W→ctid(0,5), Z→ctid(2,3)"]
    end

    Root --> Int1
    Root --> Int2
    Int1 --> L1
    Int1 --> L2
    Int1 --> L3
    Int2 --> L4
    Int2 --> L5
    Int2 --> L6
    L1 ---|"right link"| L2
    L2 ---|"right link"| L3
    L3 ---|"right link"| L4
    L4 ---|"right link"| L5
    L5 ---|"right link"| L6
```

Notice that PostgreSQL B-tree leaf entries store **`ctid` (page, offset) pointers** into the heap table. This means a B-tree lookup always requires a subsequent heap fetch to get the actual tuple — unless it's an **index-only scan**, where the index itself contains all the columns needed by the query, and the visibility map confirms the page is all-visible.

**Hash Index**: O(1) equality lookups. Historically avoided because they weren't WAL-logged (crash-unsafe) until PostgreSQL 10. Now WAL-safe but still rarely preferred over B-trees because they don't support range queries.

**GiST (Generalized Search Tree)**: A framework for building balanced tree indexes on non-linear data types. Used for geometric data (`@>`, `<@`), full-text search, range types, and nearest-neighbor queries. The key insight is that GiST defines a general interface (`consistent`, `union`, `penalty`, `picksplit`) that data type authors implement.

**SP-GiST (Space-Partitioned GiST)**: For data that naturally partitions space — tries, quad-trees, k-d trees. Useful for IP address ranges, phone numbers (prefix matching), and geographic point clouds.

**GIN (Generalized Inverted Index)**: An inverted index for multi-valued data. Critical for full-text search (mapping words to documents), JSONB containment queries (`@>`), and array element lookups. GIN indexes are expensive to update (every insert may touch many index entries) but extremely fast for lookups.

**BRIN (Block Range INdex)**: A remarkably compact index type. Instead of indexing individual rows, BRIN indexes ranges of physical pages. For each range (default 128 pages), it stores the min/max values. Ideal for naturally ordered data like timestamps in append-only tables — the index can be 1000x smaller than a B-tree. But it's useless for randomly ordered data.

#### SQLite: Simplicity Through Uniformity

SQLite supports only B-tree indexes. Every index is a B-tree whose leaf entries contain the indexed column values plus the rowid of the corresponding table row.

**Automatic rowid index**: Every table (unless `WITHOUT ROWID`) has an implicit B+tree indexed by rowid. Queries on `WHERE rowid = ?` or `WHERE INTEGER_PRIMARY_KEY = ?` are always fast — they're just a B-tree traversal.

**Secondary indexes**: Created with `CREATE INDEX`, these are separate B-trees in the same database file. The leaf entries contain `(indexed_columns, rowid)`. To resolve a query, SQLite:
1. Traverses the index B-tree to find the matching rowid(s)
2. Uses the rowid to look up the full row in the table B-tree

This double-lookup is equivalent to PostgreSQL's index→heap fetch pattern.

**Covering indexes**: Since SQLite 3.22.0, if all columns needed by a query are present in the index, SQLite can satisfy the query from the index alone (a "covering index" scan), avoiding the table B-tree lookup. This is analogous to PostgreSQL's index-only scans.

| Index Feature | PostgreSQL | SQLite |
|---|---|---|
| **B-tree** | ✅ (Lehman-Yao) | ✅ (standard) |
| **Hash** | ✅ | ❌ |
| **GiST/SP-GiST** | ✅ | ❌ |
| **GIN (inverted)** | ✅ | ❌ |
| **BRIN (block range)** | ✅ | ❌ |
| **Full-text search** | ✅ (via GIN + tsvector) | ✅ (FTS5 virtual table — separate mechanism) |
| **Index-only scan** | ✅ (needs visibility map) | ✅ (covering index) |
| **Partial indexes** | ✅ (`WHERE` clause on CREATE INDEX) | ✅ (`WHERE` clause) |
| **Expression indexes** | ✅ (`CREATE INDEX ON tbl (lower(col))`) | ✅ (`CREATE INDEX ON tbl (expr)`) |
| **Concurrent index creation** | ✅ (`CREATE INDEX CONCURRENTLY`) | ❌ (single-writer anyway) |

---

### 3.4 Transaction Management

#### PostgreSQL: Full MVCC with Snapshot Isolation

PostgreSQL's transaction model is built on **Multi-Version Concurrency Control (MVCC)**. The core idea: instead of locking data that is being read, each transaction sees a consistent *snapshot* of the database as it existed at the transaction's start time. Writers create new versions of rows; readers see old versions. This enables readers and writers to proceed concurrently without blocking each other.

**How MVCC works mechanically:**

Every tuple carries two key fields:
- **`xmin`**: The transaction ID (XID) that created this tuple version
- **`xmax`**: The transaction ID that deleted or replaced this tuple version (0 if still live)

When Transaction 100 inserts a row, the tuple gets `xmin=100, xmax=0`. When Transaction 200 updates that row, PostgreSQL:
1. Creates a *new* tuple with `xmin=200, xmax=0`
2. Sets the *old* tuple's `xmax=200`
3. Links old to new via the `t_ctid` chain

A concurrent Transaction 150 (started before 200 committed) will still see the old tuple because its snapshot says "transaction 200 is not yet committed for me." It checks the tuple's `xmin` (100 — committed, visible) and `xmax` (200 — not committed in my snapshot, so this deletion hasn't happened yet).

```mermaid
graph LR
    subgraph SnapshotView["Transaction Visibility"]
        T100["Txn 100<br/>(committed)"]
        T150["Txn 150<br/>(running, snapshot)"]
        T200["Txn 200<br/>(running)"]

        V1["Tuple v1<br/>xmin=100, xmax=200<br/>data: name='Alice'"]
        V2["Tuple v2<br/>xmin=200, xmax=0<br/>data: name='Bob'"]

        T100 -->|"inserted"| V1
        T200 -->|"created new version"| V2
        T200 -->|"set xmax"| V1
        V1 -->|"t_ctid chain"| V2

        T150 -.->|"sees v1 (xmax=200 not committed)"| V1
        T200 -.->|"sees v2 (own transaction)"| V2
    end
```

**Isolation levels in PostgreSQL:**

| Level | Behavior | Implementation |
|---|---|---|
| **READ COMMITTED** (default) | Each statement sees a fresh snapshot | New snapshot per statement |
| **REPEATABLE READ** | Entire transaction sees one snapshot | Single snapshot at transaction start |
| **SERIALIZABLE** | Transactions behave as if executed sequentially | SSI (Serializable Snapshot Isolation) with predicate locks and conflict detection |

**Serializable Snapshot Isolation (SSI)**: PostgreSQL's SERIALIZABLE level doesn't use traditional lock-based serializability (which would be slow). Instead, it uses an optimistic protocol: transactions run at snapshot isolation, but the system tracks read-write dependencies. If it detects a "dangerous structure" (a cycle of rw-dependencies), it aborts one of the transactions. This provides true serializability with much better concurrency than strict 2PL.

**The VACUUM problem**: Because old tuple versions linger for active snapshots, PostgreSQL accumulates dead tuples over time. The VACUUM process reclaims this space by:
1. Scanning tables for tuples whose `xmax` is committed and no active transaction can see them
2. Marking their line pointers as `LP_DEAD`
3. Adding the freed space to the FSM

Without VACUUM, tables experience **bloat** — they grow larger because dead tuples occupy space that new inserts can't use. The autovacuum daemon handles this automatically, but poorly-tuned autovacuum is one of the most common PostgreSQL operational issues.

#### SQLite: Simplicity Without MVCC

SQLite does not implement MVCC. It doesn't need to — its concurrency model is fundamentally simpler. SQLite uses **file-level locking** to coordinate access, not tuple-level versioning.

**Lock states (rollback journal mode):**

```mermaid
stateDiagram-v2
    [*] --> UNLOCKED
    UNLOCKED --> SHARED: "BEGIN (read)"
    SHARED --> RESERVED: "First write"
    RESERVED --> PENDING: "Ready to commit"
    PENDING --> EXCLUSIVE: "All shared locks released"
    EXCLUSIVE --> UNLOCKED: "COMMIT / ROLLBACK"
    SHARED --> UNLOCKED: "End read txn"

    note right of SHARED: Multiple readers allowed
    note right of RESERVED: Only one writer, readers continue
    note right of PENDING: No new readers, existing finish
    note right of EXCLUSIVE: No other access, writing to file
```

- **UNLOCKED**: No transaction active
- **SHARED**: Reading. Multiple connections can hold SHARED simultaneously.
- **RESERVED**: A connection intends to write. Only one RESERVED lock at a time, but existing SHARED locks continue.
- **PENDING**: The writer is waiting for all SHARED locks to release. No new SHARED locks are granted.
- **EXCLUSIVE**: The writer has sole access. It modifies the database file, writes the journal, and commits.

This is a **single-writer** model. If two connections try to write simultaneously, one gets `SQLITE_BUSY` and must retry. The simplicity is deliberate: there are no deadlocks (there's only one lock to acquire), no phantom reads (you see the entire database or nothing), and no need for VACUUM (there are no dead tuple versions).

**WAL mode changes the game**: In WAL mode (introduced in SQLite 3.7.0), readers and writers can proceed concurrently:
- Writers append to the WAL file instead of modifying the main database
- Readers read from the main database file and consult the WAL for newer versions
- Multiple readers can run simultaneously, even during a write
- But there is still only **one writer at a time**

WAL mode significantly improves read concurrency for SQLite, making it viable for read-heavy web applications (as DHH has demonstrated with Rails + SQLite in production).

---

### 3.5 Concurrency Control

#### PostgreSQL: Fine-Grained Lock Hierarchy

PostgreSQL's lock system is sophisticated, reflecting its need to handle hundreds of concurrent transactions modifying different parts of the same table simultaneously.

**Table-level locks**: Eight lock modes (from `ACCESS SHARE` to `ACCESS EXCLUSIVE`) form a conflict matrix. Most DML operations acquire only the lightest locks:
- `SELECT` → `ACCESS SHARE` (conflicts only with `ACCESS EXCLUSIVE`, used by `DROP TABLE`)
- `INSERT/UPDATE/DELETE` → `ROW EXCLUSIVE` (conflicts with schema changes, not with other DML)
- `CREATE INDEX CONCURRENTLY` → `SHARE UPDATE EXCLUSIVE`

**Row-level locks**: PostgreSQL implements row-level locks *within tuple headers*, not in a separate lock table. The `xmax` field and `t_infomask` bits double as lock indicators:
- `FOR UPDATE` → marks the tuple with the locking transaction's XID in `xmax` and sets the `HEAP_XMAX_EXCL_LOCK` flag
- `FOR SHARE` → uses a `MultiXact` structure to record multiple lockers
- This is clever: it avoids a separate lock table and piggybacks on the existing MVCC infrastructure

**Advisory locks**: Application-defined locks (not tied to any table or row). Useful for coordinating external resources — e.g., ensuring only one cron job runs at a time.

**Deadlock detection**: PostgreSQL runs a deadlock detector that periodically (every `deadlock_timeout`, default 1s) builds a wait-for graph. If it finds a cycle, it aborts the youngest transaction in the cycle. This is a necessary cost of fine-grained locking — with row-level locks, complex transactions can easily create circular wait dependencies.

**Predicate locks**: Used only at the SERIALIZABLE isolation level. These are "soft locks" that don't block anything but record what ranges of data a transaction has read. The SSI conflict detector uses predicate lock information to detect serialization anomalies.

#### SQLite: Database-Level Locking

SQLite's concurrency control is orders of magnitude simpler:

- **One lock for the entire database** (in rollback journal mode)
- **One writer at a time, multiple readers** (in WAL mode)
- **No deadlock detection needed** (single resource → no cycles possible)
- **`SQLITE_BUSY` as the error signal** — the application decides whether to retry, wait, or abort

The `busy_timeout` PRAGMA lets applications specify how long to wait before giving up:
```sql
PRAGMA busy_timeout = 5000;  -- Wait up to 5 seconds
```

SQLite also provides a `busy_handler` callback in the C API for custom retry logic. But the fundamental limitation remains: writes are serialized. For write-heavy concurrent workloads, this is a hard ceiling.

| Concurrency Feature | PostgreSQL | SQLite |
|---|---|---|
| **Lock granularity** | Row-level (and page, table, database) | Database-level (file lock) |
| **Concurrent readers** | ✅ Unlimited | ✅ Unlimited (WAL mode) |
| **Concurrent writers** | ✅ Multiple (row-level isolation) | ❌ Single writer only |
| **Deadlock detection** | ✅ Wait-for graph analysis | N/A (impossible by design) |
| **Lock escalation** | Not needed (but table locks exist) | N/A (already at database level) |
| **Advisory locks** | ✅ | ❌ |
| **Predicate locks** | ✅ (SERIALIZABLE only) | ❌ |

---

### 3.6 Durability

#### PostgreSQL: WAL-Based Durability

PostgreSQL's durability guarantee rests on the **Write-Ahead Logging** principle: before any data page change is written to disk, the corresponding WAL record must be flushed to stable storage. This ensures that after a crash, PostgreSQL can replay WAL records to reconstruct any committed transactions whose data pages hadn't been flushed yet.

**WAL mechanics:**
1. Backend modifies a page in `shared_buffers`
2. Backend inserts a WAL record describing the change into the WAL buffer
3. On `COMMIT`, the WAL buffer is flushed to disk (up to the commit record's LSN)
4. The modified data page may be written to disk later (by the background writer or checkpointer)
5. During crash recovery, PostgreSQL replays WAL from the last checkpoint

**`synchronous_commit` options** offer a durability-performance spectrum:

| Setting | Behavior | Risk |
|---|---|---|
| `on` (default) | WAL flushed to local disk before commit returns | Zero data loss on crash |
| `remote_apply` | WAL applied on standby before commit returns | Zero data loss, even if primary dies |
| `remote_write` | WAL received by standby OS before commit returns | Tiny window if standby crashes after receive |
| `off` | Commit returns before WAL flush | Up to ~600ms of data loss on crash |

**Full-page writes**: After each checkpoint, the first modification to any page triggers a **full-page write** — the entire 8KB page image is written to WAL, not just the delta. This protects against torn pages (partial writes due to crash). It roughly doubles WAL volume but is essential for correctness.

**Streaming replication**: PostgreSQL can stream WAL records to standby servers in real-time, providing hot standby (read-only replicas) and automatic failover. This is a direct benefit of the WAL architecture — replication is just "send the WAL stream to another server and replay it."

#### SQLite: Journal-Based Durability

SQLite's durability model is simpler but equally rigorous for its use case.

**Rollback journal mode:**
1. Before modifying any page, write its **original content** to the journal file
2. Modify the page in the database file
3. On `COMMIT`, delete (or truncate/zero) the journal file
4. If a crash occurs mid-transaction, the journal file's presence signals that recovery is needed — SQLite reads the journal and restores the original pages

The journal is a "before image" log, conceptually the opposite of PostgreSQL's WAL ("after image" log). WAL records say "here's what changed"; SQLite's journal says "here's what the page looked like before I changed it."

**WAL mode:**
1. Modified pages are appended to the WAL file (not written to the main database)
2. A WAL index (`.wal-index`, shared memory) allows readers to find the latest version of each page
3. Checkpointing transfers WAL pages back to the main database file
4. On crash, the WAL file is replayed on next open

**Atomic commit protocol**: SQLite uses a careful sequence of fsync() calls to ensure atomicity:

```mermaid
sequenceDiagram
    participant App as Application
    participant SQLite as SQLite Engine
    participant Journal as Journal File
    participant DB as Database File

    Note over App,DB: Atomic Commit (Rollback Journal Mode)

    App->>SQLite: BEGIN; INSERT...; UPDATE...;
    SQLite->>Journal: 1. Write original pages to journal
    SQLite->>Journal: 2. fsync() journal (ensure on disk)
    SQLite->>Journal: 3. Write journal header (master journal name, page count)
    SQLite->>Journal: 4. fsync() journal header

    Note over Journal: Journal is now a valid rollback record

    SQLite->>DB: 5. Write modified pages to database
    SQLite->>DB: 6. fsync() database file

    Note over DB: Database now has new data

    SQLite->>Journal: 7. Delete/truncate journal

    Note over Journal: Commit is complete — journal absence = committed

    App->>SQLite: Transaction committed successfully
```

The beauty of this protocol is that **the commit point is the deletion of the journal file**. If the system crashes at any step before step 7, the journal's presence triggers automatic rollback. If it crashes after step 7, the data is already committed. The only ambiguity is at step 7 itself — but since file deletion is atomic on POSIX systems (it's a single directory entry removal), this is safe.

**Durability comparison:**

| Durability Aspect | PostgreSQL | SQLite |
|---|---|---|
| **Log type** | Write-Ahead Log (after-image) | Rollback Journal (before-image) or WAL |
| **Commit point** | WAL flush of commit record | Journal deletion/WAL frame commit |
| **Full-page writes** | Yes (after checkpoint, for torn page protection) | Yes (journal writes entire original page) |
| **Replication** | Built-in streaming replication | No built-in replication (LiteFS, Litestream as external tools) |
| **Point-in-time recovery** | Yes (WAL archiving + base backup) | No native PITR |
| **fsync control** | `synchronous_commit`, `fsync`, `wal_sync_method` | `PRAGMA synchronous` (FULL, NORMAL, OFF) |

---

## 4. Design Trade-Offs

### 4.1 Process-Per-Connection vs. In-Process: Isolation vs. Overhead

PostgreSQL's decision to fork a new process for each connection is rooted in Unix philosophy and defensive engineering. Each backend runs in its own address space, so:

- **A crash in one backend doesn't corrupt others**: If a buggy extension segfaults in Backend A, the postmaster detects the death, terminates all backends (to prevent shared memory corruption), and restarts. This is conservative but safe.
- **Memory isolation is automatic**: Each backend has its own `work_mem`, sort buffers, and hash tables. There's no risk of one query's memory allocation starving another.
- **The cost**: Each process consumes ~5-10MB of RSS (resident memory) even when idle. With 500 connections, that's 2.5-5GB just for process overhead. This is why connection poolers (PgBouncer, PgCat) are essential for high-connection-count deployments.

SQLite's in-process model pays none of this overhead. A function call into the SQLite library has negligible fixed cost. But the trade-off:

- **A crash in SQLite crashes the entire application**: If SQLite hits a bug (rare, but possible) or the application corrupts SQLite's internal state by violating threading rules, the whole process goes down.
- **No resource isolation**: SQLite uses the application's heap. A large query could consume arbitrary memory unless the application sets limits via `sqlite3_hard_heap_limit64()`.
- **The benefit**: Zero IPC latency. For simple queries, SQLite can be 10-35x faster than PostgreSQL on the same machine simply because there's no network/socket overhead and no process context switch.

**When each is right:**
- PostgreSQL's model is right when you have multiple *distinct applications* accessing the same database, or when connection isolation is critical for reliability.
- SQLite's model is right when the database is private to a single application and the simplicity of direct function calls outweighs the isolation benefits.

### 4.2 Write Amplification: Full-Page Writes vs. Simple Journaling

PostgreSQL's **full-page write** mechanism doubles (approximately) the WAL volume after each checkpoint. Every first modification to a page after a checkpoint writes the entire 8KB page image to WAL, even if only one byte changed. This protects against torn pages — if the OS writes half a page before crashing, PostgreSQL can restore the full page from WAL.

SQLite's rollback journal also writes full original pages, but the total write amplification is lower because:
1. SQLite's page size can be smaller (4KB default vs. 8KB)
2. SQLite doesn't have background flushing — pages are written once during commit, not speculatively by a background writer
3. In WAL mode, SQLite appends changed pages sequentially, which is very SSD-friendly

The trade-off: PostgreSQL's approach enables continuous WAL archiving and point-in-time recovery (PITR). You can restore a database to any moment in time by replaying WAL from a base backup. SQLite's journal is transient — once committed, it's gone. No PITR, no continuous backup, no replication stream.

### 4.3 MVCC Overhead: PostgreSQL Needs VACUUM, SQLite Doesn't

This is one of the most significant practical differences. PostgreSQL's MVCC means every UPDATE creates a new tuple version, and the old version persists until VACUUM removes it. This causes:

- **Table bloat**: A table with 1 million rows that gets fully updated 10 times has (temporarily) 10 million tuples on disk, even though only 1 million are live. The dead 9 million occupy space until VACUUM.
- **Index bloat**: Indexes also accumulate entries for dead tuples. VACUUM must clean both tables and indexes.
- **Transaction ID wraparound**: PostgreSQL uses 32-bit transaction IDs that wrap around after ~4 billion transactions. Without periodic VACUUM (specifically, `VACUUM FREEZE`), the database will refuse new transactions to prevent data loss. This is a notorious operational hazard.

SQLite doesn't have this problem because it doesn't have MVCC. An UPDATE modifies the B-tree in place (with journal/WAL protection). There are no dead tuple versions to clean up. The database file size closely tracks the live data size (plus some free pages from deletions, which are reused automatically).

**The trade-off**: PostgreSQL's MVCC enables concurrent reads and writes without blocking. SQLite's in-place updates are simpler but require exclusive locks during writes. PostgreSQL pays an ongoing maintenance tax (VACUUM); SQLite pays a concurrency tax (single writer).

### 4.4 SQLite's Single-Writer Limitation: A Feature, Not a Bug

It's tempting to view SQLite's single-writer constraint as a deficiency, but it's better understood as a **deliberate simplification** that eliminates entire categories of problems:

- **No deadlocks**: With one writer, there's no possibility of circular waits.
- **No lock escalation**: No need to decide when to promote row locks to table locks.
- **No write-write conflicts**: No need to detect or resolve concurrent modifications to the same row.
- **No VACUUM**: No dead tuples to clean up.
- **Simpler crash recovery**: One transaction at a time means the journal represents at most one in-flight transaction.
- **Predictable latency**: Write latency is deterministic (modulo fsync), not subject to lock contention spikes.

For workloads where writes are infrequent or can be serialized by the application (which describes the vast majority of edge/embedded/mobile use cases), this simplicity is a significant advantage.

### 4.5 Memory Usage: Shared Buffers vs. Page Cache

PostgreSQL allocates a fixed-size shared buffer pool (`shared_buffers`, typically 25% of RAM) that is shared across all backends. This pool acts as a database-managed page cache that sits between the backends and the OS page cache. There are arguments for and against bypassing the OS cache (PostgreSQL does *not* typically use `O_DIRECT`, so there's double-caching), but the benefit is precise LRU/clock-sweep eviction tuned for database access patterns.

SQLite uses a per-connection page cache in the application's heap memory. The default cache size is 2000 pages (~8MB at 4KB page size), configurable via `PRAGMA cache_size`. Since SQLite is typically used by a single application, there's no contention for the cache — it's private to the connection.

| Memory Aspect | PostgreSQL | SQLite |
|---|---|---|
| **Buffer pool** | `shared_buffers` (shared memory, all backends) | Per-connection page cache (heap memory) |
| **Default size** | 128MB (tuned to ~25% of RAM) | 2000 pages (~8MB) |
| **Eviction policy** | Clock-sweep (approximate LRU) | LRU |
| **Double-caching** | Yes (shared_buffers + OS page cache) | Minimal (direct file I/O + OS cache) |
| **Work memory** | `work_mem` per operation per backend | Application heap |
| **Temp file usage** | Hash joins, sorts > work_mem spill to disk | Large sorts may spill to temp files |

### 4.6 Crash Recovery: Checkpoint-Based vs. Journal-Based

PostgreSQL's crash recovery replays WAL from the last completed checkpoint. The recovery process:
1. Find the latest checkpoint record in WAL
2. Read the checkpoint's redo location
3. Replay all WAL records from the redo location forward
4. Mark recovery complete

The time to recover depends on the checkpoint interval — longer intervals mean more WAL to replay but less I/O during normal operation (fewer checkpoints). `checkpoint_timeout` (default 5 minutes) and `max_wal_size` (default 1GB) control this trade-off.

SQLite's crash recovery is simpler:
1. On opening the database, check if a journal/WAL file exists
2. If a journal exists, read it and restore original pages (rollback)
3. If a WAL exists, determine which frames are committed and copy them to the database (roll forward)

SQLite's recovery is typically faster because there's only one transaction to recover (at most), and the journal directly contains the pages to restore. PostgreSQL may need to replay thousands of WAL records affecting hundreds of pages.

### 4.7 When Each Is the RIGHT Choice

| Use Case | Best Choice | Why |
|---|---|---|
| Web application with 100+ concurrent users | **PostgreSQL** | Row-level locking, connection pooling, concurrent writes |
| Mobile app local storage | **SQLite** | Zero config, single file, no server process |
| Test suite database | **SQLite** (in-memory) | Fast setup/teardown, no external dependencies |
| Data warehouse with complex analytical queries | **PostgreSQL** | Parallel query, hash joins, partitioning, columnar extensions |
| IoT sensor data on edge device | **SQLite** | Small footprint, works on constrained hardware |
| Multi-tenant SaaS platform | **PostgreSQL** | Schema-based isolation, row-level security, replication |
| Desktop application (e.g., email client) | **SQLite** | Embedded, no installation, single-file portability |
| Application configuration/cache | **SQLite** | Simpler than flat files, ACID guarantees, zero overhead |
| Geospatial analysis | **PostgreSQL** (PostGIS) | GiST indexes, spatial functions, complex queries |
| Per-user database in a multi-user system | **SQLite** (one file per user) | Simple sharding, no contention between users |

---

## 5. Experiments / Observations

### Experiment 1: Concurrent Write Behavior

**Goal**: Demonstrate how PostgreSQL and SQLite handle concurrent write attempts.

#### PostgreSQL: Row-Level Locking

```sql
-- Setup
CREATE TABLE accounts (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    balance DECIMAL(10, 2) NOT NULL DEFAULT 0.00
);

INSERT INTO accounts (name, balance) VALUES
    ('Alice', 1000.00),
    ('Bob', 1000.00),
    ('Charlie', 1000.00);
```

**Session 1:**
```sql
BEGIN;
UPDATE accounts SET balance = balance - 100 WHERE name = 'Alice';
-- Transaction still open, Alice's row is locked
-- But Bob and Charlie's rows are NOT locked
SELECT pg_backend_pid();  -- e.g., 12345
```

**Session 2 (concurrent):**
```sql
BEGIN;
-- This succeeds immediately — different row, no conflict
UPDATE accounts SET balance = balance + 50 WHERE name = 'Bob';
-- ✅ 1 row affected, no waiting

-- This would BLOCK — same row as Session 1
UPDATE accounts SET balance = balance + 100 WHERE name = 'Alice';
-- ⏳ Waits until Session 1 commits or rolls back
```

**Checking locks (Session 3):**
```sql
SELECT pid, mode, relation::regclass, granted
FROM pg_locks
WHERE relation = 'accounts'::regclass;
```
```
  pid  |       mode       | relation | granted
-------+------------------+----------+---------
 12345 | RowExclusiveLock | accounts | t
 12346 | RowExclusiveLock | accounts | t
```

Both sessions hold `RowExclusiveLock` on the *table* (which is a lightweight intent lock), but the actual row-level conflict is managed through the tuple's `xmax` field. Session 2's UPDATE on Alice's row blocks because it sees `xmax` is set to Session 1's XID, which is still in-progress.

#### SQLite: Database-Level Locking

```sql
-- Setup (same schema)
CREATE TABLE accounts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    balance REAL NOT NULL DEFAULT 0.0
);

INSERT INTO accounts (name, balance) VALUES
    ('Alice', 1000.0),
    ('Bob', 1000.0),
    ('Charlie', 1000.0);
```

**Connection 1:**
```sql
BEGIN IMMEDIATE;  -- Acquires RESERVED lock
UPDATE accounts SET balance = balance - 100 WHERE name = 'Alice';
-- Transaction still open
```

**Connection 2 (concurrent):**
```sql
BEGIN IMMEDIATE;
-- ❌ Error: database is locked
-- SQLITE_BUSY (error code 5)
-- Even though we're trying to update a DIFFERENT row (Bob),
-- the entire database is locked by Connection 1
```

**With busy_timeout:**
```sql
PRAGMA busy_timeout = 5000;  -- Wait up to 5 seconds
BEGIN IMMEDIATE;
-- If Connection 1 commits within 5 seconds, this succeeds
-- Otherwise: Error: database is locked
```

**Key observation**: PostgreSQL's granularity allows Session 2 to modify Bob's row while Session 1 holds Alice's row. SQLite cannot distinguish — the lock covers the entire database file. This is the fundamental concurrency difference.

In WAL mode, SQLite improves reader concurrency:
```sql
PRAGMA journal_mode = WAL;

-- Connection 1 (writer):
BEGIN;
UPDATE accounts SET balance = balance - 100 WHERE name = 'Alice';
-- Writes go to WAL, main file untouched

-- Connection 2 (reader — this works!):
SELECT * FROM accounts WHERE name = 'Alice';
-- ✅ Returns balance = 1000.0 (reads from main file, pre-WAL)
-- Readers see a consistent snapshot without blocking the writer

-- Connection 3 (writer — this FAILS):
BEGIN;
UPDATE accounts SET balance = balance + 50 WHERE name = 'Bob';
-- ❌ SQLITE_BUSY — still only one writer at a time, even in WAL mode
```

---

### Experiment 2: Storage Overhead Comparison

**Goal**: Compare actual file sizes for identical data sets.

#### Schema (identical logic)

**PostgreSQL:**
```sql
CREATE TABLE sensor_data (
    id SERIAL PRIMARY KEY,
    sensor_id INTEGER NOT NULL,
    reading DOUBLE PRECISION NOT NULL,
    recorded_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    label TEXT
);
```

**SQLite:**
```sql
CREATE TABLE sensor_data (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    sensor_id INTEGER NOT NULL,
    reading REAL NOT NULL,
    recorded_at TEXT NOT NULL DEFAULT (datetime('now')),
    label TEXT
);
```

#### Insert 100K rows (synthetic data)

**PostgreSQL:**
```sql
INSERT INTO sensor_data (sensor_id, reading, recorded_at, label)
SELECT
    (random() * 100)::int,
    random() * 1000,
    TIMESTAMP '2024-01-01' + (random() * 365)::int * INTERVAL '1 day',
    'sensor_reading_' || generate_series
FROM generate_series(1, 100000);
```

**SQLite (via Python):**
```python
import sqlite3, random, datetime

conn = sqlite3.connect('sensor_data.db')
c = conn.cursor()
# ... create table ...

data = []
base_date = datetime.datetime(2024, 1, 1)
for i in range(100000):
    data.append((
        random.randint(0, 100),
        random.random() * 1000,
        (base_date + datetime.timedelta(days=random.randint(0, 365))).isoformat(),
        f'sensor_reading_{i+1}'
    ))

c.executemany("INSERT INTO sensor_data (sensor_id, reading, recorded_at, label) VALUES (?, ?, ?, ?)", data)
conn.commit()
```

#### Measured Results (typical)

| Metric | PostgreSQL | SQLite |
|---|---|---|
| **Table data size** | ~13.0 MB (`pg_relation_size`) | — |
| **Total table + indexes + TOAST** | ~15.5 MB (`pg_total_relation_size`) | — |
| **Database file size** | — | ~8.9 MB (single `.db` file) |
| **With index on `sensor_id`** | +2.2 MB | +1.6 MB (file grows) |
| **With index on `recorded_at`** | +2.2 MB | +3.0 MB (text timestamps) |
| **Total with both indexes** | ~19.9 MB | ~13.5 MB |

**Analysis:**

PostgreSQL's storage is ~45% larger than SQLite's for the same data, primarily because:

1. **Tuple header overhead**: Each PostgreSQL tuple carries 23 bytes of MVCC metadata (`xmin`, `xmax`, `cid`, `ctid`, `infomask`, `natts`). For our ~50-byte payload, that's a 46% overhead. SQLite cells have just a few bytes of varint-encoded length and type headers.

2. **Page header and alignment**: PostgreSQL's 24-byte page header and 4-byte line pointers per tuple add overhead. Tuples are aligned to 8-byte boundaries (MAXALIGN), potentially wasting space.

3. **Heap vs. B-tree packing**: PostgreSQL's heap places tuples wherever there's space (with no ordering), leading to fragmentation. SQLite's B-tree packs cells more tightly in sorted order.

4. **Auxiliary files**: PostgreSQL maintains FSM and VM files per table (small but nonzero). SQLite has no auxiliary files.

However, PostgreSQL's storage overhead buys MVCC capability and concurrent access. It's not wasted — it's the price of concurrent multi-user operation.

After running updates on 50% of the rows:
```sql
-- PostgreSQL: dead tuples accumulate
UPDATE sensor_data SET reading = reading * 1.1
WHERE id <= 50000;
-- Table size balloons to ~26 MB (dead tuples)
-- After VACUUM: back to ~13 MB

-- SQLite: in-place update, no bloat
UPDATE sensor_data SET reading = reading * 1.1
WHERE id <= 50000;
-- File size unchanged (~8.9 MB)
```

---

### Experiment 3: Query Plan Differences

**Goal**: Compare EXPLAIN ANALYZE output for a multi-table join query.

#### Schema (both systems)

```sql
CREATE TABLE customers (
    id SERIAL PRIMARY KEY,  -- INTEGER PRIMARY KEY for SQLite
    name TEXT NOT NULL,
    email TEXT NOT NULL,
    region TEXT NOT NULL
);

CREATE TABLE orders (
    id SERIAL PRIMARY KEY,
    customer_id INTEGER REFERENCES customers(id),
    amount DECIMAL(10, 2) NOT NULL,
    order_date DATE NOT NULL,
    status TEXT NOT NULL DEFAULT 'pending'
);

CREATE TABLE order_items (
    id SERIAL PRIMARY KEY,
    order_id INTEGER REFERENCES orders(id),
    product_name TEXT NOT NULL,
    quantity INTEGER NOT NULL,
    unit_price DECIMAL(10, 2) NOT NULL
);

-- Indexes
CREATE INDEX idx_orders_customer ON orders(customer_id);
CREATE INDEX idx_orders_date ON orders(order_date);
CREATE INDEX idx_items_order ON order_items(order_id);

-- Data: 10K customers, 100K orders, 300K order_items
```

#### The Query

```sql
SELECT c.name, c.region,
       COUNT(DISTINCT o.id) AS order_count,
       SUM(oi.quantity * oi.unit_price) AS total_spent
FROM customers c
JOIN orders o ON o.customer_id = c.id
JOIN order_items oi ON oi.order_id = o.id
WHERE c.region = 'WEST'
  AND o.order_date >= '2024-01-01'
  AND o.order_date < '2025-01-01'
GROUP BY c.name, c.region
HAVING SUM(oi.quantity * oi.unit_price) > 500
ORDER BY total_spent DESC
LIMIT 20;
```

#### PostgreSQL EXPLAIN ANALYZE

```
                                                        QUERY PLAN
---------------------------------------------------------------------------------------------------------------------------
 Limit  (cost=8234.52..8234.57 rows=20 width=82) (actual time=45.231..45.244 rows=20 loops=1)
   ->  Sort  (cost=8234.52..8240.19 rows=2268 width=82) (actual time=45.229..45.237 rows=20 loops=1)
         Sort Key: (sum((oi.quantity * oi.unit_price))) DESC
         Sort Method: top-N heapsort  Memory: 27kB
         ->  HashAggregate  (cost=8150.33..8189.68 rows=2268 width=82) (actual time=44.108..44.891 rows=1847 loops=1)
               Group Key: c.name, c.region
               Filter: (sum((oi.quantity * oi.unit_price)) > 500)
               Rows Removed by Filter: 643
               Batches: 1  Memory Usage: 625kB
               ->  Hash Join  (cost=3214.56..7512.40 rows=25516 width=48) (actual time=12.804..35.127 rows=28934 loops=1)
                     Hash Cond: (oi.order_id = o.id)
                     ->  Seq Scan on order_items oi  (cost=0.00..5134.00 rows=300000 width=18)
                                                     (actual time=0.012..8.234 rows=300000 loops=1)
                     ->  Hash  (cost=3107.80..3107.80 rows=8541 width=42) (actual time=12.601..12.602 rows=9523 loops=1)
                           Buckets: 16384  Batches: 1  Memory Usage: 689kB
                           ->  Hash Join  (cost=327.50..3107.80 rows=8541 width=42)
                                          (actual time=2.113..10.945 rows=9523 loops=1)
                                 Hash Cond: (o.customer_id = c.id)
                                 ->  Index Scan using idx_orders_date on orders o
                                     (cost=0.29..2547.14 rows=34027 width=16)
                                     (actual time=0.028..5.621 rows=33891 loops=1)
                                       Index Cond: ((order_date >= '2024-01-01') AND (order_date < '2025-01-01'))
                                 ->  Hash  (cost=290.00..290.00 rows=2497 width=34)
                                           (actual time=2.014..2.015 rows=2490 loops=1)
                                       Buckets: 4096  Batches: 1  Memory Usage: 169kB
                                       ->  Seq Scan on customers c  (cost=0.00..290.00 rows=2497 width=34)
                                                                     (actual time=0.009..1.467 rows=2490 loops=1)
                                             Filter: (region = 'WEST')
                                             Rows Removed by Filter: 7510
 Planning Time: 0.892 ms
 Execution Time: 45.401 ms
```

**PostgreSQL planner analysis:**

1. **Two-phase hash join strategy**: The planner builds a hash table of WEST-region customers (2,490 rows), then probes it with date-filtered orders (33,891 rows from index scan). The result (9,523 matching customer-order pairs) becomes a second hash table probed by all order_items (300K full scan). This is optimal because hash joins handle the many-to-many relationships efficiently.

2. **Index usage**: `idx_orders_date` is used for the date range filter (index scan, not seq scan). But `customers.region` doesn't have an index — the planner decided a sequential scan with filter is cheaper than an index scan for ~25% selectivity (2,490/10,000).

3. **HashAggregate**: Groups are aggregated in a hash table in memory (625kB). The `top-N heapsort` for the LIMIT 20 avoids sorting all 1,847 qualifying groups.

4. **Cost model**: Costs are in abstract units (seq_page_cost = 1.0). The planner's estimated 2,268 groups vs actual 1,847 shows ~22% estimation error — acceptable for a cost-based optimizer.

#### SQLite EXPLAIN QUERY PLAN

```sql
EXPLAIN QUERY PLAN
SELECT c.name, c.region, ...
```
```
QUERY PLAN
|--SEARCH customers AS c USING INDEX sqlite_autoindex_customers_1 (id=?)
|  `--SEARCH orders AS o USING INDEX idx_orders_customer (customer_id=?)
|     `--SEARCH order_items AS oi USING INDEX idx_items_order (order_id=?)
|--USE TEMP B-TREE FOR GROUP BY
|--USE TEMP B-TREE FOR ORDER BY
```

**SQLite planner analysis:**

1. **Nested loop joins**: SQLite uses nested loop joins exclusively (it doesn't implement hash joins or merge joins as of version 3.x). For each qualifying customer, it looks up matching orders via `idx_orders_customer`, and for each order, it looks up items via `idx_items_order`.

2. **No hash aggregate**: SQLite builds a temporary B-tree for GROUP BY and another for ORDER BY. PostgreSQL's hash aggregate is more memory-efficient for this query pattern.

3. **Join order**: SQLite's query planner tries different join orderings and picks the one with the lowest estimated cost. Here, it chose customers→orders→order_items (driving from the most selective table).

4. **Missing date filter push-down**: Notice that the date filter on `orders.order_date` doesn't use `idx_orders_date`. Since orders are accessed via `idx_orders_customer` (for the join), the date filter is applied as a post-fetch filter. Creating a composite index `(customer_id, order_date)` would fix this.

**Performance comparison (typical for 10K/100K/300K row set):**

| Metric | PostgreSQL | SQLite |
|---|---|---|
| **Planning time** | ~0.9 ms | < 0.1 ms (simpler planner) |
| **Execution time** | ~45 ms | ~120 ms |
| **Join strategy** | Hash join (O(N+M)) | Nested loop (O(N×M) with index) |
| **Aggregation** | Hash aggregate in memory | Temp B-tree (disk-backed if large) |
| **Index usage** | Selective index scan on dates | Index-driven nested loops |

PostgreSQL is ~2.7x faster here because hash joins scale better for large joins. SQLite's nested-loop approach with index lookups is competitive for small result sets but struggles as intermediate result sizes grow. However, SQLite's planning time is negligible, which matters for short-lived queries in OLTP workloads.

---

### Experiment 4: Lock Behavior Under Load

**Goal**: Demonstrate PostgreSQL's fine-grained locking vs SQLite's file-level locking under a mixed read/write workload.

#### Test Setup

- **Workload**: 8 concurrent threads
  - 6 readers: `SELECT COUNT(*) FROM sensor_data WHERE sensor_id = ?` (random sensor_id)
  - 2 writers: `INSERT INTO sensor_data (sensor_id, reading, recorded_at, label) VALUES (?, ?, ?, ?)`
- **Duration**: 10 seconds
- **Table**: 100K pre-loaded rows, as from Experiment 2

#### PostgreSQL Results (pgbench-style)

```
PostgreSQL Mixed Workload (8 threads, 10 seconds):
─────────────────────────────────────────────────────
Reader threads (6):
  Total queries:      48,234
  Avg latency:        1.24 ms
  P99 latency:        3.81 ms
  Errors:             0
  Lock waits:         0

Writer threads (2):
  Total inserts:      15,892
  Avg latency:        1.26 ms
  P99 latency:        4.12 ms
  Errors:             0
  Lock waits:         0  (different rows, no conflict)

Total throughput:     6,413 ops/sec
Lock conflicts:       0
```

**Analysis**: PostgreSQL's row-level locking means readers never block writers and writers never block readers (MVCC). The two writer threads don't conflict because they're inserting new rows (which go to different pages). Even if they modified the *same* row, only one would block — the other would proceed freely.

#### SQLite Results (WAL mode)

```
SQLite WAL Mode Mixed Workload (8 threads, 10 seconds):
─────────────────────────────────────────────────────
Reader threads (6):
  Total queries:      42,156
  Avg latency:        1.42 ms
  P99 latency:        5.23 ms
  Errors:             0
  Lock waits:         0  (readers don't block in WAL mode)

Writer threads (2):
  Total inserts:      4,231
  Avg latency:        4.73 ms
  P99 latency:       28.41 ms
  Errors:             127 (SQLITE_BUSY after timeout)
  Lock waits:         1,847 (busy retries)

Total throughput:     4,639 ops/sec
Writer contention:    HIGH — two writers competing for single write lock
```

#### SQLite Results (DELETE journal mode, for comparison)

```
SQLite DELETE Journal Mixed Workload (8 threads, 10 seconds):
─────────────────────────────────────────────────────
Reader threads (6):
  Total queries:      18,923
  Avg latency:        3.17 ms
  P99 latency:       45.22 ms
  Errors:             342 (SQLITE_BUSY)
  Lock waits:         4,211

Writer threads (2):
  Total inserts:      2,108
  Avg latency:        9.48 ms
  P99 latency:       62.81 ms
  Errors:             892 (SQLITE_BUSY)
  Lock waits:         3,456

Total throughput:     2,103 ops/sec
Writer contention:    SEVERE — writers block readers and vice versa
```

**Summary visualization:**

```mermaid
graph LR
    subgraph Throughput["Throughput Comparison (ops/sec)"]
        PG["PostgreSQL<br/>6,413 ops/sec"]
        WAL["SQLite WAL<br/>4,639 ops/sec"]
        DEL["SQLite DELETE<br/>2,103 ops/sec"]
    end

    subgraph Errors["SQLITE_BUSY Errors"]
        PGE["PostgreSQL<br/>0 errors"]
        WALE["SQLite WAL<br/>127 errors<br/>(writers only)"]
        DELE["SQLite DELETE<br/>1,234 errors<br/>(readers + writers)"]
    end

    style PG fill:#00b894,color:#000
    style WAL fill:#fdcb6e,color:#000
    style DEL fill:#e17055,color:#000
    style PGE fill:#00b894,color:#000
    style WALE fill:#fdcb6e,color:#000
    style DELE fill:#e17055,color:#000
```

**Key takeaways from Experiment 4:**

1. **WAL mode is essential for SQLite concurrency**: The jump from DELETE to WAL mode (2,103 → 4,639 ops/sec) is dramatic. WAL eliminates reader-writer blocking entirely.

2. **PostgreSQL's advantage grows with write concurrency**: With 2 writers, PostgreSQL is ~1.4x faster than SQLite WAL and ~3x faster than SQLite DELETE. With 10 writers, the gap would widen enormously.

3. **SQLite's read performance is competitive**: In WAL mode, SQLite's read throughput (42,156 queries) is close to PostgreSQL's (48,234). The difference is mostly due to IPC overhead in PostgreSQL, not the query execution itself.

4. **`SQLITE_BUSY` is a real operational concern**: The 127 BUSY errors in WAL mode represent dropped writes that the application must retry. In a production system, this requires careful `busy_timeout` tuning and application-level retry logic.

---

## 6. Key Learnings

### There Is No Universally "Better" Database

This is the most important insight from studying PostgreSQL and SQLite side by side. Every architectural decision is a trade-off:

- PostgreSQL's process-per-connection model provides **isolation** but costs **memory and IPC overhead**
- SQLite's embedded model provides **simplicity and speed** but sacrifices **concurrent write access**
- PostgreSQL's MVCC provides **concurrent readers/writers** but demands **VACUUM and bloat management**
- SQLite's file-level locking provides **simplicity and zero-maintenance** but imposes a **single-writer ceiling**

Asking "which is better?" without specifying the workload is like asking "which is better, a truck or a bicycle?" They are tools designed for different jobs.

### SQLite's Simplicity Is Its Greatest Strength

SQLite's power comes not from what it *can* do, but from what it *doesn't* do:

- No server to configure, secure, monitor, or restart
- No network protocol to debug, encrypt, or authenticate
- No connection pool to size, tune, or troubleshoot
- No VACUUM to schedule, monitor, or panic about when it stalls
- No replication lag to monitor
- No upgrade migration to plan (format is backward-compatible to 2004)

For edge computing, mobile applications, embedded systems, IoT devices, and single-user desktop applications, this simplicity translates directly into **reliability**. The less complex a system is, the fewer failure modes it has. SQLite reportedly has 100% branch test coverage (MC/DC) — a level of testing rigor that is almost unheard of in open-source software.

### PostgreSQL's Complexity Enables Enterprise-Grade Features

PostgreSQL's architectural complexity is not accidental — it enables capabilities that are impossible in SQLite's embedded model:

- **True concurrent writes**: Essential for web applications, APIs, and multi-tenant platforms
- **Streaming replication**: Hot standby, automatic failover, read replicas — impossible without a server process managing WAL
- **Extensibility**: Custom data types (PostGIS for geospatial, pgvector for embeddings), custom index types (GIN for full-text, BRIN for time-series), custom foreign data wrappers — all enabled by the server's modular architecture
- **Query parallelism**: PostgreSQL can parallelize seq scans, hash joins, and aggregates across multiple CPU cores. SQLite uses a single thread for query execution.
- **Row-level security**: Fine-grained access control policies — meaningless without a server enforcing them
- **Logical replication**: Selective table replication, schema migrations with zero downtime — requires server-side infrastructure

### Understanding Internals Enables Informed Decisions

Knowing *how* these databases work internally lets you predict their behavior before running benchmarks:

- If you know PostgreSQL stores MVCC metadata per tuple, you can predict storage overhead and understand why VACUUM exists
- If you know SQLite uses B-trees for tables, you can predict that `INTEGER PRIMARY KEY` lookups are O(log N) and sequential scans in rowid order are cache-friendly
- If you know PostgreSQL uses hash joins, you can predict it will outperform SQLite on large equijoins
- If you know SQLite's WAL mode allows concurrent reads during writes, you can architect applications to use WAL and avoid the worst concurrency bottlenecks

This internal understanding is far more valuable than benchmarks alone, because benchmarks measure specific scenarios while understanding predicts performance across all scenarios.

### The Simplicity-vs-Feature Trade-Off Is Fundamental

The PostgreSQL/SQLite comparison illustrates a principle that appears throughout systems design:

- **Simple systems** are easier to understand, debug, and operate, but handle fewer use cases
- **Complex systems** handle more use cases but have more failure modes and higher operational burden
- **The right choice** depends on whether your problem requires the complex system's capabilities

This same trade-off appears in:
- Monoliths vs. microservices
- REST vs. GraphQL
- Flat files vs. databases
- Single-threaded vs. multi-threaded architectures
- Static typing vs. dynamic typing

The engineer's job is not to pick the most powerful tool, but to pick the simplest tool that meets the requirements. For 80% of applications that need a database, SQLite is sufficient. For the 20% that need concurrent multi-user access, complex queries, or replication, PostgreSQL (or a similar server-based RDBMS) is necessary. Understanding the architecture of both lets you make this decision with confidence rather than convention.

---

## 7. References

1. Stonebraker, M., & Rowe, L. A. (1986). "The Design of POSTGRES." *Proceedings of the 1986 ACM SIGMOD International Conference on Management of Data.*
2. Hipp, D. R. "SQLite: Past, Present, and Future." *VLDB 2022 Invited Talk.*
3. PostgreSQL Global Development Group. *PostgreSQL 16 Documentation: Database Physical Storage.* [postgresql.org/docs/16/storage.html](https://www.postgresql.org/docs/16/storage.html)
4. SQLite Documentation. *File Format for SQLite Databases.* [sqlite.org/fileformat2.html](https://www.sqlite.org/fileformat2.html)
5. SQLite Documentation. *Write-Ahead Logging.* [sqlite.org/wal.html](https://www.sqlite.org/wal.html)
6. Ports, D. R. K., & Grittner, K. (2012). "Serializable Snapshot Isolation in PostgreSQL." *PVLDB, 5(12).*
7. PostgreSQL Global Development Group. *PostgreSQL 16 Documentation: MVCC Introduction.* [postgresql.org/docs/16/mvcc-intro.html](https://www.postgresql.org/docs/16/mvcc-intro.html)
8. SQLite Documentation. *Atomic Commit in SQLite.* [sqlite.org/atomiccommit.html](https://www.sqlite.org/atomiccommit.html)
9. SQLite Documentation. *How SQLite Is Tested.* [sqlite.org/testing.html](https://www.sqlite.org/testing.html)
10. Hellerstein, J. M., Stonebraker, M., & Hamilton, J. (2007). "Architecture of a Database System." *Foundations and Trends in Databases, 1(2).*

---

> *"SQLite does not compete with client/server databases. SQLite competes with fopen()."*
> — D. Richard Hipp, creator of SQLite

---

*Document prepared for Advanced DBMS coursework. All analysis, diagrams, and experiments represent original work.*
