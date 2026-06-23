# PostgreSQL vs SQLite Architecture Comparison

## 1. Problem Background

PostgreSQL and SQLite solve different database problems:

- **SQLite** (1999): Embedded database for applications needing local data storage without external dependencies. Targets mobile, desktop, and embedded systems where deployment overhead is critical.
- **PostgreSQL** (1986): Shared database server for organizations needing multi-user ACID compliance with high concurrency. Targets backend infrastructure where multiple applications and users access the same database.

The comparison reveals fundamental architectural choices made when optimizing for different constraints.

## 2. Architecture Overview

### PostgreSQL: Multi-Process Client-Server

```
Applications (Network Clients)
         ↓ TCP/IP
Postmaster (Main Server Process)
         ↓
Backend Processes (1 per connection)
         ↓
Shared Buffer Pool + Heap Storage
         ↓
Disk (Multiple files: tables, indexes, WAL)
```

Key characteristics:
- Client connects to server over network
- Each connection spawns independent backend process
- Shared buffer pool for memory efficiency
- Separate WAL for crash recovery

### SQLite: Embedded Single-Process

```
Application Process
         ↓
SQLite Library (In-process)
         ↓
Page Cache (VFS managed)
         ↓
Disk (Single .db file)
```

Key characteristics:
- No separate server; library linked into application
- Single-threaded execution per application instance
- Single .db file contains all data
- File-level locking for coordination between processes

## 3. Internal Design

### Storage Differences

| Aspect | PostgreSQL | SQLite |
|--------|-----------|--------|
| **Table Storage** | Unordered heap with MVCC versions | B-tree indexed by primary key |
| **Index Organization** | Separate B-tree files pointing to heap | B-tree files; integer PK embedded |
| **Data Layout** | Multiple versions per tuple on page | Single version per row |
| **File Organization** | Separate files per table/index | Single .db file with all data |

### Concurrency Control

**PostgreSQL**:
- Row-level locks for UPDATE/DELETE
- MVCC for readers (no locks needed)
- Snapshot isolation: transactions see consistent database state

**SQLite**:
- Database-level locks (SHARED, RESERVED, EXCLUSIVE)
- Write-Ahead Logging enables readers during WAL writes
- Transaction isolation prevents dirty reads but may serialize writes

### Recovery

**PostgreSQL**:
- WAL-based recovery with point-in-time restore capability
- Archiving supports replication and backups
- VACUUM process reclaims space from old MVCC versions

**SQLite**:
- Rollback journal maintains transaction atomicity
- Simpler recovery (one file to manage)
- No background cleanup needed (in-place updates)

## 4. Design Trade-Offs

### PostgreSQL Trade-Offs

**Advantages**:
- High concurrency: Row-level locks + MVCC enable parallel writes to different rows
- Advanced features: Replication, point-in-time recovery, complex queries
- Scalability: Shared resource model supports thousands of clients

**Limitations**:
- Process-per-connection uses ~5-10 MB per client (memory overhead)
- MVCC requires VACUUM to reclaim storage (operational complexity)
- Multi-process coordination adds complexity

### SQLite Trade-Offs

**Advantages**:
- Zero deployment: Embedded library, no server process
- Simplicity: Single file, no background processes
- No overhead: Direct library calls, no network latency

**Limitations**:
- Write concurrency: Database-level locking limits to one writer at a time
- Scalability: File-level coordination doesn't work well across machines
- MVCC not supported: Long queries may block recent transactions

## 5. Experiments & Observations

### PostgreSQL EXPLAIN ANALYZE Example

```sql
CREATE TABLE sales (id INT, store_id INT, amount DECIMAL);
CREATE INDEX idx_store ON sales(store_id);

EXPLAIN ANALYZE
SELECT store_id, SUM(amount) FROM sales WHERE store_id = 5 GROUP BY store_id;

Output:
Index Scan using idx_store on sales (cost=0.13..15.23 rows=100)
  Index Cond: (store_id = 5)
  Buffers: shared hit=1 read=2
  Planning Time: 0.042 ms
  Execution Time: 0.891 ms
```

Observations:
- Planner chose index scan (efficient for specific store)
- MVCC visibility check happens per row
- Buffer manager served 1 page from cache, read 2 from disk

### SQLite B-Tree Efficiency

SQLite's B-tree table organization makes range queries efficient:

```sql
-- SQLite (range query efficient due to B-tree sorting)
SELECT * FROM temperature_readings 
WHERE timestamp BETWEEN '2026-01-01' AND '2026-01-31';

-- B-tree provides direct access to date range, no full table scan
```

PostgreSQL would require index on timestamp or full heap scan.

## 6. Key Learnings

1. **Architecture follows deployment context**: PostgreSQL's multi-process complexity is justified for shared-resource servers. SQLite's simplicity is justified for embedded applications.

2. **MVCC enables concurrency at a cost**: PostgreSQL's tuple versioning allows readers and writers to proceed without blocking, but requires VACUUM and adds CPU overhead. SQLite avoids this complexity by accepting write serialization.

3. **Storage organization reflects query patterns**: PostgreSQL's unordered heap with separate indexes assumes diverse query patterns. SQLite's B-tree tables assume queries benefit from sorted primary key access.

4. **Concurrency granularity matters**: Row-level locks scale to many concurrent writers. Database-level locks work for read-heavy or single-writer workloads.

5. **Choose the right database for your problem**: Neither is universally "better"—the choice depends on deployment model (embedded vs. server), concurrency requirements (multiple writers vs. single writer), and operational complexity tolerance.

