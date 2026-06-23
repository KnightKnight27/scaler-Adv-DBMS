# PostgreSQL Internals

## 1. Problem Background

A relational database engine must satisfy several requirements at once. Multiple clients read and write concurrently and each expects a consistent view of the data. A statement written in declarative SQL must be turned into an efficient physical access plan over tables and indexes. Data must survive process crashes and power loss without corruption, and it must do so without paying the cost of a synchronous disk write for every page touched. Lookups over large tables must complete in time proportional to the depth of an index rather than the size of the table.

PostgreSQL addresses these requirements with a specific set of design choices: a multi-process server in which each connection is handled by a dedicated operating-system process; multiversion concurrency control (MVCC) so that readers and writers do not block each other; a shared buffer pool that mediates all page access; B-tree indexes built on a high-concurrency algorithm; and write-ahead logging (WAL) that makes commit cost one sequential disk flush rather than many random ones. The sections below describe how these components fit together and the engineering reasons behind each decision.

## 2. Architecture Overview

PostgreSQL runs as a collection of cooperating processes that share a single segment of shared memory. A supervisor process called the postmaster listens for incoming connections. For each accepted connection it forks a backend process that handles every query for that session until the client disconnects. This process-per-connection model gives strong fault isolation: a backend that crashes can be detected by the postmaster, which then resets shared memory and recovers rather than letting one session corrupt another. The cost is that each connection carries the memory and scheduling overhead of a full process, which is why high connection counts are usually fronted by a connection pooler.

The shared-memory segment holds structures that every backend needs to see: the shared buffer pool (the page cache), the WAL buffers, the lock table, and assorted bookkeeping such as the list of running transactions. Per-backend private memory holds session-local state and working areas for sorts and hashes.

A SQL statement moves through a fixed pipeline inside the backend. The parser turns text into a parse tree and checks syntax. The rewriter applies rules and view expansions. The planner and optimizer generate candidate execution plans and choose one using cost estimates derived from collected table statistics. The executor then walks the chosen plan, which is a tree of operators such as scans, joins, sorts, and aggregates, pulling tuples upward through the tree.

Several background processes run alongside the backends. The checkpointer periodically flushes dirty buffers and writes a checkpoint record. The background writer trickles dirty pages out ahead of demand so backends find clean victims to evict. The WAL writer flushes WAL buffers to disk on a schedule. Autovacuum launches worker processes that reclaim dead tuples and freeze old transaction ids. The archiver and replication processes ship WAL to standbys.

## 3. Internal Design

### Storage structures

Tables (called relations) and indexes are stored as files on disk, divided into fixed-size pages of 8 KB by default. Each table is one or more forks: the main fork holds the data, the free space map records where free space exists, and the visibility map tracks pages where all tuples are visible to all transactions. A page has a header, an array of item pointers (line pointers) growing from the front, and tuple data growing from the back, with free space in the middle. A tuple is addressed by a CTID, the pair (block number, line pointer index).

A heap tuple carries a header before its user data. The header includes xmin (the transaction id that created the tuple), xmax (the transaction id that deleted or replaced it, or zero), an info mask of status bits, and a CTID field that can point to a newer version of the same row. These fields are what MVCC and vacuuming operate on.

### Memory management

All access to table and index pages goes through the shared buffer pool, a fixed array of 8 KB frames sized by the shared_buffers parameter. Three structures cooperate to run it. The buffer table is a hash that maps a buffer tag, the triple (relation, fork, block number), to the id of the frame that holds it. The buffer descriptors are an array with one entry per frame, holding the per-frame state: a pin or reference count, a dirty flag, a usage count, a content lock, and flags for valid and IO-in-progress. The frames themselves hold the raw page bytes. To read a page, a backend computes its tag, looks it up in the buffer table, pins the descriptor so the frame cannot be evicted while in use, and takes the content lock in shared or exclusive mode.

Eviction uses a clock-sweep algorithm rather than a true least-recently-used (LRU) ordering. A true LRU list would have to be mutated on every page access to move the touched entry to the front, and that mutation under a global lock would serialize all buffer access and become a contention bottleneck on a busy server. Clock-sweep approximates LRU cheaply. Each descriptor has a small usage count (capped at a low maximum). A sweep pointer (nextVictimBuffer) advances circularly through the descriptors. When it lands on an unpinned frame with a nonzero usage count, it decrements the count and moves on. The first unpinned frame it finds with a usage count of zero is the victim. Pinned frames are skipped because they are in active use. Accessing a page raises its usage count, so frequently touched pages survive several sweeps before they can be evicted.

A single large one-pass scan would otherwise read many pages once each, drive their usage counts up, and push the existing working set out of the cache. This is sequential flooding. To prevent it, sequential scans, VACUUM, and bulk writes use a BufferAccessStrategy: a small ring of buffers (on the order of 256 KB for sequential scans) that is reused for that operation, so a large scan recycles its own handful of frames instead of consuming the whole pool.

### Index organization

The default index is a B-tree, implemented by the access method named nbtree. It is an implementation of the Lehman and Yao high-concurrency B-link tree. The defining additions over a textbook B-tree are a right-link on every page that points to its right sibling, and a high key on every page that is an upper bound on the keys the page may hold. These two fields let a searcher detect that a page split happened concurrently: if the sought key is greater than the high key, the search follows the right-link to the sibling instead of failing. Because of this, descending and scanning the tree need no read locks on the path, only a short lock on the single page being examined. This is what makes the structure high-concurrency.

Internal pages route searches; leaf pages hold an index key together with a CTID pointing to the heap tuple. High fan-out keeps the tree shallow, so even a large table is only a few levels deep and a point lookup touches few pages. When a leaf fills, it splits into two and a separator key is copied up into the parent. The tree grows in height only when the root itself splits, which is why all leaves remain at equal depth. Leaf pages are chained through their sibling links, so a range scan finds the first matching leaf by descending once and then walks sideways across leaves without returning to the upper levels.

If every column a query needs is present in the index, including columns added through INCLUDE, PostgreSQL can answer from the index alone and skip the heap fetch, provided the visibility map shows the relevant pages are all-visible. This is an index-only scan. A composite index can satisfy a predicate only on a leftmost prefix of its key columns, because the ordering of later columns is meaningful only within a fixed value of the earlier ones.

### Transaction processing

Each transaction is assigned a 32-bit transaction id (XID) when it first writes. Commit status for every transaction is recorded in the commit log (pg_xact). A statement executes against a snapshot, a record of which transactions had committed at the moment the snapshot was taken. A snapshot stores an xmin (every XID below it is settled), an xmax (every XID at or above it is treated as not yet started), and an explicit list of the XIDs that were in progress between those bounds. Under READ COMMITTED a fresh snapshot is taken at the start of each statement; under REPEATABLE READ and SERIALIZABLE a single snapshot is taken at transaction start and reused.

### Concurrency control

PostgreSQL uses MVCC so that reading never blocks writing and writing never blocks reading. An UPDATE does not overwrite a row in place. It writes a new tuple version and sets the xmax of the old version to the updating transaction. A DELETE sets xmax without writing a new version. A tuple version is visible to a snapshot if its xmin is committed and falls within the snapshot and its xmax is either unset or belongs to a transaction not visible as committed in that snapshot. Because each reader sees the version that matches its own snapshot, concurrent readers and writers proceed without mutual blocking. Row-level write conflicts are still serialized through row locks held in the tuple header and the shared lock table, so two transactions cannot update the same row at once.

The HOT (heap-only tuple) optimization reduces the cost of updates. When an update does not change any indexed column and the new version fits on the same page, the new version is linked from the old one through the CTID chain and no new index entries are created, since the existing index entries still point to a place on the page from which the live version is reachable.

The SERIALIZABLE isolation level adds serializable snapshot isolation (SSI) on top of MVCC. It runs on the same snapshots but tracks read/write dependencies between concurrent transactions and aborts one of them when it detects a pattern of dependencies that could produce a non-serializable result, so it gives true serializability without the reader blocking of strict two-phase locking.

### Recovery

Durability rests on write-ahead logging. Before a modified data page may be written to disk, the WAL record describing that change must already be flushed to durable storage. On commit, the WAL up to and including the commit record is flushed with a single fsync and only then is the client told the commit succeeded. The dirty data pages themselves are written later by the background writer or the checkpointer. A checkpoint flushes all currently dirty buffers and records a redo point, the WAL position from which recovery must begin.

After a crash, recovery opens the last checkpoint, finds its redo point, and replays the WAL forward from there, reapplying every logged change (the redo phase). PostgreSQL uses no separate undo log for this: changes made by transactions that never committed are simply never made visible by MVCC, because their XIDs are not marked committed, and the dead tuples they left behind are reclaimed later by VACUUM. To guard against torn pages, where an 8 KB page is only partly written when the machine fails, full_page_writes causes the first modification of a page after each checkpoint to log the entire page image, so recovery can restore a clean copy before applying later incremental records.

Superseded tuple versions accumulate as dead tuples. VACUUM, normally run automatically by autovacuum, reclaims that space and updates the free space and visibility maps. VACUUM also performs freezing: because XIDs are only 32 bits and wrap around after about four billion transactions, old tuples must have their xmin marked frozen so they stay visible regardless of the current XID counter. Failing to vacuum in time risks transaction-id wraparound, which is why autovacuum tracks each table's age and forces an anti-wraparound vacuum when needed.

## 4. Design Trade-Offs

The process-per-connection model gives clean fault isolation and a simple programming model inside each backend, with no need for fine-grained thread synchronization across sessions for private state. The cost is per-connection memory and context-switch overhead, so deployments with thousands of clients place a pooler in front of the server. This is a deliberate decision to favor robustness and simplicity over raw per-connection lightness.

MVCC removes read/write blocking and makes consistent reads cheap, which suits read-heavy and mixed workloads. The price is that updates and deletes leave dead tuples that must be collected later, which causes table and index bloat if vacuuming falls behind, and it requires the wraparound-prevention machinery described above. Storing visibility information per tuple also makes the heap the source of truth for visibility, which is why an index-only scan still has to consult the visibility map. The engineering judgment here is that the throughput won by never blocking readers outweighs the background cost of vacuuming, given an autovacuum system tuned to keep up.

Clock-sweep trades a small loss of replacement accuracy for a large gain in scalability. It does not track exact recency, but it avoids the global serialization that a true LRU list would impose, so it scales to many concurrent backends. The ring-buffer strategy for large scans accepts that a bulk operation will not be fully cached in exchange for protecting the hot working set of the rest of the system.

WAL converts the durability cost of a commit from many random page writes into one sequential flush. This raises throughput substantially because sequential fsync to the log is far cheaper than scattered random writes, and it enables physical replication and point-in-time recovery as a side benefit since the log is a complete change stream. The trade-offs are write amplification, since every change is written twice (once to WAL, once eventually to the data file) and full-page images enlarge the log further, and periodic checkpoint I/O spikes when many dirty pages are flushed at once. Checkpoint spreading and background writing exist to smooth that I/O.

B-tree indexes give logarithmic point lookups and efficient ordered range scans, and the Lehman-Yao design keeps them highly concurrent under write load. The limitations are the standard ones: an index speeds up only predicates it can serve (a leftmost prefix for composite indexes), it adds write and storage cost on every insert and non-HOT update, and it must itself be vacuumed.

## 5. Experiments / Observations

The following observations come from PostgreSQL 14 with two tables: customers with 10,000 rows and orders with 100,000 rows, with a secondary index on orders.customer_id. They show the planner selecting different physical plans from the same logical query depending on selectivity, driven by collected statistics.

An aggregate over the full join of the two tables chose a hash join. The plan built a Hash on the smaller 10,000-row customers table and probed it with a sequential scan of the 100,000-row orders table, feeding the result into a HashAggregate. The planner's estimated row count equaled the actual row count of 100,000. Execution took about 20.9 ms with 605 shared buffer hits.

```
HashAggregate
  -> Hash Join
       -> Seq Scan on orders        (rows = 100000)
       -> Hash
            -> Seq Scan on customers (rows = 10000)
```

The same join restricted by a selective predicate (WHERE c.id = 42) chose a nested loop instead. An Index Scan on customers_pkey returned the single matching customer row, and for that row a Bitmap Index Scan on the orders.customer_id index returned about 10 matching order rows. The estimate of 10 rows matched the actual 10 rows. Execution took about 0.102 ms, roughly 200 times faster than the full aggregate.

```
Nested Loop
  -> Index Scan using customers_pkey on customers (rows = 1)
  -> Bitmap Heap Scan on orders                    (rows = 10)
       -> Bitmap Index Scan on orders_customer_id_idx
```

The reason for the different choice is the statistics. The pg_stats view reported an n_distinct of about 9992 for orders.customer_id. The planner therefore estimates roughly 100000 / 9992, near 10 rows per customer. With a predicate that pins the query to one customer, the expected output is about 10 rows, and a nested loop with an index lookup on the inner side is cheaper for that few rows than building a hash over the whole table. Without the predicate, the query must scan all 100,000 orders, and a hash join with a single sequential pass over each table is cheaper than 10,000 separate index probes. The planner is choosing each plan from the cardinality it estimates, and in both cases the estimate matched the actual count, which is why the chosen plans were the cheap ones.

## 6. Key Learnings

PostgreSQL's design centers on a few interlocking decisions. MVCC keeps readers and writers from blocking each other, at the cost of dead-tuple cleanup and wraparound management handled by VACUUM and autovacuum. The shared buffer pool funnels all page access through a small set of structures and uses clock-sweep to approximate LRU without the contention a real LRU list would create, with ring buffers protecting the working set from large scans. WAL makes commit a single sequential flush and provides the change stream used for recovery and replication, accepting write amplification and checkpoint I/O in return. B-tree indexes built on the Lehman-Yao B-link algorithm stay shallow and highly concurrent, serving point lookups and ordered ranges and, when a query is fully covered, answering without touching the heap.

The cost-based planner ties these together. It does not follow fixed rules; it estimates cardinalities from statistics gathered by ANALYZE and selects the plan with the lowest estimated cost. The experiment above shows the same query resolving to a hash join or a nested loop purely from the estimated number of rows, which is why keeping statistics current is a practical requirement for good plans.

## References

- PostgreSQL documentation, Write Ahead Log configuration: https://www.postgresql.org/docs/current/runtime-config-wal.html
- PostgreSQL documentation, WAL Configuration: https://www.postgresql.org/docs/current/wal-configuration.html
- PostgreSQL documentation, Routine Vacuuming (wraparound and freezing): https://www.postgresql.org/docs/current/routine-vacuuming.html
- PostgreSQL source, nbtree README (Lehman and Yao B-link tree, high key, right-link): https://github.com/postgres/postgres/blob/master/src/backend/access/nbtree/README
- The Internals of PostgreSQL, Buffer Manager (buffer table, descriptors, clock-sweep, ring buffer): https://www.interdb.jp/pg/pgsql08/04.html
- DeepWiki, postgres/postgres Buffer Management: https://deepwiki.com/postgres/postgres/2.3.1-buffer-management
- Postgres Professional, MVCC in PostgreSQL, Snapshots: https://postgrespro.com/blog/pgsql/5967899
- Postgres Professional, MVCC in PostgreSQL, Freezing: https://postgrespro.com/blog/pgsql/5967948
- PostgreSQL B-Tree index optimizations (Fujitsu Fastware): https://www.postgresql.fastware.com/pzone/2025-02-postgresql-btree-index-optimizations
- Ports, Serializable Snapshot Isolation in PostgreSQL: https://www.drkp.net/papers/ssi-vldb12.pdf
