# MySQL / InnoDB Storage Engine Architecture

## 1. Problem Background

### Why InnoDB Exists
Innobase Oy created InnoDB (Oracle later acquired the company) to bring ACID compliance and crash safety to MySQL. Before InnoDB arrived, MyISAM served as MySQL's default engine, but it offered no transaction support, no row-granularity locks, and no ability to recover after a crash.

### What Problem It Solves
Applications handling mixed reads and writes at scale needed transactional guarantees without sacrificing MySQL's familiar interface and replication features. InnoDB addressed this by introducing row-level locking alongside MVCC, allowing concurrent operations to proceed safely. Its logging infrastructure ensures committed data survives system failures.

---

## 2. Architecture Overview

### High-Level Architecture
InnoDB fits into MySQL as a replaceable storage module. MySQL's upper layers manage connections, parse SQL, and produce execution plans. InnoDB takes over at the storage layer, handling row access, transaction boundaries, and lock acquisition.

### Main System Components
*   **Buffer Pool:** An in-memory region that caches frequently accessed table and index pages.
*   **Log Buffer:** A temporary holding area for redo records before they get written to disk.
*   **System Tablespace:** Contains the data dictionary, doublewrite buffer, change buffer, and undo logs by default.
*   **File-per-table Tablespaces:** Optional separate files holding each table's data and indexes.
*   **Redo Logs:** Record physical page-level changes for crash recovery.
*   **Undo Logs:** Store previous row versions to support rollback and MVCC snapshots.

---

## 3. Internal Design

### Storage Structures (Clustered Indexes)
*   **Clustered Design:** Every InnoDB table is stored as a B-Tree indexed by the primary key. The leaf level of this tree holds the complete row data.
*   **Secondary Indexes:** Non-primary indexes store only the primary key value at their leaves, not the full row. Retrieving a row through a secondary index therefore requires traversing two B-Trees—first the secondary index, then the primary clustered index.

### Transaction Processing and Concurrency (MVCC & Locking)
*   **Oracle-style MVCC:** When a row gets updated, InnoDB modifies the row in its current location in the clustered index and copies the previous version into the undo log. Queries that need a consistent old snapshot reconstruct the earlier row from these undo records.
*   **Row-Level Locking:** Locks are placed on individual index entries rather than whole tables.
*   **Gap Locks:** Under REPEATABLE READ isolation, InnoDB locks the spaces between index entries to stop other transactions from inserting new rows into a queried range, thereby eliminating phantom reads.

### Recovery Mechanisms (Redo and Undo Logs)
*   **Redo Logs (Write-Ahead Logging):** Every page modification is recorded as a redo log entry before the page itself is written. After a crash, these logs are replayed to restore any changes that were committed but not yet flushed.
*   **Undo Logs:** Serve dual duty—they enable rollback of uncommitted transactions and supply earlier row snapshots for MVCC readers.

---

## 4. Design Trade-Offs

### InnoDB vs PostgreSQL Trade-offs

1.  **Updates: In-Place vs. Append-Only**
    *   *InnoDB:* Modifies rows directly in the clustered index. If no secondary index columns changed, the secondary indexes stay untouched, avoiding bloat. However, old row versions must be reconstructed from undo logs, which adds cost for long-running read transactions.
    *   *PostgreSQL:* Each update appends a new tuple version to the heap. This keeps snapshot creation simple but causes table bloat that must be cleaned by `VACUUM`.

2.  **Storage: Clustered vs. Heap**
    *   *InnoDB:* The clustered organization makes range scans over the primary key very fast. On the downside, lookups via secondary indexes are slower (they need the extra PK step), and wide primary keys bloat every secondary index.
    *   *PostgreSQL:* Uses a separate heap area. All indexes—primary or secondary—are symmetric and point directly to heap tuples. Primary key range scans are slower, but secondary indexes are leaner and faster.

3.  **Durability Logs: Redo + Undo vs. WAL**
    *   *InnoDB:* Maintains separate redo logs for crash recovery and undo logs for MVCC and rollback.
    *   *PostgreSQL:* Consolidates all recovery and concurrency information into a single WAL, leveraging old tuple versions already present in the heap.

---

## 5. Experiments / Observations

### Locking Behavior
*   Running `SELECT ... FOR UPDATE` on a range of rows reveals gap locking. If transaction A locks a range with IDs 10 through 20, transaction B cannot insert a row with ID 15 even though that row does not yet exist. This is how InnoDB prevents phantoms.

### Performance Impact of Primary Keys
*   Since rows are clustered by primary key, inserting monotonically increasing values (like auto-increment IDs) avoids page splits and fragmentation. Using random primary keys such as UUIDs causes excessive B-Tree rebalancing and random I/O, significantly hurting write throughput compared to PostgreSQL's heap-based approach.

---

## 6. Key Learnings

1.  **Clustered Indexes Force Careful PK Design:** Primary key choice directly affects every aspect of performance. A poorly chosen key degrades not just the table itself but all secondary indexes as well.
2.  **In-Place Updates Trade Bloat for Complexity:** By keeping the main table compact and relegating old versions to undo logs, InnoDB avoids vacuuming but must manage and navigate rollback segments during long-running queries.
3.  **Isolation Levels Map to Real Mechanisms:** Gap locks explain how InnoDB achieves REPEATABLE READ without escalating to SERIALIZABLE, providing a practical balance between consistency and concurrency.
