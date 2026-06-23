# Database Systems Architecture Analysis: MySQL (InnoDB) vs. PostgreSQL
## Objective
This document provides an architectural analysis of the MySQL InnoDB storage engine, contrasting its design choices—specifically regarding storage, indexing, and concurrency control—with PostgreSQL. The analysis is grounded in observed system behaviors from live experiments.

## 1. Storage & Indexing: Clustered vs. Heap Architecture
### The Architectural Difference
A fundamental difference between InnoDB and PostgreSQL lies in how they store table data physically on disk.
* **InnoDB uses Clustered Storage:** The table data itself is stored in the leaf nodes of the Primary Key B+Tree.
* **PostgreSQL uses Heap Storage:** Data is stored in an unordered "heap." Indexes (including the primary key) are separate data structures that contain pointers (CTIDs) to the physical block and offset in the heap.

### Experimental Evidence: Index Performance
We executed three queries against an InnoDB table with 100,000 rows to observe the performance cost of different index traversals.

| Query Type      | Query                                                       | Execution Cost | Actual Time (ms) |
| --------------- | ----------------------------------------------------------- | -------------- | ---------------- |
| Clustered (PK)  | SELECT * FROM employees WHERE id = 50000;                   | 0              | 0.06 - 0.09      |
| Secondary Index | SELECT * FROM employees WHERE department_id = 42;           | 319            | 0.17 - 1.51      |
| Covering Index  | SELECT id, department_id FROM ... WHERE department_id = 42; | 102            | 0.05 - 0.16      | 

### Analysis & Trade-offs
* **Why Clustered Indexes Improve Lookup Performance:** The Primary Key lookup is incredibly fast (cost 0) because once the B+Tree traversal reaches the leaf node, the entire row's data is already there. No secondary disk hop is required.

* **The Secondary Index Penalty:** The secondary index lookup is noticeably slower and more expensive. In InnoDB, secondary indexes do not point to physical disk addresses; they point back to the Primary Key. Therefore, querying a secondary index requires traversing two B+Trees (the secondary index tree, then the clustered index tree).

* **The Covering Index Workaround:** Query 3 demonstrates that if a query only requests columns present in the secondary index, InnoDB can bypass the clustered index lookup entirely, yielding performance similar to a direct PK lookup.

## 2. Multi-Version Concurrency Control (MVCC) Models
Both databases support MVCC to allow concurrent readers and writers without locking, but their implementations are fundamentally opposed.

### InnoDB: In-Place Updates + Undo Logs
InnoDB updates rows in-place. When a row is modified, the new data overwrites the old data in the B+Tree. To maintain MVCC, InnoDB pushes the previous version of the row into a separate Undo Log.

#### Observation:
In a ```REPEATABLE READ``` transaction, Terminal A updated ```id = 1``` to ```UPDATED_NAME``` but did not commit. Terminal B simultaneously queried ```id = 1``` and received ```Employee_1``` (the old value).
Why? Terminal B's query encountered a modified row in the buffer pool with a newer transaction ID. To satisfy the isolation level, InnoDB traversed the undo log pointer to reconstruct the older, committed snapshot of the row in memory.

### PostgreSQL: Append-Only Heap Updates
PostgreSQL does not update in-place, nor does it use undo logs for MVCC. It uses an append-only model.

#### Observation:
When inserting a row in Postgres, its physical location was ```ctid = (0,1)```. After running an UPDATE on that same row, the physical location changed to ```ctid = (0,2)```.Why? Postgres writes an entirely new version of the tuple to the heap. The old tuple ```(0,1)``` is marked as "dead" but remains on disk until a concurrent transaction no longer needs it, at which point the ```VACUUM``` process cleans it up.

#### Trade-offs: Why did PostgreSQL choose a different model?
* **PostgreSQL Advantages:** Rollbacks are incredibly fast (just mark the new tuple as aborted; no data to reverse). There is no theoretical limit to the size of an transaction, as there are no undo segments to exhaust.

* **InnoDB Advantages:** No need for a continuous VACUUM process to reclaim heap space, avoiding the dreaded "write amplification" problem that Postgres suffers from when indexing heavily updated tables.

## 3. The Dual-Log System: Why InnoDB Needs Undo and Redo Logs
It is critical to understand why InnoDB maintains two separate logs. They serve completely different ACID properties.
* **Redo Logs (The Forward Look - Durability):** InnoDB uses Write-Ahead Logging (WAL). When a transaction modifies data, the change is written to the redo log buffer and flushed to disk before the actual data pages in the buffer pool are flushed to the tablespace. If the database crashes, the redo log is replayed to restore committed transactions.
* **Undo Logs (The Backward Look - Atomicity & Isolation):** As observed in the MVCC experiment, the undo log stores how to reverse a change. It is used for two purposes: rolling back an aborted transaction (Atomicity) and reconstructing older row versions for concurrent readers (Isolation).

**Conclusion:** Redo logs ensure we never lose a committed write. Undo logs ensure we never see an uncommitted write.

## 4. Concurrency: Row-Level and Gap Locks
InnoDB's default isolation level is ```REPEATABLE READ```. To strictly enforce this and prevent "Phantom Reads" (where new rows appear in a range query during a transaction), InnoDB utilizes Gap Locks.

### Experimental Evidence
Terminal A executed:
```sql
SELECT * FROM employees WHERE id BETWEEN 105000 AND 105010 FOR UPDATE;
```
When Terminal B attempted to ```INSERT``` an ID of ```105005```, the transaction hung.
The ```SHOW ENGINE INNODB STATUS``` output revealed:
```bash
LOCK WAIT 2 lock struct(s), heap size 1128, 1 row lock(s)RECORD LOCKS space id 5 page no 503... lock_mode X insert intention waiting
```

### Analysis
Even though ID ```105005``` did not previously exist, Terminal A locked the gap between the existing index records. Terminal B's "insert intention" lock was blocked by Terminal A's exclusive gap lock. This architectural choice guarantees that if Terminal A repeats its range query, the result set will be identical, ensuring mathematical consistency at the cost of slightly reduced insert concurrency in targeted ranges.