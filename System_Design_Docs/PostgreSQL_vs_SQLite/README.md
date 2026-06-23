# PostgreSQL vs SQLite: An Architectural Comparison

## 1. Problem Background

PostgreSQL and SQLite are both relational SQL engines, but they were built to solve different problems, and almost every structural difference between them follows from that divergence.

SQLite was written by D. Richard Hipp in 2000 while he worked on software for the US Navy DDG-79 guided-missile destroyer. The existing setup used an Informix database server, and the application stopped working whenever that server went down. The reaction was to remove the server entirely and read the data straight off the disk drive. SQLite is therefore an in-process, serverless, zero-configuration engine: the entire library compiles into the host application, and the database is a single ordinary file accessed with normal file system calls. The project's own documentation states that SQLite competes with fopen(), not with client-server databases such as PostgreSQL, MySQL, or Oracle. The design goal is local data storage for one application, with the emphasis on economy, reliability, and the absence of any process to install or administer.

PostgreSQL descends from the POSTGRES research project started by Michael Stonebraker at UC Berkeley in 1986, with a SQL front end added in the mid-1990s. It is a client-server database aimed at a shared repository of data with many concurrent clients. A supervisor process called the postmaster listens for connections and forks a dedicated backend process for each one. Backends are separate operating-system processes that coordinate through a shared memory segment. The architecture targets concurrency, centralized control, and durability across many simultaneous writers.

The distinction is not which engine is better. It is whether the data belongs to a single process that wants a file with SQL semantics, or to a server that arbitrates access among many clients.

## 2. Architecture Overview

SQLite has no server and no separate address space. The application links the SQLite library and calls into it directly. Reads and writes go to the database file through the operating system. There is no network protocol, no authentication layer, and no background daemon. Concurrency between processes is mediated entirely by file locks on the database file and, in write-ahead log mode, by a small shared-memory index file. State that would otherwise live in a server lives instead in the file format and in the locking protocol.

PostgreSQL runs as a set of cooperating processes around one shared memory region. The postmaster allocates that shared memory at startup, authenticates incoming connections against pg_hba.conf, and forks one backend per connection. Each backend parses, plans, and executes SQL for its client, and dies when the client disconnects. The shared memory region holds the buffer pool (database pages cached in memory), the WAL buffers, the lock tables, and the transaction status caches that every backend reads and writes. Auxiliary background processes handle work that no single backend owns: the WAL writer, the background writer, the checkpointer, the autovacuum workers, and the statistics collector. Process isolation is a deliberate property. A backend that crashes on a bad query does not corrupt the address space of the others, and the postmaster can reset shared state and continue.

The following table summarizes the structural contrast.

| Property | SQLite | PostgreSQL |
|---|---|---|
| Deployment | In-process library, single file | Client-server, multi-process |
| Connection model | Direct function calls | Postmaster forks a backend per connection |
| Coordination | File locks plus a shared-memory index in WAL mode | Shared memory segment plus semaphores |
| Default page size | 4096 bytes | 8192 bytes |
| Table organization | Clustered B-tree keyed by rowid | Heap with separate B-tree indexes |
| Concurrency | One writer at a time, many readers | MVCC, many concurrent writers on distinct rows |
| Durability default | Rollback journal (undo) | Write-ahead log (redo) |

## 3. Internal Design

### Storage structures

Both engines store data in fixed-size pages and both use the slotted-page layout. A page has a header, an array of pointers that grows forward from the front, and the records themselves that grow backward from the end, with free space in the middle. A row is addressed by page number plus a slot index, which lets the engine move a record within a page during compaction without changing the external pointer to it.

In PostgreSQL the page is 8 KB. The page header (PageHeaderData) is 24 bytes and carries the LSN of the last WAL record that touched the page, a checksum, and the offsets that bound the free space. Each line pointer (ItemIdData) is 4 bytes and holds an offset and length pair. PostgreSQL is heap-organized: table rows live in an unordered heap file, and the physical location of a row is its CTID, a 6-byte pair of page number and item index. Every index, including the one backing a primary key, is a separate B-tree. The leaf entries of that B-tree store the indexed key plus the CTID that points into the heap. A lookup through an index is therefore two hops: descend the index to find a CTID, then read the heap page that CTID names.

In SQLite the default page is 4096 bytes, and the page size must be a power of two between 512 and 65536. The whole database is a forest of B-trees inside one file. A table is itself a B-tree keyed by a 64-bit signed integer rowid, and the rows live in the leaf pages of that tree in rowid order. This is a clustered layout: there is no separate heap and no separate primary-key structure, because the table is the primary-key index. A secondary index is a second B-tree whose entries are the indexed columns followed by the rowid. Resolving a secondary index lookup descends the index B-tree to recover the rowid, then descends the table B-tree a second time to reach the row. For a WITHOUT ROWID table the trailing reference is the declared primary key instead of a rowid.

### Memory management

PostgreSQL keeps a shared buffer pool in the shared memory segment. All backends read and pin pages from this single cache, which is why dirty pages written by one backend become visible to the buffer accounting of all of them. Per-backend memory (the work_mem region used for sorts and hashes, for example) is private to each process. The checkpointer and background writer move dirty buffers to disk so that backends rarely have to write pages themselves.

SQLite has a per-connection page cache sized by the cache_size pragma. Because the engine is in-process, this cache lives in the application's own heap. There is no shared buffer pool across processes in rollback-journal mode; coordination across processes happens through the file and its locks. In WAL mode a shared-memory file holds an index into the log so that multiple connections on the same machine can locate the latest version of a page quickly.

### Index organization

PostgreSQL's default index type is a B-tree, and indexes are always physically separate from the table heap. This makes the heap independent of any one access path and allows many indexes of different types (B-tree, hash, GiST, GIN, BRIN) over the same heap, at the cost of a second access to reach the row and of extra space for a primary-key index that does not exist for free.

SQLite collapses the primary access path into the table itself. The rowid B-tree is the table, so a lookup by rowid is a single descent with no separate index to maintain or store. Secondary indexes are ordinary B-trees that point back by rowid.

### Transaction processing and recovery

SQLite in its default mode uses a rollback journal, which is an undo log. Before a page is modified, its original content is copied into a journal file. Changes are then applied to the database file. A commit flushes the modified pages and deletes the journal. If a crash leaves a journal behind, the next process to open the database detects this hot journal and rolls the original pages back, which restores the pre-transaction state. Atomicity comes from the rule that the transaction is committed only at the instant the journal is deleted. WAL mode inverts this into a redo scheme: the original database file is left alone, new page images are appended to a -wal file, and a commit appends a commit record. A checkpoint later copies committed pages from the log back into the main database. WAL mode creates two side files, app.db-wal for the log and app.db-shm for the shared-memory index.

PostgreSQL uses write-ahead logging, a redo log. The rule is that the log record describing a change is flushed to durable storage before the data page it describes. After a crash, recovery replays the log forward from the last checkpoint to reconstruct committed changes. WAL is stored as 16 MB segment files under the pg_wal directory.

### Concurrency control

SQLite serializes writers with locks on the whole database file. The lock progresses through the states UNLOCKED, SHARED, RESERVED, PENDING, and EXCLUSIVE. Many connections can hold SHARED locks and read at once, but only one can hold the RESERVED then EXCLUSIVE lock needed to write, so there is one writer at a time over the entire file. WAL mode relaxes the reader-versus-writer conflict: a reader records an end mark in the log and reads a consistent view up to that mark while a writer appends past it, so readers and the single writer no longer block each other. There is still exactly one writer, because there is one append point in one WAL file.

PostgreSQL uses multiversion concurrency control. A write does not overwrite a row in place. It writes a new row version and marks the old one as expired, using the xmin and xmax transaction-id fields in each tuple header. Each statement sees a snapshot defined by which transaction ids were committed at the time the snapshot was taken. Because old versions remain readable, reads never block writes and writes never block reads. Many transactions write concurrently as long as they touch different rows. When two transactions try to update the same row, a row-level lock serializes them, and the second waits for the first to commit or abort. The cost of this model is that expired tuples accumulate. VACUUM removes the dead tuples and reclaims space; without it the heap and indexes grow without bound. SQLite has no equivalent obligation because it does not keep multiple row versions.

## 4. Design Trade-Offs

The serverless model gives SQLite zero administration, no connection setup cost, and a single portable file. The price is coarse write concurrency. With one writer at a time across the whole file, a workload with many concurrent writers will serialize on the database lock, and a write attempt that cannot get the lock fails quickly rather than queueing. WAL mode removes reader-writer blocking and roughly halves write cost by replacing the double write of the rollback journal with one sequential append, but it does not add a second writer, it requires shared memory so it cannot run over a network file system, and the log must be checkpointed back into the database or read performance degrades as the log grows.

PostgreSQL's process-per-connection model and MVCC give true multi-writer concurrency and strong isolation between connections, which is the reason it scales to many simultaneous clients. The engineering costs are visible in the storage numbers. Each tuple carries a 23-byte header with xmin and xmax, the heap and every index are separate structures, and a primary key requires its own B-tree. The redo-based WAL means a change is written twice, once to the log and once to the data file at checkpoint. MVCC requires VACUUM to be run, and a forked process plus a fresh snapshot per connection make connections relatively expensive, which is why production deployments place a connection pooler in front of the database.

The index design follows the same logic. SQLite's clustered table makes primary-key access a single B-tree descent and stores the primary key for free, but every secondary index lookup pays a second descent of the table B-tree, and rows are physically ordered by rowid whether or not that helps a query. PostgreSQL's heap keeps the table independent of any index and supports many index types over one table, at the cost of the extra hop from index leaf to heap and the space of a separate primary-key index.

The recovery models reflect the deployment target. SQLite's default undo journal keeps the common single-connection case simple and needs no background process. PostgreSQL's redo log is built for a server that must recover a busy multi-writer database and stream changes to replicas, which an undo scheme could not support as cleanly.

## 5. Experiments / Observations

The following observations come from PostgreSQL 14.20 and SQLite 3.51.0 on macOS, using the same schema users(id int primary key, name text, email text) with a secondary index on email and 100,000 rows.

Storage footprint:

| Engine | Total | Table or heap | Primary-key index | Email index |
|---|---|---|---|---|
| SQLite | about 6.3 MB | about 3.5 MB (clustered, includes the primary key) | none separate | about 2.8 MB |
| PostgreSQL | about 15 MB | about 6.4 MB heap | about 2.2 MB | about 6.8 MB |

PostgreSQL used roughly 2.4 times the space. The difference comes from SQLite having no separate primary-key index and using a more compact encoding, against PostgreSQL's heap plus a separate primary-key index plus the 23-byte per-tuple header that carries xmin and xmax.

Page size on disk was 4096 bytes for SQLite and 8192 bytes for PostgreSQL.

Bulk load of 100,000 rows took about 0.08 s on SQLite and about 0.54 s on PostgreSQL. The in-process engine has no client-server round trips and a lighter durability path for the load.

Concurrency behaved as the locking models predict. With one SQLite write transaction open, a second connection running BEGIN IMMEDIATE was refused at once with the error "database is locked". In PostgreSQL, with one transaction holding a row lock, a second session updating the same row blocked until its lock_timeout fired at 1001.9 ms, while a second session updating a different row committed in 1.1 ms. The same-row case serializes through a row lock; the different-row case proceeds because MVCC does not put the two transactions in conflict.

Query plans showed the access paths directly. The SQLite primary-key lookup reported "SEARCH USING INTEGER PRIMARY KEY (rowid=?)", which is a single descent of the table B-tree. PostgreSQL reported "Index Scan using users_pkey", a descent of the separate primary-key B-tree followed by a heap fetch. Enabling WAL mode in SQLite created the side files app.db-wal and app.db-shm. PostgreSQL kept 16 MB WAL segment files under pg_wal.

## 6. Key Learnings

The architectural split between the two engines reduces to one decision: whether a server arbitrates access to the data. SQLite removed the server, so its data model, locking, and recovery are all built around one file touched by ordinary file system calls. PostgreSQL kept the server, so its data model, MVCC, and redo logging are all built around a shared memory segment and many backend processes.

Storage organization is a direct consequence of that choice. A clustered rowid B-tree gives SQLite a compact single file and cheap primary-key access, while a heap with separate indexes gives PostgreSQL index independence and many index types at the cost of space and an extra hop.

Concurrency is the sharpest practical difference. One writer at a time is acceptable, and often faster, for an embedded or single-application workload, and it is the wrong model for many concurrent writers. MVCC supports concurrent writers and snapshot reads but obliges the system to run VACUUM and to carry per-tuple version metadata.

The measured results match the theory. The roughly 2.4 times storage difference traces to the per-tuple header and the separate primary-key index. The faster SQLite bulk load traces to the in-process path. The immediate "database is locked" error against the blocking-then-committing PostgreSQL sessions is the file lock and MVCC distinction made visible.

## References

- https://www.sqlite.org/whentouse.html
- https://www.sqlite.org/different.html
- https://www.sqlite.org/fileformat2.html
- https://www.sqlite.org/lockingv3.html
- https://www.sqlite.org/wal.html
- https://corecursive.com/066-sqlite-with-richard-hipp/
- https://www.postgresql.org/docs/current/storage-page-layout.html
- https://www.postgresql.org/docs/current/mvcc-intro.html
- https://www.postgresql.org/docs/current/connect-estab.html
