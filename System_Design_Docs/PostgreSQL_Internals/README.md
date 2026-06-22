# Topic 2: PostgreSQL Internal Architecture
## 1. Problem Background
PostgreSQL was engineered with a primary focus on extensibility, standards compliance, and data integrity. As a highly concurrent enterprise-grade relational database, it was designed to resolve several complex system constraints:

* **Memory Limits:** Managing datasets that far exceed physical RAM bounds without stalling queries on slow, random disk I/O.

* **Lock Contention:** Allowing high volumes of concurrent reads and writes simultaneously without permitting writing clients to block readers or vice versa.

* **System Resilience:** Providing strict ACID durability guarantees so that transactions are completely safe even if the host machine experiences a sudden power loss or kernel panic mid-operation.

## 2. Architecture Overview
PostgreSQL isolates query processing from the underlying physical storage via a series of highly synchronized layers.

```bash
             [ Client SQL Query ] 
                       │
                       ▼
             [ Parser & Optimizer ] ──( Evaluates pg_statistic )
                       │
                       ▼
             [ Execution Engine ]
                       │
                       ▼
   ┌───────────────────┴───────────────────┐
   ▼                                       ▼
[ Buffer Manager ]                  [ WAL Writer ]
   │                                       │
   ▼                                       ▼
[ Shared Buffers (RAM) ]            [ Write-Ahead Log (Disk) ]
   │
   ▼
[ Table Heap Files (Disk) ]
```

When a query executes, it interacts directly with the Buffer Manager. The buffer manager abstraction operates on fixed-size data blocks, ensuring that data updates modify a shared memory space called Shared Buffers rather than touching disk directly. Simultaneously, modifications are streamed sequentially to the Write-Ahead Log (WAL) to guarantee persistence before changes are lazily written to table heap files.

## 3. Internal Design
### Buffer Manager (```src/backend/storage/buffer/```)
The buffer manager maps requested logical block numbers to physical page files on disk. PostgreSQL uses a Clock Sweep (Second Chance) replacement algorithm to manage page pinning and eviction in the shared memory pool:

* The buffer pool is treated as a circular array of buffer frames.

* Each frame holds a single page and maintains an atomic ```usage_count``` (up to a max of 5) and a ```pin_count``` (tracking active execution threads reading the page).

* A conceptual "clock hand" sweeps the array. If it encounters an unpinned frame with a ```usage_count > 0```, it decrements the count and moves onward. If it encounters a frame with ```usage_count == 0``` and no active pins, that page is instantly evicted to free memory for a incoming disk read.

### B-Tree Implementation (```nbtree```)
PostgreSQL implements Lehman & Yao’s high-concurrency B-Tree algorithm. Unlike classical textbook B-Trees, pages at the same tree level contain an explicit pointer to their immediate right neighbor (right-links). When a concurrent insert splits a page, a reader can follow the right-link to locate a key that moved during the split without requiring heavy, top-down tree locking from the root node.

### MVCC (Multi-Version Concurrency Control)
Concurrency is achieved through row versioning. PostgreSQL never updates a physical tuple in-place. Instead, tuples are maintained via structural headers inside the table heap:

* **xmin:** The Transaction ID (TxID) that inserted the row version.

* **xmax:** The TxID that deleted or updated the row version (set to 0 for active tuples).
An ```UPDATE``` statement writes a copy of the tuple into a new slot in the heap, setting ```xmax``` on the old tuple and ```xmin``` on the new tuple to match the current transaction identifier. The engine uses a transaction isolation snapshot to determine visibility at runtime. Dead tuples left behind by older completed transactions are subsequently purged via the asynchronous VACUUM engine.

### WAL (Write-Ahead Logging)
To guarantee ACID safety without sacrificing performance, changes are appended sequentially to a transaction log in memory and flushed to disk during a COMMIT. Because sequential disk I/O is significantly faster than writing random 8KB heap pages across disk blocks, this ensures safety. In a crash, Postgres replays the WAL from the last Checkpoint (the point where all dirty memory pages were guaranteed to be safely written to disk).

## 4. Design Trade-Offs
### Heap Bloat vs. Undo-Log Chains
Because PostgreSQL writes every new row variation directly into the table heap, tables inevitably expand or "bloat" during high-update workloads. This places a heavy processing tax on the background ```autovacuum``` daemon, which must constantly sweep pages to free dead space. The massive advantage, however, is that read queries never have to reconstruct old row states by traversing backwards through chain structures like undo logs; the row version exists directly on the data page.

### Double Buffering Overhead
PostgreSQL reads pages into its custom shared_buffers pool using standard OS file access calls. Consequently, the operating system caches the exact same 8KB blocks inside the Linux kernel page cache. While this simplifies the Postgres kernel code and provides a secondary caching layer, it results in memory duplication, decreasing the absolute efficiency of host RAM.

## 5. Experiments / Observations
### Recommended Exercise: Query Plan and Optimizer Analysis
An execution plan analysis was run inside the WSL environment against the internal system catalog tables to observe how the storage engine interacts with the buffer manager:

```SQL
EXPLAIN (ANALYZE, BUFFERS)
SELECT c.relname, n.nspname 
FROM pg_class c 
JOIN pg_namespace n ON c.relnamespace = n.oid 
WHERE c.relkind = 'r';
```

### Actual Terminal Log Output:
```bash
                                                     QUERY PLAN
---------------------------------------------------------------------------------------------------------------------
 Hash Join  (cost=1.09..20.62 rows=68 width=128) (actual time=0.042..0.161 rows=68 loops=1)
   Hash Cond: (c.relnamespace = n.oid)
   Buffers: shared hit=11 read=4
   ->  Seq Scan on pg_class c  (cost=0.00..19.16 rows=68 width=68) (actual time=0.007..0.118 rows=68 loops=1)
         Filter: (relkind = 'r'::"char")
         Rows Removed by Filter: 345
         Buffers: shared hit=10 read=4
   ->  Hash  (cost=1.04..1.04 rows=4 width=68) (actual time=0.014..0.015 rows=4 loops=1)
         Buckets: 1024  Batches: 1  Memory Usage: 9kB
         Buffers: shared hit=1
         ->  Seq Scan on pg_namespace n  (cost=0.00..1.04 rows=4 width=68) (actual time=0.003..0.003 rows=4 loops=1)
               Buffers: shared hit=1
 Planning:
   Buffers: shared hit=163 read=32
 Planning Time: 9.798 ms
 Execution Time: 1.210 ms
``` 

### Architectural Analysis:
1. **Buffer Manager Metrics:** The line ```Buffers: shared hit=11 read=4``` directly confirms the double-layered retrieval strategy of the Buffer Manager. To perform the Hash Join, the engine required 15 pages. 11 pages were already pinned in the ```shared_buffers``` pool (```shared hit=11```), while 4 pages had to be brought into RAM from the underlying OS filesystem layer (```read=4```).

2. **Planner Precision:** The planner predicted exactly ```rows=68``` for the sequential scan on ```pg_class```, and the runtime engine processed exactly ```rows=68```. This highly accurate estimation shows that the cost formulas mapped perfectly to the statistical distribution data collected by the database from the ```pg_statistic``` system catalog table.

3. **Execution Footprint:** A total of 345 rows were removed via the filtering condition ```(relkind = 'r'::"char")```. Under Postgres MVCC, even filtered or non-matching rows must be read into memory buffers entirely, as visibility metrics can only be checked once a page resides safely inside the shared buffer pool.

## 6. Key Learnings
* **The Interdependency of Memory and Statistics:** The query planner does not guess. Its ability to construct optimal strategies (such as picking a Hash Join over a Nested Loop) relies completely on the mathematical data stored in ```pg_statistic```. If statistics go stale, the planner chooses suboptimal node paths.

* **The Structural Trade-off of Append-Only Engines:** Seeing that ```shared hit``` counts include filtered rows highlights that Postgres reads full physical blocks to evaluate data. The architectural cost of protecting transactions via append-only MVCC is an overhead of page-space processing that must be systematically managed.