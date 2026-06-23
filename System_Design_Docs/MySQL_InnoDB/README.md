# MySQL InnoDB Storage Engine Architecture

## 1. Problem Background

InnoDB is the default transactional storage engine for MySQL. It exists because a relational database needs more than SQL parsing: it needs durable storage, indexes, transaction isolation, crash recovery, and locking. InnoDB provides these database-engine responsibilities under the MySQL server layer.

The main design goal of InnoDB is to support OLTP workloads: many short transactions, primary-key lookups, secondary-index lookups, row-level updates, and crash-safe commits. Its most important design choices are clustered primary-key storage, undo logs for MVCC and rollback, redo logs for crash recovery, and fine-grained locking.

## 2. Architecture Overview

```text
MySQL server layer
  |
  +--> SQL parser, optimizer, executor
  |
  v
InnoDB storage engine
  |
  +--> Buffer pool
  +--> Clustered index pages
  +--> Secondary index pages
  +--> Lock manager
  +--> Transaction system
  +--> Undo logs
  +--> Redo log
  +--> Change buffer / purge / checkpoint activity

Disk:
  tablespaces, redo log files, undo tablespaces, doublewrite-related storage
```

Data access flows through indexes. For a primary-key lookup, InnoDB navigates the clustered index and finds the full row directly in the leaf page. For a secondary-index lookup, InnoDB first finds the secondary key entry, then uses the stored primary key to look up the full row in the clustered index.

## 3. Internal Design

### Clustered Indexes

Each InnoDB table has a clustered index that stores row data. Usually this clustered index is the primary key. The leaf pages of the clustered index contain the actual row contents, not just pointers to a separate heap.

This makes primary-key lookups efficient because one B+ tree search reaches the row. It also makes range scans by primary key efficient because nearby primary keys tend to be stored near each other. The cost is that the primary key becomes part of the physical organization of the table. A large or random primary key can increase page splits and reduce locality.

### Secondary Indexes

InnoDB secondary indexes do not point directly to a heap tuple the way PostgreSQL indexes point to heap tuple locations. A secondary-index leaf entry stores the secondary key plus the primary-key value. To fetch the full row, InnoDB performs a second lookup in the clustered index.

This design avoids unstable physical row pointers because rows are organized by primary key. The trade-off is that secondary-index lookups can require two B+ tree traversals unless the query is covered by the secondary index.

### Buffer Pool

The buffer pool caches data and index pages in memory. It is critical for InnoDB performance because almost all table and index access flows through pages. Dirty pages are not written immediately on every update. Instead, changes are logged and dirty pages are flushed later.

The buffer pool reduces random disk I/O, but it also introduces checkpoint and flushing behavior. If dirty pages accumulate too much, the system must write them out, which can affect latency.

### Undo Logs

Undo logs store information needed to undo a transaction's changes. They also support consistent reads. If a transaction needs to see an older version of a row, InnoDB can reconstruct that older version from undo records.

This is different from PostgreSQL's heap-version model. PostgreSQL keeps multiple tuple versions in the table heap. InnoDB updates clustered index records more directly and uses undo logs to provide older versions when needed.

### Redo Logs

Redo logs are used for crash recovery. When data pages are changed, redo records describe how to replay those changes if MySQL crashes before dirty pages reach their tablespaces. During recovery, InnoDB replays redo for committed changes and uses undo information to roll back incomplete transactions.

The key idea is that redo logs make random page updates durable through a mostly sequential logging path.

### Row-Level Locking And Gap Locks

InnoDB supports row-level locking for updates. This allows different transactions to modify different rows concurrently. It also uses next-key locks and gap locks in some isolation levels to prevent phantom rows. A gap lock protects the space between index records, not only existing rows.

Gap locking improves correctness for repeatable reads and range predicates, but it can surprise application developers because transactions may block even when they are not updating the exact same row.

### Isolation And MVCC

InnoDB implements MVCC using transaction IDs and undo records. Consistent reads can see an older committed version of a row without blocking writers. Locking reads and writes still acquire locks when they need to protect current data.

## 4. Design Trade-Offs

| Design choice | Benefit | Cost |
| --- | --- | --- |
| Clustered primary key | Fast primary-key lookup and range locality | Primary-key choice affects physical layout |
| Secondary index stores primary key | Stable logical pointer to row | Extra clustered-index lookup |
| Undo logs | Rollback and consistent reads | Purge required to remove old history |
| Redo logs | Crash-safe recovery | Extra write path and checkpoint tuning |
| Row locks | Better write concurrency than table locks | Deadlocks and lock waits still possible |
| Gap locks | Prevents phantoms under range access | Can reduce concurrency |

Compared with PostgreSQL, InnoDB leans more heavily on clustered storage and undo logs. PostgreSQL's heap is append-version oriented, while InnoDB stores the row in the clustered index and uses undo to reconstruct previous versions. Both designs support MVCC, but they pay the cost in different places.

## 5. Experiments / Observations

### Clustered vs Secondary Lookup

Example:

```sql
CREATE TABLE users (
  id INT PRIMARY KEY,
  email VARCHAR(255),
  name VARCHAR(255),
  INDEX idx_email (email)
) ENGINE=InnoDB;

EXPLAIN SELECT * FROM users WHERE id = 10;
EXPLAIN SELECT * FROM users WHERE email = 'a@example.com';
```

Expected behavior:

- The primary-key query can find the full row directly in the clustered index.
- The secondary-index query can use `idx_email`, then fetch the clustered row by primary key.
- If the query only asks for indexed columns, the secondary index can become a covering index and avoid the extra clustered lookup.

### Locking Observation

For range updates under repeatable-read behavior, InnoDB may lock index gaps as well as existing records. This protects correctness against phantom inserts, but it means two transactions can block each other even when they are not updating the same existing row.

### PostgreSQL Comparison

PostgreSQL secondary indexes point to heap tuple identifiers. InnoDB secondary indexes store primary-key values. This single difference affects update behavior, lookup cost, and physical table organization.

## 6. Key Learnings

- InnoDB is built around B+ tree indexes, especially the clustered primary key.
- Primary-key design matters because it shapes physical locality.
- Undo logs provide older row versions for consistent reads and transaction rollback.
- Redo logs provide crash recovery by replaying changes after failure.
- Row-level locking improves concurrency, but gap locks can intentionally reduce concurrency to preserve isolation.
- InnoDB and PostgreSQL both implement MVCC, but their storage models are significantly different.
