# Topic 3: MySQL / InnoDB Storage Engine

This report explores the architectural design of MySQL's default storage engine, InnoDB. It details clustered index structures, memory management, transactional logging (undo and redo logs), row-level locking mechanisms, and provides a direct architectural comparison with PostgreSQL.

---

## 1. Problem Background

MySQL was originally designed with a pluggable storage engine architecture, separating query processing from the underlying physical storage details. Early engines like MyISAM used table-level locks and lacked support for transactions, foreign keys, and crash recovery. InnoDB was introduced to address these limitations, providing an ACID-compliant engine designed for high-concurrency enterprise workloads using row-level locking, clustered indexing, and transaction logging.

---

## 2. Architecture Overview

InnoDB organizes memory allocations and disk-based structures to balance transactional safety and I/O efficiency:

```
+-------------------------------------------------------------------------+
|                              INNODB MEMORY                              |
|                                                                         |
|  +-------------------------------------+  +--------------------------+  |
|  |             Buffer Pool             |  |        Log Buffer        |  |
|  | [Page Cache: Young/Old sublists]    |  | [Redo log record queue]  |  |
|  |                                     |  +--------------------------+  |
|  | - Change Buffer   - Adaptive Hash   |               |                |
|  +-------------------------------------+               v                |
+--------------------|-----------------------------------|----------------+
                     |                                   v
                     v                         +------------------+
         +-----------------------+             |  Redo Log Files  |
         |  Doublewrite Buffer   |             | (ib_logfile1..n) |
         | [Contiguous Disk Area]|             +------------------+
         +-----------|-----------+
                     v
             +---------------+
             | System/File   |
             | Tablespaces   |
             | (data.ibd)    |
             +---------------+
```

- **Buffer Pool**: Memory area caching loaded data and index pages, including helper structures like the Change Buffer and Adaptive Hash Index.
- **Log Buffer**: Caches unwritten redo log records in RAM before flushing them to disk.
- **Doublewrite Buffer**: A disk-based buffer where pages are written before they are written to their positions in the tablespace, preventing page corruption during write failures.

---

## 3. Internal Design

### Clustered Index and Table Organization

InnoDB organizes tables as index-organized structures based on B+ Trees:

```
                  +--------------------------------+
                  |  Secondary Index (Key: Name)   |
                  +--------------------------------+
                         /                  \
            +-----------v---------+  +-------v-------------+
            | Leaf: Alice -> ID=3 |  | Leaf: Bob -> ID=7   |
            +---------------------+  +---------------------+
                                       /
  Double Lookup                       /
  traverse Primary B-Tree            /
                                    v
                  +--------------------------------+
                  |  Clustered Index (Key: ID)     |
                  +--------------------------------+
                         /                  \
            +-----------v---------+  +-------v-------------+
            | Leaf: ID=3,         |  | Leaf: ID=7,         |
            | Data: [Alice, 22]   |  | Data: [Bob, 29]     |
            +---------------------+  +---------------------+
```

#### Primary Key Storage (Clustered Index)
- In InnoDB, the table data is physically organized according to the primary key.
- The leaf nodes of the primary key B+ Tree (the **Clustered Index**) contain the actual row values (clustered storage).
- **Lookup Cost**: Primary key queries can locate and retrieve row data in a single traversal of the clustered index B+ Tree.

#### Secondary Indexes
- Leaf nodes of secondary indexes store the indexed key and the corresponding primary key value.
- **Double Lookup**: Queries scanning a secondary index retrieve the primary key, then traverse the clustered index to fetch the full row payload, adding index traversal overhead.

---

### Buffer Pool Management

The **Buffer Pool** is the primary memory area used to cache table and index pages:
- **LRU List with Sublists**: To prevent sequential scans from flushing out frequently accessed pages, the buffer pool LRU list is divided into two segments:
  - *Young Sublist* (default 5/8 of the list): Caches recently accessed pages.
  - *Old Sublist* (default 3/8 of the list): Caches older, candidate pages.
  - Newly loaded pages are placed at the boundary between the two sublists. A page is promoted to the young sublist only if it remains accessed after a configured time delay (`innodb_old_blocks_time`), preventing single-scan pages from evicting active cache pages.
- **Change Buffer**: Caches modifications to secondary indexes when the target page is not in the buffer pool. This reduces random I/O overhead by deferring index page loads and merging changes during subsequent reads.
- **Doublewrite Buffer**: Before writing a modified page from the buffer pool to its tablespace file, InnoDB writes the page to the contiguous doublewrite buffer on disk and syncs it. Only after the sync completes does it write the page to its final location. If a system crash occurs during the final write, InnoDB restores the page from the doublewrite buffer, avoiding **torn page** corruption.

---

### Undo Logs and Redo Logs

InnoDB splits logging into separate files to handle crash recovery and concurrent transactions:

```
+------------------+------------------------------------+-------------------------------------+
| Dimension        | Undo Logs                          | Redo Logs                           |
+------------------+------------------------------------+-------------------------------------+
| **Primary Goal** | Transaction abort and MVCC reads   | Durability and crash recovery       |
| **Write Pattern**| Random (page modifications in undo)| Sequential append (log buffers)     |
| **Storage Area** | Undo tablespace segments           | Circular log files (ib_logfile0..n) |
| **Duration**     | Active until transaction purge     | Overwritten after checkpoint flush  |
+------------------+------------------------------------+-------------------------------------+
```

#### Undo Logs: MVCC Row Reconstruction
- Undo logs record transaction rollback details. If a transaction aborts, InnoDB reads the undo log to revert modifications.
- InnoDB updates pages **in-place**. Before overwriting a column value on a page, it writes the original value to the undo log.
- **Version Reconstruction**: When a transaction reads a row, InnoDB evaluates the transaction snapshot against the row's metadata header. If the row was modified by a newer transaction, InnoDB follows the roll pointer to the undo log and reconstructs the visible version in memory, allowing read operations to proceed without blocking writers.

#### Redo Logs: Crash Recovery
- Redo logs record physical modifications made to database pages.
- When a transaction commits, InnoDB writes the page modifications to the Redo Log Buffer and flushes them to disk sequentially.
- Physical page updates in the tablespace are deferred. If a crash occurs, InnoDB replays the redo log during startup to restore committed changes (roll-forward recovery).

---

### Row-Level Locking & Gap Locks

InnoDB supports row-level locking. To prevent phantom reads under the default `REPEATABLE READ` isolation level, it uses three lock types:

```
Index Records:   ... [ Record 10 ] -------- [ Record 20 ] -------- [ Record 30 ] ...
                         |                      |
Lock Types:         Record Lock             Gap Lock            Next-Key Lock
                   on value '10'         on gap (10, 20)       on interval (10, 20]
```

- **Record Locks**: Locks a specific index record (e.g., `SELECT * FROM t WHERE id = 10 FOR UPDATE` locks the record with ID 10).
- **Gap Locks**: Locks the range between index records, or the range before the first or after the last index record. It does not lock a specific record, but prevents other transactions from inserting values into the gap (e.g., locking the range `(10, 20)` blocks inserts of value 15).
- **Next-Key Locks**: A combination of a Record Lock on the index record and a Gap Lock on the gap before it. It locks the range `(10, 20]`. This is the default locking strategy used by InnoDB to resolve phantom read conflicts.

---

## 4. Design Comparison: PostgreSQL vs. InnoDB

The architectural differences between PostgreSQL and InnoDB affect their performance characteristics under varying workloads:

```
                                  UPDATE ROW OPERATION
                                  ====================

[PostgreSQL: Append-Only MVCC]
1. Insert new tuple version on page.
2. Mark old tuple as dead (t_xmax = XID).
3. Update all secondary indexes (requires physical pointer update).
* Consequence: Table bloat, requires regular VACUUM to reclaim space.

[InnoDB: In-Place Update + Undo Log]
1. Copy old data to Undo Log.
2. Overwrite data in-place on the page.
3. Secondary indexes remain unchanged (point to immutable Primary Key).
* Consequence: Complex Purge process, memory overhead to reconstruct old versions.
```

- **MVCC Storage Overhead**: 
  PostgreSQL stores all tuple versions in the main heap files. InnoDB stores only the current version in the table page, writing previous versions to the undo log. This prevents table bloat in InnoDB but adds CPU and memory overhead when reconstructing older versions for long-running transactions.
- **Secondary Index Updates**: 
  PostgreSQL secondary indexes contain physical pointers (TIDs) to the heap. Updating a row requires updating all secondary indexes unless a HOT optimization is triggered. In InnoDB, secondary indexes point to the primary key. This avoids secondary index updates on row moves, but requires a double lookup for queries executing via secondary indexes.
- **Cleanup Mechanics**: 
  PostgreSQL requires `VACUUM` to reclaim page space from dead tuples. InnoDB uses a background **Purge Thread** to remove undo log pages once they are no longer needed by active transactions.

---

## 5. Key Design Questions Resolved

### 1. Why does InnoDB need both undo and redo logs?
- Redo logs guarantee **Durability**. They record physical modifications to pages and are written sequentially, allowing InnoDB to defer writing dirty pages to tablespace files.
- Undo logs guarantee **Atomicity** and **Consistency**. They record logical rollback operations, allowing InnoDB to revert changes from aborted transactions and reconstruct previous row versions to support MVCC reads.

### 2. What advantages do clustered indexes provide?
- **Read Locality**: Storing the row data in the primary B+ Tree leaf nodes accelerates primary key queries by eliminating the need for a secondary read.
- **Range Queries**: Sequential primary key range scans are faster because records are physically sorted along the index page sequence.

### 3. Why did PostgreSQL choose a different MVCC model?
- PostgreSQL chose an append-only heap design to simplify database recovery, as there are no undo logs to parse or manage.
- This design avoids the CPU overhead of reconstructing older versions in memory, but delegates storage reclamation to the `VACUUM` daemon.
