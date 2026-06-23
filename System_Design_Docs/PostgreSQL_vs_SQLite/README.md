# PostgreSQL vs SQLite Architecture Comparison

## 1. Problem Background

When looking at database architecture, there is no single "best" design; there are only designs optimized for specific constraints. PostgreSQL and SQLite perfectly illustrate this by taking wildly different approaches to solve completely different problems. 

PostgreSQL is built to be a robust, shared database server handling thousands of concurrent users, complex queries, and massive datasets. It assumes it has dedicated server resources, network access, and background maintenance capabilities.

SQLite, on the other hand, is built to be an embedded database. It is designed to run entirely within the same process as the application itself, requiring zero configuration, zero network overhead, and zero dedicated background processes. It treats the database simply as a highly structured file on disk.

Comparing the two highlights how the deployment context completely dictates a system's internal architecture.

## 2. Architecture Overview

```text
[ PostgreSQL: Client-Server ]       [ SQLite: Embedded ]

  +-------+       +-------+             +-------------+
  | App 1 |       | App 2 |             | App Process |
  +-------+       +-------+             +-------------+
      | TCP           | TCP                    |
      v               v                        v
  +-----------------------+             +-------------+
  |      Postmaster       |             | SQLite Lib  |
  +-----------------------+             +-------------+
      |               |                        |
      v               v                        v
  +---------+   +---------+             +-------------+
  | Backend |   | Backend |             | Page Cache  |
  +---------+   +---------+             +-------------+
      |               |                        |
      +-------+-------+                        |
              |                                |
              v                                v
       +-------------+                  +-------------+
       | Shared Buff |                  | Single .db  |
       +-------------+                  |    File     |
              |                         +-------------+
              v
       +-------------+
       | DB Files    |
       +-------------+
```

PostgreSQL uses a Multi-Process Client-Server architecture. When an application wants data, it sends a request over a network connection (TCP/IP or Unix sockets). A master server process (the Postmaster) receives this connection and spawns a completely independent backend process dedicated solely to that client. All of these separate backend processes communicate with a shared memory segment (the buffer pool) and coordinate their reads and writes to the underlying data files.

SQLite uses an Embedded Single-Process architecture. There is no server. There are no network calls. The SQLite engine is simply a C library linked directly into the application's code. When the application runs a query, it's just making a local function call. The library manages its own internal page cache and interacts directly with a single `.db` file on the filesystem. If multiple applications want to access the same database, they do so by literally pointing to the same file and using OS-level file locks to coordinate.

## 3. Internal Design

### Storage and File Organization
PostgreSQL splits its data into many different files. Every table and every index gets its own file (or multiple files, if they grow large enough). The table data is stored as an unordered heap, and separate B-tree files point into that heap.
SQLite takes a radically simpler approach. Everything—tables, indexes, metadata, schema—is packaged neatly into one single `.db` file. Furthermore, SQLite doesn't use an unordered heap; its tables are inherently stored as B-trees sorted by the primary key, meaning the primary key is physically embedded in the tree structure.

### Concurrency Control
Because PostgreSQL expects hundreds of users modifying data simultaneously, it uses Row-Level Locking combined with Multi-Version Concurrency Control (MVCC). If User A is updating row 5, User B can comfortably update row 6 without waiting. Even better, User C can read row 5 while User A is updating it, because PostgreSQL will just show User C the old version of the row.
SQLite operates under much tighter constraints. It generally uses Database-Level Locks. While it has Write-Ahead Logging (WAL) which allows concurrent readers alongside a single writer, it fundamentally cannot support multiple parallel writers. If one process is writing to the database file, any other process that wants to write simply has to wait in line.

### Recovery and Maintenance
PostgreSQL's use of MVCC means old versions of rows pile up. It requires a dedicated background process (the Autovacuum daemon) to constantly clean up this dead space. Its recovery is based on a complex Write-Ahead Log that can be archived to restore the database to any exact second in the past.
SQLite relies on a simpler Rollback Journal (or a local WAL). Because it updates data in-place, there are no dead row versions to clean up, meaning no background vacuum processes are necessary. Recovery simply means ensuring the single file is in a consistent state upon application startup.

## 4. Design Trade-Offs

PostgreSQL's multi-process, MVCC architecture is a massive trade-off of memory and complexity in exchange for extreme scalability. Giving every connection its own process means high memory overhead (often 5-10 MB per idle connection). The MVCC engine requires background vacuuming, which demands operational tuning. However, the payoff is that thousands of clients can read and write to the same tables concurrently without bottlenecking each other. It's a perfect trade-off for a cloud backend.

SQLite's single-file, library-based architecture trades concurrency for absolute simplicity. By locking the whole database (or at least serializing all writes), SQLite completely eliminates the complex deadlock detection, row-level tracking, and shared-memory management found in PostgreSQL. The payoff is zero deployment friction and incredibly fast read performance (since there's no network latency or process context switching). However, this makes it entirely unsuitable for heavy multi-writer workloads like a busy web application backend.

## 5. Experiments / Observations

To observe PostgreSQL's complex planning and execution, we can run `EXPLAIN ANALYZE`:

```sql
CREATE TABLE sales (id INT, store_id INT, amount DECIMAL);
CREATE INDEX idx_store ON sales(store_id);

EXPLAIN ANALYZE
SELECT store_id, SUM(amount) FROM sales WHERE store_id = 5 GROUP BY store_id;
```
Looking at the output, we can observe the system actively making decisions. We see the planner calculating costs, deciding to use the `idx_store` index, and we can observe exactly how many memory buffers it had to read to resolve the MVCC visibility of the rows. 

For SQLite, we can observe the efficiency of its storage model by running a range query:

```sql
SELECT * FROM temperature_readings 
WHERE timestamp BETWEEN '2026-01-01' AND '2026-01-31';
```
Because SQLite tables are physically organized as B-trees by their primary key, if `timestamp` is the primary key, this query requires almost no planning. SQLite just jumps to the start date in the tree and sequentially walks forward. To achieve this exact same physical layout in PostgreSQL, we would have to use a special `CLUSTER` command to forcefully rewrite the heap table to match the index order.

## 6. Key Learnings

Comparing PostgreSQL and SQLite is the best way to understand that architectural choices are driven by deployment context. The complexity of PostgreSQL's shared-memory buffer pool, MVCC, and autovacuum isn't there because the engineers wanted to overcomplicate things; it's there because it's the only way to mathematically allow thousands of concurrent users to edit a shared resource fairly.

Conversely, SQLite shows that if you drop the requirement for concurrent writes and network access, you can build a database that is staggeringly fast and completely invisible to the end user. I've learned that you shouldn't just ask "which database is faster?" or "which is better?". You have to ask, "does my application need concurrent writers across a network, or does it need zero-configuration local persistence?" The architecture of the database must match the architecture of the application.
