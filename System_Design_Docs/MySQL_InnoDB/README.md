# 1. Problem Background

MySQL is one of the most widely deployed relational database systems in the world. While MySQL provides the SQL interface and server infrastructure, much of its storage and transaction behavior is determined by the underlying storage engine.

In modern MySQL deployments, the default storage engine is InnoDB.

InnoDB was designed to provide:

* ACID-compliant transactions
* High concurrency
* Crash recovery
* Row-level locking
* Multi-Version Concurrency Control (MVCC)

Unlike simpler storage engines that focus primarily on data persistence, InnoDB incorporates sophisticated mechanisms for memory management, logging, indexing, and concurrency control.

A key design goal of InnoDB is balancing performance with reliability. To achieve this, it uses clustered indexes for efficient primary-key access, a large buffer pool for caching, undo logs for transaction rollback, redo logs for durability, and row-level locking for concurrent access.

This study explores the internal architecture of InnoDB and examines how its design differs from PostgreSQL. Particular focus is placed on clustered storage, memory management, transaction processing, locking mechanisms, and recovery systems.

# 2. InnoDB Architecture Overview

InnoDB is implemented as a storage engine beneath the MySQL server layer.

```text
                Client
                   |
                   v
            MySQL Server
                   |
        +----------+----------+
        |                     |
        v                     v
   Query Parser         Optimizer
        |
        v
   InnoDB Storage Engine
        |
  +-----+-----+-----+-----+
  |           |           |
  v           v           v
Buffer     Undo       Redo
 Pool      Logs       Logs
  |
  v
Data Pages
```

The MySQL server handles SQL parsing, optimization, and client communication, while InnoDB manages:

* Physical storage
* Indexes
* Transactions
* Locking
* Crash recovery
* Memory management

Major InnoDB components include:

### Buffer Pool

Primary memory cache used to store frequently accessed pages.

### Clustered Index Storage

Tables are physically organized around the primary key.

### Undo Logs

Store previous row versions for rollback and MVCC.

### Redo Logs

Record changes for crash recovery and durability.

### Lock Manager

Coordinates row locks, gap locks, and transaction isolation.

Together, these components enable InnoDB to support transactional workloads with strong consistency guarantees.

# 3. Storage Engine Design

One of the most important architectural characteristics of InnoDB is its use of **clustered storage**.

Unlike PostgreSQL, which stores tables as heap files and indexes separately, InnoDB organizes table data directly inside the primary key B-Tree.

This design affects:

* Data layout
* Query performance
* Index structure
* Update costs
* Storage efficiency

---

## 3.1 Clustered Indexes

A clustered index determines the physical order in which rows are stored.

In InnoDB:

> The PRIMARY KEY index is the table.

This means table rows are stored directly inside the leaf pages of the primary key B-Tree.

```text id="q6kt7q"
Primary Key B-Tree

          [50]
         /    \
      [20]    [80]
      /  \    /  \
   Rows Rows Rows Rows
```

Unlike PostgreSQL:

```text id="98s4o2"
Heap Table
     +
Separate B-Tree Index
```

InnoDB does not require a separate heap relation for table storage.

---

### Why Clustered Storage Exists

Consider:

```sql id="s9b3ga"
SELECT *
FROM customers
WHERE customer_id = 500;
```

With a clustered index:

```text id="7ozwqj"
Root
  |
Internal Page
  |
Leaf Page
  |
Row Data
```

The lookup reaches the actual row immediately.

No additional table lookup is required.

Advantages:

* Faster primary key access
* Better locality of reference
* Reduced I/O for primary key lookups

Trade-off:

* Primary key updates become expensive
* Row movement may be required

---

## 3.2 Primary Key Storage

The PRIMARY KEY defines both:

```text id="i8owyk"
Logical Identifier
        +
Physical Storage Order
```

Example:

```sql id="h96ly5"
CREATE TABLE customers (
    customer_id INT PRIMARY KEY,
    customer_name VARCHAR(100)
);
```

Rows are stored physically in ascending primary key order.

Simplified representation:

```text id="ntmx9g"
1
2
3
4
5
...
```

This organization improves range-query performance.

Example:

```sql id="xv5f5w"
SELECT *
FROM customers
WHERE customer_id
BETWEEN 100 AND 200;
```

Because rows are physically close together, fewer pages need to be accessed.

---

### Experimental Observation

The customers table was created using:

```sql id="7np9hm"
customer_id INT PRIMARY KEY AUTO_INCREMENT
```

InnoDB automatically created a clustered B-Tree index.

Verification:

```sql id="nl6t0z"
SHOW INDEX FROM customers;
```

Observed output:

```text id="dqplr4"
PRIMARY
BTREE
Cardinality ≈ 9693
```

This confirms that the table's primary key is implemented using a B-Tree clustered index.

---

## 3.3 Secondary Indexes

Secondary indexes behave differently from primary indexes.

Suppose:

```sql id="rf0ew5"
CREATE INDEX idx_city
ON customers(city);
```

In PostgreSQL:

```text id="mll4ab"
Secondary Index
      |
      v
Heap Tuple
```

In InnoDB:

```text id="5v9u1x"
Secondary Index
      |
      v
Primary Key
      |
      v
Clustered Index
      |
      v
Row Data
```

The secondary index does not point directly to the row.

Instead it stores:

```text id="7q6gfm"
Secondary Key
      +
Primary Key Value
```

Example:

```text id="mdxy8p"
City = Mumbai
Primary Key = 500
```

---

### Secondary Index Lookup

Query:

```sql id="2cxl7e"
SELECT *
FROM customers
WHERE city = 'Mumbai';
```

Execution:

```text id="5qzc9v"
Secondary Index
      |
      v
Primary Key
      |
      v
Clustered Index
      |
      v
Row
```

This process is called:

```text id="z7l4ep"
Double Lookup
```

Advantages:

* Secondary indexes remain compact.
* Clustered storage remains consistent.

Trade-off:

* Additional lookup required.
* More work than primary key access.

---

## 3.4 Comparison with PostgreSQL Storage

### PostgreSQL

```text id="u5r3nx"
Heap Table
      +
Indexes
```

Characteristics:

* Rows stored separately from indexes.
* Index entries point to heap tuples.
* Updates create new tuple versions.
* VACUUM required to remove dead tuples.

---

### InnoDB

```text id="7v8g4z"
Clustered Index
      =
Table Storage
```

Characteristics:

* Primary key B-Tree contains row data.
* No separate heap relation.
* Secondary indexes reference primary keys.
* In-place storage organization.

---

## 3.5 Benefits of Clustered Storage

### Faster Primary Key Lookups

Single B-Tree traversal reaches row data directly.

### Efficient Range Queries

Nearby primary key values are stored close together.

### Improved Locality

Related records often occupy adjacent pages.

### Reduced Random I/O

Fewer page accesses required for primary-key workloads.

---

## 3.6 Trade-Offs of Clustered Storage

### Expensive Primary Key Changes

Changing the primary key may require row relocation.

### Larger Secondary Index Operations

Secondary indexes contain primary key values.

### Insert Hotspots

Sequential keys can concentrate inserts on a small number of pages.

### Storage Dependency on Key Design

Poor primary key choices can negatively affect performance.

---

## 3.7 Why InnoDB Uses Clustered Indexes

InnoDB was designed for transactional workloads where primary-key access patterns are extremely common.

Examples:

```sql id="ks8jtz"
SELECT * FROM customer
WHERE customer_id = ?;
```

```sql id="yhwknc"
SELECT * FROM order
WHERE order_id = ?;
```

Clustered storage minimizes the number of page accesses required for these operations.

The trade-off is additional complexity for secondary indexes and higher costs when primary key values change.

This represents a classic database engineering decision: InnoDB sacrifices some flexibility to optimize the most common access patterns encountered in OLTP (Online Transaction Processing) systems.

# 4. Memory Management

Efficient memory management is critical for database performance because disk I/O is significantly slower than memory access.

To minimize disk operations, InnoDB maintains a dedicated memory region called the **Buffer Pool**.

The Buffer Pool acts as a cache for:

* Data pages
* Index pages
* Undo pages
* Dictionary pages

Almost every read and write operation performed by InnoDB passes through the Buffer Pool.

---

## 4.1 Buffer Pool Architecture

The Buffer Pool is the primary memory component of InnoDB.

```text id="2pn6qa"
             Query
               |
               v
        InnoDB Engine
               |
               v
         Buffer Pool
          /       \
         /         \
 Buffer Hit      Buffer Miss
     |               |
     v               v
  Memory          Disk Read
```

When a page is requested:

### Buffer Hit

If the page is already present:

```text id="s6vw8q"
Requested Page
      |
Buffer Pool
      |
Returned Immediately
```

No disk access occurs.

---

### Buffer Miss

If the page is not present:

```text id="hsjlwm"
Requested Page
      |
Disk Read
      |
Buffer Pool
      |
Returned
```

The page is first loaded into memory before being accessed.

---

## 4.2 Why InnoDB Uses a Dedicated Buffer Pool

Modern operating systems already provide a page cache.

A natural question is:

> Why not simply rely on the OS cache?

InnoDB chooses to manage its own cache because it possesses database-specific knowledge.

For example:

```text id="5v4dzx"
Data Page
Index Page
Undo Page
```

have different access patterns.

InnoDB can make caching decisions based on:

* Query behavior
* Transaction activity
* Index usage
* Dirty page status

This level of control is not available through the operating system page cache alone.

---

## 4.3 Page Reads

InnoDB stores data in fixed-size pages.

Default page size:

```text id="j5vjyh"
16 KB
```

When a row is requested:

```sql id="sm6i3x"
SELECT *
FROM customers
WHERE customer_id = 500;
```

InnoDB:

1. Locates the page.
2. Checks the Buffer Pool.
3. Loads the page if necessary.
4. Returns the row.

The Buffer Pool operates at the page level rather than the row level.

---

## 4.4 Dirty Pages

When data is modified:

```sql id="2l5i1o"
UPDATE customers
SET city = 'Mumbai'
WHERE customer_id = 500;
```

InnoDB does not immediately write the modified page to disk.

Instead:

```text id="7b0q1x"
Page Modified
      |
Dirty Page
      |
Stored in Buffer Pool
```

The page remains in memory until it is later flushed to disk.

Advantages:

* Fewer disk writes
* Better throughput
* Improved batching

---

## 4.5 Buffer Pool Replacement

The Buffer Pool has finite capacity.

Eventually pages must be evicted.

InnoDB uses a modified LRU (Least Recently Used) algorithm.

Simplified view:

```text id="2rxy6f"
Most Recently Used
         |
         v
[P1][P2][P3][P4][P5]
         ^
         |
Least Recently Used
```

Frequently accessed pages remain in memory.

Rarely used pages become eviction candidates.

This helps maximize the probability of future buffer hits.

---

## 4.6 Experimental Observation

The following information was obtained using:

```sql id="9t4j3s"
SHOW ENGINE INNODB STATUS\G
```

Observed values:

```text id="goe6q4"
Buffer Pool Size = 8192 pages
Free Buffers = 6896
Database Pages = 1296
Modified Pages = 0
```

Interpretation:

* Only a portion of the buffer pool was currently occupied.
* No dirty pages were waiting to be flushed.
* The workload fit comfortably within available memory.

---

## 4.7 Buffer Pool Size

Observed configuration:

```sql id="4utl0h"
SHOW VARIABLES
LIKE 'innodb_buffer_pool_size';
```

Result:

```text id="6r5r5o"
134217728 bytes
```

Equivalent:

```text id="vdslqv"
128 MB
```

This memory region is allocated specifically for InnoDB caching operations.

---

## 4.8 Buffer Pool Hit Rate

Observed value:

```text id="nr6gsi"
Buffer pool hit rate = 1000 / 1000
```

This indicates that nearly all page requests were satisfied directly from memory.

Benefits:

* Reduced disk I/O
* Lower latency
* Improved throughput

A high hit rate is generally a sign that the working dataset fits comfortably within available memory.

---

## 4.9 Relationship with Transaction Processing

The Buffer Pool works closely with:

### Redo Logs

Before dirty pages are written:

```text id="6wpgpf"
Redo Log Written
      |
Page Flush
```

This ensures durability.

---

### Undo Logs

Old row versions may remain accessible through:

```text id="7yms1e"
Undo Records
```

used for rollback and MVCC.

---

### Lock Manager

Pages cached in memory may still be protected by row locks or gap locks.

The Buffer Pool therefore serves as a central point where storage, transactions, recovery, and concurrency mechanisms interact.

---

## 4.10 Comparison with PostgreSQL

PostgreSQL uses:

```text id="11o57t"
Shared Buffers
```

InnoDB uses:

```text id="5mqolx"
Buffer Pool
```

Both serve similar purposes:

* Cache pages
* Reduce disk I/O
* Improve performance

However:

### PostgreSQL

* Uses 8 KB pages
* Shared Buffers managed by PostgreSQL

### InnoDB

* Uses 16 KB pages
* Buffer Pool managed by InnoDB

Both systems rely heavily on memory caching, but the surrounding storage and transaction architectures differ significantly.

---

## 4.11 Summary

The Buffer Pool is one of the most important components of InnoDB.

It:

* Stores frequently accessed pages in memory.
* Reduces disk I/O.
* Buffers dirty pages before flushing.
* Supports transaction processing.
* Works together with redo logs and undo logs.

The experimental results demonstrated a very high buffer pool hit rate and low memory pressure, illustrating how effective caching contributes to InnoDB's performance.

# 5. Transaction Processing

InnoDB is a fully ACID-compliant transactional storage engine.

To provide:

* Atomicity
* Consistency
* Isolation
* Durability

InnoDB relies on several internal mechanisms:

```text id="z9w0by"
Transactions
     |
     +-- Undo Logs
     |
     +-- Redo Logs
     |
     +-- MVCC
     |
     +-- Checkpointing
```

These components work together to ensure data remains correct even in the presence of concurrent transactions, crashes, and system failures.

---

## 5.1 Undo Logs

Undo logs store the previous version of modified rows.

Example:

```sql id="0wxm8v"
UPDATE customers
SET city = 'Mumbai'
WHERE customer_id = 100;
```

Before modifying the row, InnoDB stores the old value inside the undo log.

```text id="5vd2ht"
Before Update

Customer 100
City = Delhi
```

Undo record:

```text id="22z28w"
Customer 100
City = Delhi
```

After update:

```text id="opr32w"
Customer 100
City = Mumbai
```

If the transaction rolls back:

```sql id="35kkk9"
ROLLBACK;
```

the undo log restores the previous value.

---

### Why Undo Logs Are Needed

Undo logs serve two purposes:

### Rollback

Restore previous data if a transaction fails.

### MVCC

Provide historical row versions for concurrent readers.

This is a major architectural difference from PostgreSQL.

PostgreSQL stores old versions directly inside heap pages.

InnoDB stores previous versions in undo records.

---

## 5.2 Experimental Observation

From:

```sql id="m1csh3"
SHOW ENGINE INNODB STATUS\G
```

Observed:

```text id="zc8mnp"
History List Length = 8
```

The History List represents undo information that has not yet been purged.

This indicates that InnoDB maintains previous row versions even after transactions complete.

The purge process eventually removes obsolete versions when they are no longer required.

---

## 5.3 Redo Logs

Redo logs provide durability.

Before a modified page is written to disk:

```text id="2vch6w"
Change Recorded
      |
Redo Log
      |
Page Flush Later
```

This follows the Write-Ahead Logging principle.

If the system crashes:

```text id="m1j53i"
Crash
```

InnoDB can replay redo records and restore committed changes.

---

### Redo Log Workflow

Transaction:

```sql id="n4t39z"
UPDATE accounts
SET balance = balance - 100;
```

Execution:

```text id="kl1wko"
Modify Page In Memory
         |
Generate Redo Record
         |
Flush Redo Log
         |
Commit
         |
Write Page Later
```

This ensures durability even if data pages have not yet reached disk.

---

## 5.4 Experimental Observation

Observed:

```text id="bpb9wp"
Log Sequence Number = 80508323
```

```text id="1s9p1q"
Log Written Up To = 80508323
```

```text id="3g4y4q"
Log Flushed Up To = 80508323
```

These values indicate that:

* Redo records were generated.
* Redo records were written.
* Redo records were flushed to durable storage.

At the time of observation:

```text id="5zyq0r"
Written = Flushed
```

indicating that all generated log records had already reached stable storage.

---

## 5.5 Checkpointing

Eventually dirty pages must be written from memory to disk.

InnoDB periodically performs checkpoints.

```text id="v33yha"
Buffer Pool
      |
Dirty Pages
      |
Checkpoint
      |
Disk
```

Checkpoints reduce recovery time because only redo records generated after the checkpoint must be replayed.

---

### Experimental Observation

Observed:

```text id="uqi7k5"
Last Checkpoint = 80508323
```

This value matched:

```text id="s3tbk0"
Log Sequence Number = 80508323
```

indicating that the system was fully synchronized and no additional redo replay would be required at that moment.

---

## 5.6 MVCC in InnoDB

InnoDB implements Multi-Version Concurrency Control (MVCC).

Unlike PostgreSQL:

```text id="t6oy1d"
Old Versions
Stored In Heap
```

InnoDB stores previous versions in undo logs.

```text id="73x3k5"
Current Row
      |
Undo Records
      |
Historical Versions
```

Example:

```text id="hvnmkn"
Version 3
     |
Version 2
     |
Version 1
```

Readers can access older versions without blocking writers.

Benefits:

* Consistent reads
* High concurrency
* Reduced lock contention

---

## 5.7 Consistent Read

Suppose:

```text id="e9wp1j"
Transaction A
Reads Customer

Transaction B
Updates Customer

Transaction A
Reads Again
```

MVCC ensures Transaction A continues to see a consistent snapshot.

```text id="7x5nql"
Snapshot
     |
Historical Version
```

This prevents inconsistent results during concurrent workloads.

---

## 5.8 Why InnoDB Needs Both Undo and Redo Logs

A common question is:

> Why maintain two separate log systems?

The answer is that they solve different problems.

### Undo Logs

Purpose:

```text id="93nz7t"
Go Backward
```

Used for:

* Rollback
* MVCC
* Historical versions

---

### Redo Logs

Purpose:

```text id="vmp42j"
Go Forward
```

Used for:

* Crash recovery
* Durability
* Replaying committed changes

Together:

```text id="mn2bwg"
Undo
  |
Rollback

Redo
  |
Recovery
```

provide complete transactional support.

---

## 5.9 Comparison with PostgreSQL

### PostgreSQL

```text id="d07qmw"
Heap Tuples
      |
Old Versions Remain
```

Characteristics:

* Append-style updates
* Tuple versioning
* VACUUM cleanup

---

### InnoDB

```text id="s6j1wy"
Current Row
      |
Undo Records
```

Characteristics:

* In-place storage model
* Undo-based MVCC
* Purge cleanup process

---

### Architectural Trade-Off

PostgreSQL:

Advantages:

* Simpler visibility model
* Strong snapshot isolation

Trade-off:

* Table bloat
* VACUUM required

---

InnoDB:

Advantages:

* Compact table storage
* Efficient clustered organization

Trade-off:

* More complex undo infrastructure
* Additional purge processing

---

## 5.10 Summary

Transaction processing in InnoDB relies on the coordinated operation of:

* Undo Logs
* Redo Logs
* MVCC
* Checkpoints
* Purge Processing

The experimental observations demonstrated active redo logging, checkpoint synchronization, and retained undo history, illustrating how InnoDB maintains both durability and concurrency.

This architecture enables InnoDB to support high-performance transactional workloads while preserving full ACID guarantees.

# 6. Locking and Concurrency Control

While MVCC allows readers and writers to operate concurrently, some situations still require explicit locking.

InnoDB therefore combines:

```text id="htrq7u"
MVCC
   +
Locks
```

to ensure transaction isolation and consistency.

The lock manager is responsible for coordinating access to rows, index ranges, and transactional resources.

---

## 6.1 Why Locks Are Needed

Consider two transactions:

### Transaction A

```sql id="i1d6h0"
UPDATE accounts
SET balance = balance - 100
WHERE account_id = 1;
```

### Transaction B

```sql id="ks6yaf"
UPDATE accounts
SET balance = balance + 50
WHERE account_id = 1;
```

If both modify the same row simultaneously:

```text id="dxw4j6"
Lost Updates
Corrupted Data
```

may occur.

Locks prevent these conflicts.

---

## 6.2 Row-Level Locking

The primary locking mechanism in InnoDB is the row lock.

Example:

```sql id="3opm6n"
BEGIN;

UPDATE customers
SET city = 'Mumbai'
WHERE customer_id = 100;
```

InnoDB places an exclusive lock on the affected row.

```text id="h4f2rn"
Row 100
    |
Exclusive Lock
```

Other transactions attempting to modify the same row must wait.

```text id="e6x5go"
Transaction A
      |
Locks Row

Transaction B
      |
Waits
```

Advantages:

* High concurrency
* Minimal contention
* Fine-grained locking

This is significantly more scalable than table-level locking.

---

## 6.3 Shared and Exclusive Locks

InnoDB supports two primary lock types.

### Shared Lock (S)

Allows reading.

```text id="hptjmo"
Read
Read
Read
```

Multiple transactions may hold shared locks simultaneously.

---

### Exclusive Lock (X)

Allows modification.

```text id="7h9q84"
Update
Delete
Insert
```

Only one transaction may hold an exclusive lock on a row.

---

Compatibility:

| Lock      | Shared | Exclusive |
| --------- | ------ | --------- |
| Shared    | Yes    | No        |
| Exclusive | No     | No        |

---

## 6.4 Gap Locks

One of InnoDB's unique features is the gap lock.

A gap lock protects a range between index entries.

Example index:

```text id="fprvfj"
10
20
30
40
```

Gap:

```text id="2jlwm6"
20 ---- 30
```

InnoDB can lock this interval even though no row exists there.

---

### Why Gap Locks Exist

Consider:

```sql id="ccp19z"
SELECT *
FROM customers
WHERE customer_id
BETWEEN 20 AND 30
FOR UPDATE;
```

Without gap locks:

```text id="n7jlwm"
Transaction A
Reads Range

Transaction B
Inserts 25
```

Transaction A would observe a different result if it repeated the query.

This is called a:

```text id="0q4m2o"
Phantom Read
```

Gap locks prevent such inserts.

---

## 6.5 Next-Key Locking

In practice InnoDB often uses:

```text id="3ef7db"
Next-Key Locks
```

A next-key lock combines:

```text id="4w1gmh"
Row Lock
     +
Gap Lock
```

Example:

```text id="4apz0n"
[20]
   |
Gap
   |
[30]
```

Both the row and surrounding range become protected.

This mechanism is critical for preventing phantom reads.

---

## 6.6 Isolation Levels

Isolation levels determine how transactions interact.

InnoDB supports:

### Read Uncommitted

```text id="2ffu9w"
Lowest Isolation
```

Allows dirty reads.

Rarely used.

---

### Read Committed

```text id="2wg9mk"
Sees Only Committed Data
```

Common in many database systems.

---

### Repeatable Read

Default InnoDB isolation level.

```text id="xkqj7r"
Consistent Snapshot
```

Uses MVCC and next-key locking.

Prevents:

* Dirty reads
* Non-repeatable reads

and significantly reduces phantom reads.

---

### Serializable

Highest isolation level.

```text id="jmyhcb"
Maximum Consistency
```

Provides strongest guarantees but reduces concurrency.

---

## 6.7 How MVCC and Locks Work Together

A common misconception is that MVCC eliminates the need for locks.

In reality:

```text id="g14f5l"
MVCC
     |
Read Consistency
```

and

```text id="c92gn7"
Locks
     |
Write Coordination
```

solve different problems.

Example:

```text id="mw8d6p"
Reader
     |
Old Version

Writer
     |
New Version
```

MVCC handles visibility.

However:

```text id="a7efxj"
Writer
     |
Writer
```

still requires locking to avoid conflicts.

---

## 6.8 Comparison with PostgreSQL

### PostgreSQL

Concurrency primarily relies on:

```text id="zby6u7"
MVCC
+
Tuple Versioning
```

Old row versions remain inside heap storage.

Phantom protection is largely achieved through snapshot isolation and predicate locking at higher isolation levels.

---

### InnoDB

Concurrency relies on:

```text id="rj7gfc"
MVCC
+
Row Locks
+
Gap Locks
+
Next-Key Locks
```

Historical versions are stored in undo logs.

Additional locking mechanisms prevent phantom reads.

---

## 6.9 Trade-Offs

### Advantages

* High concurrency
* Fine-grained locking
* Strong consistency guarantees
* Prevention of phantom reads

---

### Costs

* Increased implementation complexity
* Lock management overhead
* Potential deadlocks
* Additional memory consumption

---

## 6.10 Summary

InnoDB achieves concurrency through a combination of MVCC and sophisticated locking mechanisms.

Key components include:

* Row-Level Locks
* Shared Locks
* Exclusive Locks
* Gap Locks
* Next-Key Locks

These mechanisms allow InnoDB to support large numbers of concurrent transactions while preserving transactional correctness and isolation.

The use of gap locks and next-key locking is one of the major architectural differences between InnoDB and PostgreSQL, reflecting different approaches to implementing transaction isolation and preventing phantom reads.

# 7. PostgreSQL vs InnoDB

Although PostgreSQL and InnoDB are both transactional database systems that provide ACID guarantees, they use fundamentally different internal architectures.

Many of their design choices reflect different priorities regarding storage organization, concurrency control, transaction management, and recovery.

Understanding these differences helps explain why the two systems behave differently under various workloads.

---

## 7.1 Storage Organization

### PostgreSQL

PostgreSQL stores tables as heap relations.

```text id="a8s5cx"
Heap Table
      +
Separate Indexes
```

Rows are stored independently of indexes.

Indexes contain references to heap tuples.

Advantages:

* Flexible storage organization
* Easy support for multiple index types
* Efficient handling of updates

Trade-off:

* Additional lookup required after index traversal

---

### InnoDB

InnoDB uses clustered storage.

```text id="7u7rpt"
Primary Key B-Tree
        =
Table Storage
```

Rows are stored directly inside the clustered index.

Advantages:

* Fast primary-key access
* Improved locality
* Efficient range scans

Trade-off:

* More expensive primary-key modifications
* Secondary indexes require additional lookups

---

## 7.2 Update Processing

One of the most significant architectural differences is how updates are performed.

### PostgreSQL

Updates create new tuple versions.

```text id="m9n7xg"
Old Tuple
     |
New Tuple
```

The old version remains in the heap.

Advantages:

* Simplified MVCC implementation
* Strong snapshot isolation

Trade-off:

* Dead tuples accumulate
* VACUUM required

---

### InnoDB

InnoDB updates rows in place while storing previous versions in undo logs.

```text id="8v3b2m"
Current Row
      |
Undo Record
```

Advantages:

* Compact storage
* Less table bloat

Trade-off:

* More complex undo infrastructure

---

## 7.3 MVCC Implementation

Both systems support MVCC but implement it differently.

### PostgreSQL

MVCC based on tuple versioning.

Tuple metadata:

```text id="d0fh7z"
xmin
xmax
```

Historical versions remain inside table pages.

Example:

```text id="4b95vw"
Version 1
Version 2
Version 3
```

stored together.

---

### InnoDB

MVCC based on undo records.

```text id="p65qgn"
Current Version
       |
Undo Chain
       |
Older Versions
```

Historical versions are reconstructed from undo information.

---

## 7.4 Cleanup Mechanisms

### PostgreSQL

Uses:

```sql id="x2g4oq"
VACUUM
```

Responsibilities:

* Remove dead tuples
* Reclaim storage
* Prevent transaction ID wraparound

---

### InnoDB

Uses:

```text id="gmxol7"
Purge Process
```

Responsibilities:

* Remove obsolete undo records
* Reclaim MVCC history

---

### Architectural Difference

PostgreSQL cleans:

```text id="p7rqyt"
Dead Tuples
```

InnoDB cleans:

```text id="4v6r3m"
Undo Records
```

---

## 7.5 Recovery Mechanisms

Both systems use write-ahead logging principles.

### PostgreSQL

Uses:

```text id="j0kltt"
WAL
```

Workflow:

```text id="04z6v7"
WAL Record
      |
Flush WAL
      |
Write Data Page
```

---

### InnoDB

Uses:

```text id="r0mrzw"
Redo Log
```

Workflow:

```text id="8stwrh"
Redo Record
       |
Flush Redo Log
       |
Write Data Page
```

Both approaches provide durability and crash recovery.

---

## 7.6 Memory Management

### PostgreSQL

Primary cache:

```text id="d54xkh"
Shared Buffers
```

Default page size:

```text id="sxh19h"
8 KB
```

Managed by PostgreSQL's Buffer Manager.

---

### InnoDB

Primary cache:

```text id="khvxmu"
Buffer Pool
```

Default page size:

```text id="pn5q4h"
16 KB
```

Managed by InnoDB.

Observed in the experiment:

```text id="yr1xdr"
Buffer Pool Size = 128 MB
Hit Rate = 1000/1000
```

Both systems maintain dedicated database caches rather than relying entirely on the operating system.

---

## 7.7 Locking Strategy

### PostgreSQL

Relies heavily on:

```text id="x4cpm3"
MVCC
```

and lightweight row locking.

Phantom protection is largely achieved through snapshot isolation and higher-level locking mechanisms.

---

### InnoDB

Uses:

```text id="mqyrxn"
MVCC
+
Row Locks
+
Gap Locks
+
Next-Key Locks
```

Additional locking prevents phantom reads during range queries.

---

## 7.8 Primary Key Access

### PostgreSQL

Primary-key lookup:

```text id="v3pt4x"
Index
   |
Heap Lookup
```

Requires two steps.

---

### InnoDB

Primary-key lookup:

```text id="w5g9rq"
Clustered Index
       |
Row Data
```

Single traversal.

Advantage:

```text id="c2w7ls"
Faster Primary-Key Access
```

---

## 7.9 Secondary Index Access

### PostgreSQL

```text id="cl29g4"
Secondary Index
       |
Heap Tuple
```

---

### InnoDB

```text id="z8x9lo"
Secondary Index
       |
Primary Key
       |
Clustered Index
       |
Row Data
```

Requires a double lookup.

---

## 7.10 Design Philosophy

### PostgreSQL Philosophy

Prioritizes:

* Flexibility
* Concurrency
* Extensibility
* Advanced SQL features

Accepts:

* Storage overhead
* VACUUM maintenance

in exchange for powerful MVCC behavior.

---

### InnoDB Philosophy

Prioritizes:

* Efficient OLTP access
* Clustered storage
* Compact data organization
* Fast primary-key operations

Accepts:

* More complex locking
* Undo infrastructure
* Secondary index lookup overhead

in exchange for efficient transactional performance.

---

## 7.11 Summary Comparison

| Feature             | PostgreSQL                | InnoDB                         |
| ------------------- | ------------------------- | ------------------------------ |
| Table Storage       | Heap Files                | Clustered Index                |
| Primary Key Lookup  | Index + Heap              | Direct Through Clustered Index |
| MVCC                | Tuple Versioning          | Undo Logs                      |
| Cleanup             | VACUUM                    | Purge                          |
| Cache               | Shared Buffers            | Buffer Pool                    |
| Logging             | WAL                       | Redo Logs                      |
| Historical Versions | Heap Tuples               | Undo Records                   |
| Page Size           | 8 KB                      | 16 KB                          |
| Range Locking       | Limited                   | Gap / Next-Key Locks           |
| Strength            | Concurrency & Flexibility | OLTP & Primary-Key Performance |

Both systems are highly capable transactional databases, but they achieve their goals through different architectural decisions. PostgreSQL favors flexibility and sophisticated MVCC mechanisms, while InnoDB favors clustered storage and highly optimized transactional access patterns.

# 7. PostgreSQL vs InnoDB

Although PostgreSQL and InnoDB are both transactional database systems that provide ACID guarantees, they use fundamentally different internal architectures.

Many of their design choices reflect different priorities regarding storage organization, concurrency control, transaction management, and recovery.

Understanding these differences helps explain why the two systems behave differently under various workloads.

---

## 7.1 Storage Organization

### PostgreSQL

PostgreSQL stores tables as heap relations.

```text id="a8s5cx"
Heap Table
      +
Separate Indexes
```

Rows are stored independently of indexes.

Indexes contain references to heap tuples.

Advantages:

* Flexible storage organization
* Easy support for multiple index types
* Efficient handling of updates

Trade-off:

* Additional lookup required after index traversal

---

### InnoDB

InnoDB uses clustered storage.

```text id="7u7rpt"
Primary Key B-Tree
        =
Table Storage
```

Rows are stored directly inside the clustered index.

Advantages:

* Fast primary-key access
* Improved locality
* Efficient range scans

Trade-off:

* More expensive primary-key modifications
* Secondary indexes require additional lookups

---

## 7.2 Update Processing

One of the most significant architectural differences is how updates are performed.

### PostgreSQL

Updates create new tuple versions.

```text id="m9n7xg"
Old Tuple
     |
New Tuple
```

The old version remains in the heap.

Advantages:

* Simplified MVCC implementation
* Strong snapshot isolation

Trade-off:

* Dead tuples accumulate
* VACUUM required

---

### InnoDB

InnoDB updates rows in place while storing previous versions in undo logs.

```text id="8v3b2m"
Current Row
      |
Undo Record
```

Advantages:

* Compact storage
* Less table bloat

Trade-off:

* More complex undo infrastructure

---

## 7.3 MVCC Implementation

Both systems support MVCC but implement it differently.

### PostgreSQL

MVCC based on tuple versioning.

Tuple metadata:

```text id="d0fh7z"
xmin
xmax
```

Historical versions remain inside table pages.

Example:

```text id="4b95vw"
Version 1
Version 2
Version 3
```

stored together.

---

### InnoDB

MVCC based on undo records.

```text id="p65qgn"
Current Version
       |
Undo Chain
       |
Older Versions
```

Historical versions are reconstructed from undo information.

---

## 7.4 Cleanup Mechanisms

### PostgreSQL

Uses:

```sql id="x2g4oq"
VACUUM
```

Responsibilities:

* Remove dead tuples
* Reclaim storage
* Prevent transaction ID wraparound

---

### InnoDB

Uses:

```text id="gmxol7"
Purge Process
```

Responsibilities:

* Remove obsolete undo records
* Reclaim MVCC history

---

### Architectural Difference

PostgreSQL cleans:

```text id="p7rqyt"
Dead Tuples
```

InnoDB cleans:

```text id="4v6r3m"
Undo Records
```

---

## 7.5 Recovery Mechanisms

Both systems use write-ahead logging principles.

### PostgreSQL

Uses:

```text id="j0kltt"
WAL
```

Workflow:

```text id="04z6v7"
WAL Record
      |
Flush WAL
      |
Write Data Page
```

---

### InnoDB

Uses:

```text id="r0mrzw"
Redo Log
```

Workflow:

```text id="8stwrh"
Redo Record
       |
Flush Redo Log
       |
Write Data Page
```

Both approaches provide durability and crash recovery.

---

## 7.6 Memory Management

### PostgreSQL

Primary cache:

```text id="d54xkh"
Shared Buffers
```

Default page size:

```text id="sxh19h"
8 KB
```

Managed by PostgreSQL's Buffer Manager.

---

### InnoDB

Primary cache:

```text id="khvxmu"
Buffer Pool
```

Default page size:

```text id="pn5q4h"
16 KB
```

Managed by InnoDB.

Observed in the experiment:

```text id="yr1xdr"
Buffer Pool Size = 128 MB
Hit Rate = 1000/1000
```

Both systems maintain dedicated database caches rather than relying entirely on the operating system.

---

## 7.7 Locking Strategy

### PostgreSQL

Relies heavily on:

```text id="x4cpm3"
MVCC
```

and lightweight row locking.

Phantom protection is largely achieved through snapshot isolation and higher-level locking mechanisms.

---

### InnoDB

Uses:

```text id="mqyrxn"
MVCC
+
Row Locks
+
Gap Locks
+
Next-Key Locks
```

Additional locking prevents phantom reads during range queries.

---

## 7.8 Primary Key Access

### PostgreSQL

Primary-key lookup:

```text id="v3pt4x"
Index
   |
Heap Lookup
```

Requires two steps.

---

### InnoDB

Primary-key lookup:

```text id="w5g9rq"
Clustered Index
       |
Row Data
```

Single traversal.

Advantage:

```text id="c2w7ls"
Faster Primary-Key Access
```

---

## 7.9 Secondary Index Access

### PostgreSQL

```text id="cl29g4"
Secondary Index
       |
Heap Tuple
```

---

### InnoDB

```text id="z8x9lo"
Secondary Index
       |
Primary Key
       |
Clustered Index
       |
Row Data
```

Requires a double lookup.

---

## 7.10 Design Philosophy

### PostgreSQL Philosophy

Prioritizes:

* Flexibility
* Concurrency
* Extensibility
* Advanced SQL features

Accepts:

* Storage overhead
* VACUUM maintenance

in exchange for powerful MVCC behavior.

---

### InnoDB Philosophy

Prioritizes:

* Efficient OLTP access
* Clustered storage
* Compact data organization
* Fast primary-key operations

Accepts:

* More complex locking
* Undo infrastructure
* Secondary index lookup overhead

in exchange for efficient transactional performance.

---

## 7.11 Summary Comparison

| Feature             | PostgreSQL                | InnoDB                         |
| ------------------- | ------------------------- | ------------------------------ |
| Table Storage       | Heap Files                | Clustered Index                |
| Primary Key Lookup  | Index + Heap              | Direct Through Clustered Index |
| MVCC                | Tuple Versioning          | Undo Logs                      |
| Cleanup             | VACUUM                    | Purge                          |
| Cache               | Shared Buffers            | Buffer Pool                    |
| Logging             | WAL                       | Redo Logs                      |
| Historical Versions | Heap Tuples               | Undo Records                   |
| Page Size           | 8 KB                      | 16 KB                          |
| Range Locking       | Limited                   | Gap / Next-Key Locks           |
| Strength            | Concurrency & Flexibility | OLTP & Primary-Key Performance |

Both systems are highly capable transactional databases, but they achieve their goals through different architectural decisions. PostgreSQL favors flexibility and sophisticated MVCC mechanisms, while InnoDB favors clustered storage and highly optimized transactional access patterns.

# 9. Key Learnings

This study provided a detailed understanding of the internal architecture of the InnoDB storage engine and the design decisions that make it suitable for transactional database workloads.

One of the most important observations was that InnoDB organizes table data around clustered indexes. Unlike PostgreSQL's heap-based storage model, InnoDB stores rows directly inside the primary key B-Tree, allowing very efficient primary-key lookups and range scans.

The investigation also demonstrated the importance of the Buffer Pool. The observed buffer pool hit rate of 1000/1000 showed that most page requests were satisfied directly from memory, significantly reducing disk I/O and improving performance. This highlights the critical role of memory management in modern database systems.

The study further revealed how InnoDB implements transaction processing through the coordinated use of undo logs, redo logs, MVCC, and checkpointing. Undo logs provide rollback capability and support historical row versions, while redo logs ensure durability and crash recovery. Together, these mechanisms allow InnoDB to maintain full ACID compliance.

Another important learning was the role of locking in maintaining consistency. Row-level locks, gap locks, and next-key locks enable high concurrency while preventing anomalies such as phantom reads. This demonstrates how InnoDB combines MVCC with explicit locking mechanisms to achieve transaction isolation.

The comparison with PostgreSQL highlighted that both systems solve similar problems using different architectural approaches. PostgreSQL emphasizes heap-based storage and tuple versioning, while InnoDB emphasizes clustered storage and undo-based MVCC. Neither approach is universally superior; each represents a different set of engineering trade-offs.

Overall, this study demonstrates that database performance, reliability, and scalability emerge from the interaction of multiple subsystems including storage organization, memory management, transaction processing, logging, and concurrency control. Understanding these components provides valuable insight into the design of modern relational database systems.

# 10. References

## Official MySQL Documentation

1. MySQL 8.4 Reference Manual
   https://dev.mysql.com/doc/refman/8.4/en/

2. InnoDB Storage Engine Documentation
   https://dev.mysql.com/doc/refman/8.4/en/innodb-storage-engine.html

3. InnoDB Architecture
   https://dev.mysql.com/doc/refman/8.4/en/innodb-architecture.html

4. InnoDB Buffer Pool
   https://dev.mysql.com/doc/refman/8.4/en/innodb-buffer-pool.html

5. InnoDB Clustered and Secondary Indexes
   https://dev.mysql.com/doc/refman/8.4/en/innodb-index-types.html

6. InnoDB Multi-Version Concurrency Control (MVCC)
   https://dev.mysql.com/doc/refman/8.4/en/innodb-multi-versioning.html

7. InnoDB Locking and Transaction Model
   https://dev.mysql.com/doc/refman/8.4/en/innodb-locking.html

8. InnoDB Redo Logging
   https://dev.mysql.com/doc/refman/8.4/en/innodb-redo-log.html

9. InnoDB Undo Logs
   https://dev.mysql.com/doc/refman/8.4/en/innodb-undo-logs.html

10. InnoDB Checkpoints and Recovery
    https://dev.mysql.com/doc/refman/8.4/en/innodb-recovery.html

11. SHOW ENGINE INNODB STATUS Documentation
    https://dev.mysql.com/doc/refman/8.4/en/show-engine.html

## MySQL Source Code

12. MySQL Server Source Repository
    https://github.com/mysql/mysql-server

13. InnoDB Storage Engine Source Code
    storage/innobase/

14. InnoDB Buffer Pool Implementation
    storage/innobase/buf/

15. InnoDB Transaction Processing Components
    storage/innobase/trx/

## Experimental Work

16. MySQL 8.4.7 Environment Used For This Study

17. InnoDB Buffer Pool Analysis Performed Using:

```sql id="8qwy2n"
SHOW ENGINE INNODB STATUS\G
```

18. Clustered Index Analysis Performed Using:

```sql id="zl39ku"
SHOW INDEX FROM customers;
```

19. Buffer Pool Configuration Analysis Performed Using:

```sql id="pb5t7d"
SHOW VARIABLES LIKE 'innodb_buffer_pool_size';
```

20. Custom Experimental Dataset Consisting of customers and orders Tables Implemented Using the InnoDB Storage Engine
