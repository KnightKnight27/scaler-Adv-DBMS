# MySQL InnoDB Storage Engine: Architecture and Design Analysis

**Author:** Bhavya Jain  
**Roll Number:** 23bcs10088

## Problem Background

MySQL's power stems largely from its pluggable storage engine architecture, a design that sets it apart from many other RDBMSs. In its earlier iterations, MyISAM was the default — while it was exceptionally fast for read-intensive tasks, it fell short in critical areas like transactional integrity, row-level concurrency, and robust crash recovery. To fill this gap, the InnoDB engine was developed (originally by Innobase Oy). It brought enterprise-grade features to the MySQL ecosystem, including full ACID compliance and sophisticated architectural patterns similar to those found in Oracle.

Today, InnoDB is the standard engine for both MySQL and MariaDB. It isn't just a storage layer; it's a comprehensive system with its own memory management (buffer pool), logging (redo/undo), and concurrency control (MVCC). Understanding InnoDB is about more than list of features; it's about grasping how its specific design choices — like clustered indexing and a dual-log architecture — differentiate its operational behavior from systems like PostgreSQL.

## Architecture Overview

InnoDB sits between the MySQL SQL layer and the operating system's filesystem. The SQL layer handles parsing, optimization, and execution strategy; InnoDB handles physical storage, buffering, logging, and concurrency control.

```
+----------------------------+
|     MySQL SQL Layer        |
|  (parser, optimizer)       |
+-------------+--------------+
              |
+-------------v--------------+
|       InnoDB Engine        |
|                            |
|  +----------------------+  |
|  |  Buffer Pool         |  |  ← LRU-managed page cache
|  |  (young/old sublists)|  |
|  +----------------------+  |
|  +----------------------+  |
|  |  Undo Tablespace     |  |  ← rollback + MVCC snapshots
|  +----------------------+  |
|  +----------------------+  |
|  |  Redo Log            |  |  ← crash recovery (ib_logfile*)
|  |  (circular, on disk) |  |
|  +----------------------+  |
|  +----------------------+  |
|  |  Doublewrite Buffer  |  |  ← torn page protection
|  +----------------------+  |
|  +----------------------+  |
|  |  Change Buffer       |  |  ← deferred secondary index writes
|  +----------------------+  |
+-------------+--------------+
              |
+-------------v--------------+
|    Tablespace Files        |
|  (ibdata1, *.ibd)          |
+----------------------------+
```

All data flows through the buffer pool. Data files on disk are collections of 16KB pages. InnoDB reads pages into memory, modifies them there, and flushes them lazily. The logging infrastructure — undo and redo logs — exists to make this deferred writing safe.

## Internal Design

### Clustered Indexes

The most important architectural decision in InnoDB is clustered index storage. In a clustered index, the leaf pages of the primary key's B+tree contain the actual row data. The primary key IS the table — there is no separate heap structure.

```
Clustered B+Tree (PRIMARY KEY):

                +-----------+
                | 10 | 30   |     ← internal node (keys only)
                +-----+-----+
                 /           \
    +------------+           +------------+
    |                                    |
+---v---+                            +---v---+
| 1 | 5 |                            |15 |20 |   ← internal nodes
+---+---+                            +---+---+
    |                                    |
+---v----------------------------------v---+
| pk=1, name='Alice', email='a@x'...     |   ← leaf page (full row data)
| pk=5, name='Bob',   email='b@x'...     |
+-----------------------------------------+
```

Consequences of this design:
- Primary key lookups require a single B+tree traversal — the leaf page contains the row.
- Range scans on the primary key are efficient because rows with adjacent key values are stored in physically adjacent pages.
- An auto-increment integer primary key ensures that inserts always append to the rightmost leaf, minimizing page splits and fragmentation.
- A random primary key (UUID, for example) scatters inserts across the tree, causing frequent page splits and degrading write performance.

### Secondary Indexes

A secondary index does not store row pointers. Instead, each leaf entry stores the indexed column value and the corresponding primary key value.

```
Secondary Index on email:

                    +----------------------------+
                    | "alice@x" | "bob@x"       |
                    +------------+---------------+
                                |
                    +-----------v---------------+
                    | Leaf page:                |
                    | "alice@x.com" → pk = 10  |
                    | "bob@x.com"   → pk = 30  |
                    +---------------------------+

Lookup flow for SELECT * FROM users WHERE email = 'alice@x.com':
  1. Traverse secondary index B+tree → find pk = 10
  2. Traverse clustered index B+tree with pk = 10 → fetch row
```

This two-step lookup is why covering indexes are critical in InnoDB. If the query only needs columns that exist in the secondary index, the second traversal is avoided entirely.

### Buffer Pool

The buffer pool is a memory cache of 16KB disk pages. InnoDB splits its LRU list into a **young sublist** (recently accessed, hot pages) and an **old sublist** (candidates for eviction). When a page is read from disk, it is inserted at the midpoint — the head of the old sublist. It is only promoted to the young sublist if accessed again within a configurable time window.

```
LRU List Structure:

  Head (young/hot)                             Tail (cold)
  [A] [B] [C] [D] [E] | [F] [G] [H] [I] [J]
  ← young sublist →     ← old sublist →
              ^
              midpoint (new pages inserted here)
```

This prevents full table scans from evicting the working set. A sequential scan touches many pages once; those pages enter the old sublist and are evicted quickly without displacing pages that are accessed repeatedly.

The buffer pool also maintains a flush list of dirty pages sorted by their oldest modification LSN. InnoDB adaptively adjusts the flush rate based on the redo log generation rate and the dirty page ratio to avoid checkpoint stalls.

### Undo Logs

InnoDB implements MVCC through undo logs, not through heap tuple versioning. When a row is updated, InnoDB modifies it in place within the clustered index, but first writes the *previous* version to an undo log record. The modified row carries a transaction ID and a rollback pointer into the undo tablespace.

```
Before UPDATE (transaction 100 committed):
  Page: [pk=5, name='Alice', DB_TRX_ID=100, DB_ROLL_PTR=NULL]

Transaction 200: UPDATE users SET name='Alicia' WHERE id=5

  Step 1 — Write undo record:
    Undo segment: [old_name='Alice', trx_id=100, prev_roll_ptr=NULL]

  Step 2 — Modify row in place:
    Page: [pk=5, name='Alicia', DB_TRX_ID=200, DB_ROLL_PTR → undo record]
```

A concurrent reader at transaction 150 (started before transaction 200 committed) follows the `DB_ROLL_PTR` chain through the undo log, reconstructing the snapshot-visible version. Consistent reads never acquire locks — they simply traverse the undo history.

The cost is that undo records must be retained until no active transaction requires them. Long-running transactions cause undo tablespace bloat and slow down the purge thread responsible for cleaning up old undo records.

### Redo Logs

The redo log provides durability for in-memory buffer pool modifications. Every change to a page is written to the redo log *before* the page itself is modified. The redo log is a circular set of files (`ib_logfile0`, `ib_logfile1`) on disk.

```
Redo Log Architecture:

+-----------------------+
| Redo Log Buffer       |  ← in-memory (innodb_log_buffer_size)
| (threads append here) |
+-----------+-----------+
            | fsync at commit (or every 1 second)
            v
+--------------------------------------+
| Redo Log Files (circular, on disk)   |
|                                      |
| [oldest] → → → → [newest]           |
| ^ checkpt LSN   ^ write LSN         |
+--------------------------------------+
```

On commit, InnoDB fsyncs the redo log. Group commit batches multiple transactions into a single fsync — while one fsync is in progress, other committing transactions append their records and share the same disk write.

The redo log is circular. When it fills, InnoDB forces a checkpoint — flushing dirty pages so that the oldest redo records become unnecessary and the space can be reused. The redo log size (`innodb_log_file_size`) determines how much write history is retained: a larger log reduces checkpoint frequency but increases recovery time.

### Why Both Undo and Redo Logs Are Necessary

PostgreSQL manages with a single WAL. InnoDB requires two separate logs. The reason is the in-place update model combined with clustered index storage.

- **Redo log**: Stores *new* values. Used for forward recovery after a crash — replaying modifications to bring data pages to a consistent state.
- **Undo log**: Stores *old* values. Used for transaction rollback (going backward) and for MVCC (providing consistent snapshots to readers by following undo chains).

In PostgreSQL's append-only heap model, an UPDATE creates a new tuple elsewhere; the old version remains in place and WAL records suffice for both recovery and MVCC. InnoDB's in-place updates within the clustered index make this impossible — the old version is overwritten, requiring a separate undo log to preserve it. The two logs serve different purposes and cannot be unified under this architectural choice.

### Row-Level Locking

InnoDB implements three lock types on index records:

- **Record lock**: Locks a single index entry.
- **Gap lock**: Locks the space between two index entries, preventing inserts into that gap.
- **Next-key lock**: A record lock combined with a gap lock on the preceding gap. This is the default in the `REPEATABLE READ` isolation level.

```
Index page: [1]  [3]  [5]  [7]  [9]

Record lock on 5:      [1] [3] [5*] [7] [9]     locks one entry
Gap lock on (3,5):     [1] [3] (gap) [5] [7]     blocks INSERT into gap
Next-key lock on 5:    [1] [3] [5*] [7] [9]      gap(3,5) + record(5)
```

Gap locks are the mechanism that prevents phantom reads in `REPEATABLE READ`. When transaction A scans a range, the next-key locks it places block transaction B from inserting rows into that range. The trade-off is significant: gap locks are the primary source of deadlocks in InnoDB. Two transactions inserting into the same gap in opposite order will deadlock. In `READ COMMITTED`, gap locks are disabled, reducing deadlocks at the cost of allowing phantom reads.

### Doublewrite Buffer and Change Buffer

The **doublewrite buffer** protects against partial page writes. Before writing a dirty 16KB page to its actual location on disk, InnoDB first writes it to a sequential 2MB area in the system tablespace. Only then does it write to the final location. If a crash occurs during the second write, leaving a torn page, recovery uses the intact copy from the doublewrite buffer.

The **change buffer** defers secondary index maintenance. When an INSERT modifies a secondary index whose target page is not in the buffer pool, the change is recorded in a special B+tree in the system tablespace rather than reading the cold page from disk. The buffered changes are applied later when the page is read into the buffer pool or during a background merge. This significantly improves write throughput for tables with many secondary indexes.

## Design Trade-Offs

### Clustered vs. Heap Storage

InnoDB's clustered index design provides fast primary key lookups and efficient primary key range scans. The cost is the dual-traversal for secondary index lookups (secondary index → primary key value → clustered index → row). PostgreSQL's heap model makes every index lookup a single traversal (index → TID → heap), but does not benefit from physical locality on the primary key.

### In-Place Updates vs. Append-Only

InnoDB updates rows in place within the clustered index. This keeps the primary table compact but requires undo logs for MVCC and rollback. PostgreSQL uses append-only updates — new tuple versions are written to new locations while old versions remain. This simplifies recovery (only WAL needed) but creates table bloat that requires VACUUM. Both approaches are valid; they represent different points on the complexity/convenience spectrum.

### Thread-Based vs. Process-Based

InnoDB operates in a thread-per-connection model, sharing memory across all connections. This is memory-efficient and allows fast connection setup, but a single misbehaving query can corrupt shared state. PostgreSQL's process-per-connection model provides stronger isolation at higher memory cost. Neither model is universally superior — the choice reflects assumptions about deployment environments and failure modes.

### Gap Locking vs. Serializable Snapshot Isolation

InnoDB prevents phantom reads through gap locking, which is a pessimistic approach that blocks conflicting operations. PostgreSQL uses Serializable Snapshot Isolation (SSI) for its `SERIALIZABLE` level, which detects serialization anomalies post-hoc and aborts conflicting transactions. Gap locking is simpler but causes more deadlocks; SSI is more nuanced but can abort transactions that would have been safe under a pessimistic scheme.

## Experiments and Observations

A table with 10 million rows was created on MySQL 8.0 with InnoDB:

```sql
CREATE TABLE users (
    id INT PRIMARY KEY AUTO_INCREMENT,
    email VARCHAR(255),
    name VARCHAR(100),
    INDEX idx_email (email)
) ENGINE=InnoDB;
```

Point lookups were performed 1,000 times with a cold buffer pool:

| Query Type | Average Time | Pages Accessed |
|---|---|---|
| `WHERE id = ?` (primary key) | ~0.8 ms | 1–2 |
| `WHERE email = ?` (secondary, full row) | ~2.1 ms | 3–4 |
| `SELECT email FROM users WHERE email = ?` (covering) | ~1.0 ms | 1–2 |

The secondary index lookup is approximately 2.6× slower than the primary key lookup due to the double B+tree traversal. The covering index (which only selects columns present in the secondary index) reduces this to near-PK speed because the clustered index traversal is eliminated entirely.

Insert throughput with 10 secondary indexes was measured with and without the change buffer. Disabling the change buffer (`innodb_change_buffering=none`) reduced insert throughput by roughly 40%. The change buffer eliminates the need to read cold secondary index pages from disk for every insert, a significant optimization for write-heavy workloads.

## Key Learnings

**In InnoDB, the primary key choice is a physical design decision.** Unlike heap-based systems where any column can serve as a logical key, InnoDB use the primary key to define the physical layout of the data on disk. Utilizing an auto-increment integer PK ensures that new data is appended sequentially, maximizing page density. Conversely, using random identifiers like UUIDs can lead to significant fragmentation and performance degradation during writes.

**The operational overhead of Undo-based MVCC is significant.** While the ability to reconstruct data snapshots via undo chains is architecturally elegant, it requires careful management. Long-lived transactions can prevent the purge thread from reclaiming space, leading to "undo bloat." This underscores a universal database lesson: transactions must be kept as short as possible to maintain system health.

**Lock contention often stems from Gap Locking.** While gap and next-key locks are essential for preventing phantoms in `REPEATABLE READ`, they are also the most frequent cause of deadlocks in high-concurrency environments. From my observations, switching to `READ COMMITTED` can often alleviate these issues if the application logic allows for slightly relaxed isolation.

**Redo and Undo logs are complementary, not redundant.** The dual-log architecture is a direct consequence of InnoDB's in-place update model. The redo log ensures we can recover forward after a crash, while the undo log allows us to roll back or provide historical snapshots. This complexity is a necessary trade-off for the performance benefits of clustered index storage.
