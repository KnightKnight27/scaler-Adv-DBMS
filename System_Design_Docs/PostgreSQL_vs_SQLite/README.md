# PostgreSQL vs SQLite — Architecture Comparison

## 1. Problem Background

PostgreSQL and SQLite are both relational databases, but honestly, they couldn't be more different in what they're trying to do.

**PostgreSQL** started as the POSTGRES project at UC Berkeley back in the mid-80s. The whole idea was to build something that could handle real enterprise workloads — lots of users hitting the database at the same time, complex queries, strict ACID guarantees, the works. It evolved over decades and eventually became one of the most feature-rich open-source databases out there.

**SQLite** has a completely different origin story. D. Richard Hipp built it around 2000 while working on a project for the US Navy. The pain point was simple: he needed a database that didn't require installing and configuring a whole server just to store some local data. No admin, no setup, just link a library and go. That's literally the philosophy — "zero configuration." It's designed for scenarios where you just need reliable local storage without the overhead of a full database server.

What's interesting is that both are extremely successful, but for entirely different reasons. PostgreSQL powers huge web platforms and enterprise systems, while SQLite is probably the most deployed piece of software in the world (it's in every smartphone, browser, and tons of embedded devices).

## 2. Architecture Overview

### PostgreSQL — Client-Server Model
PostgreSQL follows the traditional client-server pattern with multiple processes:
- There's a main **Postmaster** process that listens for connections
- Every time a client connects, Postmaster forks a new **backend process** dedicated to that client
- All these backend processes share a chunk of **shared memory** (this is where the buffer pool, lock tables, etc. live)
- Then there are several **background processes** doing housekeeping — the background writer flushing dirty pages, the WAL writer, the autovacuum daemon, checkpointer, etc.

### SQLite — Embedded / In-Process
SQLite doesn't have a server at all. It's a C library that you link into your application.
- The database engine runs inside your application's own process
- It reads and writes directly to a single file on disk
- There's a **VFS (Virtual File System)** abstraction that lets it work across different OSes without changing the core logic

Here's a rough picture of how they differ:

```
  PostgreSQL                              SQLite
  ──────────                              ──────
  Client ──► Postmaster                   Application
               │                              │
         ┌─────┴──────┐                  SQLite Library
    Backend 1    Backend 2                    │
         │           │                   Single .db file
    Shared Memory Pool
         │
    Data Files on Disk
```

The difference is pretty stark. PostgreSQL has all this infrastructure for handling multiple clients safely, while SQLite is just a function call away from your application code.

## 3. Internal Design

### Storage & File Organization
- **PostgreSQL** spreads things across multiple files and directories. Each database gets its own subdirectory, and each table/index is stored as a separate file (segmented into 1GB chunks if it gets large). It uses a heap storage model — rows are just appended wherever there's space, and indexes store pointers (called TIDs) to locate them.
- **SQLite** keeps everything in one file. The entire database — schema, tables, indexes, all of it — lives in a single cross-platform file. This is what makes it so easy to copy, backup, or transfer. You literally just copy one file.

### Page Layout
- **PostgreSQL** pages are 8KB by default. Each page has a header, then an array of "line pointers" that grow from the front, and actual tuple data growing from the back of the page. It's a heap structure, so rows aren't necessarily ordered.
- **SQLite** organizes data using B-Trees. Tables are stored as B-Trees (with rows in the leaf nodes), and indexes are B+Trees. Default page size is 4096 bytes. Since row data lives directly in the B-Tree leaves (basically an Index-Organized Table), primary key lookups are really fast — no extra hop needed.

### Indexes
- **PostgreSQL** has a bunch of index types — B-Tree (the default), Hash, GiST, GIN, BRIN, etc. All of them essentially store pointers (TIDs) back to the heap where the actual row lives.
- **SQLite** mainly uses B-Trees. A secondary index stores the indexed columns along with the ROWID, and then you use that ROWID to look up the full row in the main table B-Tree.

### Concurrency & Transactions
This is where the two really diverge.

- **PostgreSQL** uses **MVCC** (Multi-Version Concurrency Control). When you update a row, it doesn't overwrite the old data — it creates a new version. Readers see the version that was valid at the start of their transaction. This means readers and writers don't block each other, which is great for concurrency. The downside? Old row versions pile up, so you need `VACUUM` to clean them up periodically.
- **SQLite** historically used a simple database-level lock — only one writer at a time, and writers block readers. With **WAL mode** (which most people use now), things got better: multiple readers can work simultaneously with one writer. But you still can't have multiple concurrent writers. For a single-user embedded database, this is usually fine.

### Durability
- **PostgreSQL** uses WAL (Write-Ahead Logging). Changes go to the WAL first, data files get updated later. If there's a crash, the WAL is replayed to recover.
- **SQLite** supports two modes: **rollback journals** (the default, where original page content is saved before modification) and **WAL mode** (similar idea to PostgreSQL's WAL, and generally faster for writes).

## 4. Design Trade-Offs

### PostgreSQL
**What's good:**
- Handles thousands of concurrent connections with MVCC — reads and writes don't interfere with each other
- Tons of advanced features: JSONB, full-text search, PostGIS, custom types, window functions, CTEs, etc.
- Strong ecosystem with extensions and tooling

**What's not so great:**
- Resource-heavy. Running a full PostgreSQL server needs memory for shared buffers, WAL, background processes, connection handling, etc.
- VACUUM can become a real headache on write-heavy tables. If autovacuum doesn't keep up, table bloat becomes a problem
- Definitely not something you'd embed in a mobile app

### SQLite
**What's good:**
- Dead simple to use. No server, no configuration, no admin
- Tiny footprint — the library is about 700KB
- Incredibly well-tested (they have billions of test cases, which is kind of insane)
- The single-file format makes deployment and backups trivial

**What's not so great:**
- Write concurrency is basically non-existent. One writer at a time, period.
- No built-in network access — if you need remote access, you have to build that yourself
- Fewer data types and less sophisticated query optimization compared to PostgreSQL

### Answering the Key Questions

**Why does SQLite work well for mobile apps?**
Mobile apps are single-user. You don't have 50 people writing to the database at the same time. What you need is local storage that works offline, uses minimal battery and memory, and doesn't require a background server process. SQLite nails all of this. Plus the single-file approach means app backups and migrations are straightforward.

**Why is PostgreSQL preferred for large multi-user systems?**
Because it can actually handle concurrent writes without everything grinding to a halt. MVCC lets multiple users read and write simultaneously. The process isolation model (one backend per connection) means a crash in one session doesn't take down the whole server. And the query optimizer is sophisticated enough to handle complex analytical queries over millions of rows.

**What architectural decisions cause these differences?**
It really comes down to the core decision: client-server vs. embedded library. Once you commit to client-server, you need IPC, shared memory management, connection pooling, background workers — all of which enable scalability but add complexity. If you go the embedded route, you get simplicity and directness but give up on multi-user concurrency. Everything else follows from this one fundamental choice.

## 5. Experiments / Observations

I didn't run a formal benchmark for this, but here's what I'd expect based on the architecture (and what I've read from various benchmark reports):

**Scenario:** 100 threads all trying to INSERT at the same time.

- **PostgreSQL:** Should handle this without breaking a sweat. Each connection gets its own backend, MVCC ensures inserts don't step on each other's toes, and throughput should scale roughly with available CPU cores.
- **SQLite (rollback journal mode):** This would be a disaster. Most threads would get `SQLITE_BUSY` errors immediately because the entire database file gets locked for writes. Effective concurrency = 1.
- **SQLite (WAL mode):** Better than rollback mode — reads can proceed concurrently — but writes are still serialized. The 100 writers would essentially queue up, and throughput would be a fraction of what PostgreSQL achieves.

This scenario basically illustrates *why* these two databases exist for different use cases. If your workload involves lots of concurrent writes, SQLite is the wrong tool.

## 6. Key Learnings

- **There's no "better" database — only "better for your use case."** PostgreSQL and SQLite are both excellent, just for completely different things. Asking which is better is like asking whether a truck or a bicycle is a better vehicle. Depends on what you're doing.
- **The process model is a bigger deal than I initially thought.** Client-server vs. embedded isn't just a deployment detail — it fundamentally shapes everything about the database's capabilities, from concurrency to fault isolation to resource usage.
- **Concurrency and simplicity are genuinely at odds.** PostgreSQL invests enormous architectural complexity (MVCC, shared memory, process management, VACUUM) to achieve high concurrency. SQLite gives up concurrency to stay simple. Neither is wrong — they're just optimizing for different things.
- **The storage engine design has huge performance implications.** PostgreSQL's heap model means all indexes are "equal" (they all point to the heap), but updates can be expensive. SQLite's B-Tree/IOT model gives blazing primary key lookups but has different trade-offs for secondary access.
