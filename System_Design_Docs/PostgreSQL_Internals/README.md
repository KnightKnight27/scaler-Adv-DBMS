# 1. Problem Background

PostgreSQL is one of the most widely used open-source relational database management systems. It originated from the POSTGRES research project at the University of California, Berkeley, and has evolved into a production-grade database platform used by enterprises, cloud providers, financial institutions, and large-scale web applications.

Unlike lightweight embedded databases, PostgreSQL is designed to support high concurrency, strong transactional guarantees, crash recovery, and complex analytical workloads. Achieving these goals requires sophisticated internal mechanisms for memory management, indexing, transaction processing, and durability.

The purpose of this study is to explore several core PostgreSQL subsystems and understand how they interact to provide reliable and efficient database operations. Particular focus is placed on the Buffer Manager, B-Tree indexes, Multi-Version Concurrency Control (MVCC), Write-Ahead Logging (WAL), and the Query Planner. Understanding these components helps explain why PostgreSQL behaves the way it does under real workloads and why it is widely adopted for enterprise applications.

# 2. PostgreSQL Architecture Overview

PostgreSQL follows a client-server architecture in which a central database server manages storage, memory, transactions, and concurrency for multiple clients.

```text id="n75rlf"
                 Clients
                     |
                     v
+--------------------------------+
|          Postmaster            |
+--------------------------------+
                     |
       +-------------+-------------+
       |             |             |
       v             v             v
 Backend 1     Backend 2     Backend N
       \             |             /
        \            |            /
         +----------------------+
         |    Shared Buffers    |
         +----------------------+
                    |
                    v
         +----------------------+
         | Heap Files + Indexes |
         +----------------------+
                    |
                    v
               WAL Files
```

Major components:

### Postmaster

The postmaster process accepts incoming client connections and creates backend processes for each session.

### Backend Processes

Each client is assigned a dedicated backend process responsible for parsing queries, planning execution strategies, and accessing data pages.

### Shared Buffers

Shared buffers form PostgreSQL's primary cache and are managed by the Buffer Manager.

### Storage Manager

Responsible for reading and writing heap pages and index pages on disk.

### WAL Subsystem

Ensures durability by recording changes before modified pages are written to permanent storage.

### Background Processes

PostgreSQL continuously runs maintenance processes including:

* Checkpointer
* Background Writer
* WAL Writer
* Autovacuum Launcher
* Logical Replication Launcher

Together, these components provide the foundation for PostgreSQL's scalability, reliability, and concurrency model.

# 3. Buffer Manager

The Buffer Manager is responsible for caching database pages in memory and minimizing expensive disk I/O operations. Instead of reading data directly from disk every time a query executes, PostgreSQL stores frequently accessed pages in a shared memory region called **Shared Buffers**.

Every table page, index page, and catalog page accessed by a query passes through the Buffer Manager.

---

## 3.1 Shared Buffers

Shared Buffers form PostgreSQL's primary in-memory cache.

```text id="2poh4e"
           Query
             |
             v
      Buffer Manager
             |
      +-------------+
      | Shared      |
      | Buffers     |
      +-------------+
       |         |
 Buffer Hit   Buffer Miss
       |         |
       v         v
     Memory     Disk
```

When a query requests a page:

### Buffer Hit

If the page already exists in Shared Buffers:

```text id="n7ew2g"
Requested Page
      |
      v
Shared Buffers
      |
      v
Returned Immediately
```

No disk access is required.

---

### Buffer Miss

If the page is not present:

```text id="sjtsf9"
Requested Page
      |
      v
Disk Read
      |
      v
Shared Buffers
      |
      v
Query
```

The page must first be loaded from disk before it can be accessed.

Since disk I/O is significantly slower than memory access, maintaining a high buffer hit ratio is critical for performance.

---

## 3.2 Page Reads and Writes

PostgreSQL stores tables and indexes as fixed-size pages.

Default page size:

```text id="mtn12r"
8192 bytes (8 KB)
```

When a query accesses a row:

1. Determine which page contains the row.
2. Check Shared Buffers.
3. Load page from disk if necessary.
4. Return tuple from the page.

```text id="m58vbh"
Heap File
   |
   +---- Page 1
   +---- Page 2
   +---- Page 3
```

The Buffer Manager operates at the page level rather than the row level.

---

### Reading a Page

Example:

```sql id="9ttjlwm"
SELECT * FROM customers
WHERE customer_id = 100;
```

PostgreSQL:

1. Locates the relevant page.
2. Checks Shared Buffers.
3. Reads from disk only if necessary.
4. Returns the tuple.

Even if a query requires a single row, the entire 8 KB page is loaded into memory.

---

### Writing a Page

When a row is modified:

```sql id="v7d6dg"
UPDATE customers
SET city = 'Mumbai'
WHERE customer_id = 100;
```

PostgreSQL does not immediately write the page to disk.

Instead:

```text id="44tlfo"
Page Modified
      |
      v
Marked Dirty
      |
      v
Stored in Shared Buffers
      |
      v
Written Later
```

This delayed writing improves performance by reducing disk operations.

---

## 3.3 Dirty Pages

A page modified in memory but not yet written to disk is called a **dirty page**.

```text id="sxzt7u"
Disk Page
     |
Modified In Memory
     |
Dirty Page
```

Dirty pages remain in Shared Buffers until one of the following occurs:

* Checkpoint
* Buffer eviction
* Background writer flush

This mechanism reduces write amplification because multiple modifications can be combined into a single disk write.

---

## 3.4 Buffer Replacement

Shared Buffers have finite capacity.

Eventually new pages must replace older pages.

PostgreSQL uses a variation of the **Clock-Sweep Algorithm**.

### Why Not Pure LRU?

Traditional Least Recently Used (LRU) requires maintaining precise ordering of page accesses.

```text id="r5w2xu"
Most Recent
     |
     v
[ P1 P2 P3 P4 ]
```

Updating this structure on every access would create excessive overhead.

---

### Clock-Sweep Algorithm

Each page maintains a usage counter.

```text id="vytf3x"
Page A -> usage_count = 5
Page B -> usage_count = 2
Page C -> usage_count = 0
```

A circular pointer sweeps through buffer frames.

```text id="9xpr52"
      +------------------+
      |                  |
      v                  |
 [A] [B] [C] [D] [E]
```

Pages with low usage counts become eviction candidates.

Frequently accessed pages survive multiple sweeps.

Advantages:

* Lower overhead than LRU
* Good approximation of recency
* Scales well under high concurrency

---

## 3.5 Interaction with Query Execution

The Buffer Manager directly affects query performance.

Consider a large table scan:

```sql id="7f6vst"
SELECT COUNT(*)
FROM orders;
```

First execution:

```text id="hsmh2h"
Disk Reads Required
```

Subsequent executions:

```text id="gmb2tp"
Pages Already Cached
```

Execution becomes significantly faster because pages remain in Shared Buffers.

This behavior was observed during the experiments where repeated queries executed more quickly after the initial run.

---

## 3.6 Relationship with Other PostgreSQL Components

The Buffer Manager works closely with several subsystems.

### Storage Manager

Reads and writes physical pages.

### WAL Subsystem

Before dirty pages can be written:

```text id="7z4x0g"
WAL Record
     |
Flush WAL
     |
Write Page
```

This ensures durability.

### Query Executor

Requests pages needed by scans, joins, and index lookups.

### Checkpointer

Periodically flushes dirty pages from Shared Buffers to disk.

---

## 3.7 Why Shared Buffers Matter

Without Shared Buffers:

```text id="k7y0s7"
Query
  |
  v
Disk Access Every Time
```

With Shared Buffers:

```text id="ih9w5o"
Query
  |
  v
Memory Access
```

Since memory access is orders of magnitude faster than disk access, Shared Buffers are one of the primary reasons PostgreSQL can efficiently support large workloads.

The Buffer Manager therefore acts as the central bridge between query execution and physical storage, ensuring that frequently accessed pages remain in memory while minimizing disk I/O.

# 4. B-Tree Index Implementation

PostgreSQL uses B-Trees as its default index structure because they provide efficient search, insertion, deletion, and range-query performance.

A B-Tree is a balanced tree structure in which all leaf nodes remain at the same depth. This guarantees predictable performance even as the index grows to millions of entries.

For a B-Tree with N entries:

```text
Search  : O(log N)
Insert  : O(log N)
Delete  : O(log N)
```

This logarithmic behavior makes B-Trees highly suitable for database indexing.

---

## 4.1 Index Structure

A PostgreSQL B-Tree consists of multiple page types.

```text
                Root
                  |
          +-------+-------+
          |               |
      Internal       Internal
          |               |
      +---+---+       +---+---+
      |       |       |       |
    Leaf    Leaf    Leaf    Leaf
```

### Root Page

The root page is the entry point for every index lookup.

### Internal Pages

Internal pages contain separator keys that guide the search toward the correct subtree.

### Leaf Pages

Leaf pages store actual index entries.

Each leaf entry contains:

```text
Indexed Key
     +
Tuple Identifier (TID)
```

The TID points to the row's physical location inside a heap page.

---

## 4.2 Index Page Layout

Every B-Tree page is stored as a standard PostgreSQL page.

```text
+----------------------+
| Page Header          |
+----------------------+
| Item Pointer Array   |
+----------------------+
| Free Space           |
+----------------------+
| Index Tuples         |
+----------------------+
```

Index tuples contain:

```text
(Key Value, TID)
```

Example:

```text
(100, Page 52 Tuple 7)
(105, Page 53 Tuple 2)
(110, Page 54 Tuple 9)
```

The index stores references to rows rather than storing the row data itself.

This keeps indexes compact and efficient.

---

## 4.3 Search Path

Consider the query:

```sql
SELECT *
FROM customers
WHERE customer_id = 5000;
```

If an index exists on `customer_id`, PostgreSQL performs the following steps:

### Step 1

Start at the root page.

```text
          [5000]
         /      \
```

### Step 2

Compare the search key against separator values.

### Step 3

Follow the appropriate child pointer.

```text
Root
  |
  v
Internal Page
  |
  v
Leaf Page
```

### Step 4

Locate the matching index entry.

```text
5000 -> TID(123,4)
```

### Step 5

Fetch the tuple from the heap page.

```text
Index
   |
   v
Heap Page
```

Because only a few pages are traversed, index lookups remain extremely fast even for large tables.

---

## 4.4 Index Scans

When the PostgreSQL planner determines that an index is beneficial, it performs an Index Scan.

Example:

```sql
SELECT *
FROM orders
WHERE customer_id = 500;
```

Execution flow:

```text
Index Root
    |
    v
Leaf Entry
    |
    v
Heap Tuple
```

Advantages:

* Avoids scanning the entire table
* Reduces disk I/O
* Improves lookup performance

However, index scans are not always chosen.

For large result sets, PostgreSQL may prefer a Sequential Scan because reading many index entries and heap pages can be more expensive than scanning the table once.

This behavior was observed in the experiment where PostgreSQL chose Sequential Scans despite indexes being available.

---

## 4.5 Insert Operations

When a new row is inserted:

```sql
INSERT INTO customers
VALUES (...);
```

PostgreSQL must also update every relevant index.

Steps:

### Step 1

Traverse the B-Tree.

```text
Root
  |
Internal
  |
Leaf
```

### Step 2

Locate the target leaf page.

### Step 3

Insert the new key in sorted order.

Example:

Before:

```text
10 20 30 50
```

After inserting 40:

```text
10 20 30 40 50
```

Maintaining sorted order is critical because it preserves efficient search performance.

---

## 4.6 Page Splits

A page has finite capacity.

Eventually a leaf page becomes full.

Example:

```text
10 20 30 40
```

Insert:

```text
25
```

If insufficient space exists:

```text
Page Full
```

PostgreSQL performs a page split.

Before:

```text
[10 20 30 40]
```

After:

```text
[10 20]
      \
      [25 30 40]
```

The parent page receives a new separator key.

```text
          [25]
         /    \
[10 20]      [25 30 40]
```

This process maintains tree balance.

---

## 4.7 Why B-Trees Remain Balanced

One of the most important properties of B-Trees is that all leaf pages remain at the same depth.

Example:

```text
Bad Tree

Root
 |
Leaf
 |
Leaf
 |
Leaf
```

This structure would produce poor performance.

Instead PostgreSQL continuously splits pages and propagates separator keys upward.

Result:

```text
Balanced Tree

          Root
         /    \
      Node    Node
      /  \    /  \
   Leaf Leaf Leaf Leaf
```

The height grows slowly even for very large indexes.

---

## 4.8 Connection to the Experiment

Indexes were created during the experiment:

```sql
CREATE INDEX idx_orders_customer
ON orders(customer_id);

CREATE INDEX idx_orders_product
ON orders(product_id);
```

Despite these indexes, PostgreSQL selected Sequential Scans.

Why?

The query returned approximately:

```text
64,790 rows
```

out of:

```text
100,000 rows
```

This means nearly 65% of the table satisfied the condition.

Using an index would require:

1. Traversing the B-Tree.
2. Reading many index entries.
3. Performing thousands of heap lookups.

The planner estimated that a Sequential Scan would be cheaper.

This demonstrates an important principle:

> PostgreSQL does not use indexes simply because they exist. The planner chooses the access method with the lowest estimated cost based on available statistics.

The effectiveness of PostgreSQL's query planner therefore depends heavily on accurate table statistics and cost estimation.

# 5. Multi-Version Concurrency Control (MVCC)

One of PostgreSQL's most important design features is Multi-Version Concurrency Control (MVCC).

Traditional locking systems often force readers and writers to block each other.

Example:

```text id="2ijjup"
Transaction A
     |
Updating Row
     |
     v
Transaction B
Must Wait
```

As concurrency increases, lock contention becomes a major performance bottleneck.

PostgreSQL solves this problem using MVCC.

Instead of modifying rows in place, PostgreSQL creates new tuple versions while preserving older versions that may still be visible to other transactions.

This allows:

* Readers to continue reading old versions.
* Writers to create new versions.
* Minimal blocking between transactions.

---

## 5.1 Heap Tuple Versioning

Every row in PostgreSQL is stored as a heap tuple.

A tuple contains:

```text id="s4u5c9"
User Data
+
xmin
+
xmax
```

Example:

```text id="9r7zh3"
Customer ID = 10
Name = Alice

xmin = 100
xmax = NULL
```

Here:

```text id="3n8qkh"
xmin
```

represents the transaction that created the row.

```text id="4db4jv"
xmax
```

represents the transaction that deleted or replaced the row.

---

### Initial Insert

Transaction 100 inserts a row.

```text id="2l4e6k"
Customer = Alice

xmin = 100
xmax = NULL
```

The row is visible because it has not been deleted.

---

### Update Operation

Suppose transaction 200 updates the row.

```sql id="hgrh0n"
UPDATE customers
SET city = 'Mumbai'
WHERE customer_id = 10;
```

Many databases would overwrite the row.

PostgreSQL instead creates a new tuple version.

Before:

```text id="z97w7f"
Tuple Version 1

Alice
xmin=100
xmax=NULL
```

After:

```text id="wv5h8r"
Tuple Version 1
Alice
xmin=100
xmax=200

Tuple Version 2
Alice
xmin=200
xmax=NULL
```

The old version remains available for transactions that started earlier.

---

## 5.2 Understanding xmin and xmax

Every tuple contains transaction visibility information.

### xmin

Transaction that created the tuple.

Example:

```text id="e4wqih"
xmin = 100
```

means transaction 100 inserted the row.

---

### xmax

Transaction that deleted or replaced the tuple.

Example:

```text id="b6dd4s"
xmax = 200
```

means transaction 200 invalidated this version.

---

Example:

```text id="wtgd5p"
Tuple

xmin = 100
xmax = 200
```

Interpretation:

```text id="c2obk7"
Visible after transaction 100
Invisible after transaction 200
```

These fields are the foundation of PostgreSQL's MVCC implementation.

---

## 5.3 Visibility Rules

Not every transaction sees every tuple version.

PostgreSQL applies visibility rules using transaction snapshots.

Example:

### Transaction Timeline

```text id="3g7z2m"
T1 begins
T2 updates row
T2 commits
T1 continues
```

Under MVCC:

```text id="r1c61g"
T1 still sees old version
```

because its snapshot was taken before T2 committed.

This guarantees a consistent view of the database.

---

### Visibility Example

Assume:

```text id="d6q8ra"
Old Tuple

xmin = 100
xmax = 200
```

```text id="jvw2qt"
New Tuple

xmin = 200
xmax = NULL
```

Transaction started before 200 committed:

```text id="uj44p6"
Sees Old Tuple
```

Transaction started after 200 committed:

```text id="vlxw64"
Sees New Tuple
```

The same physical page therefore contains multiple row versions simultaneously.

---

## 5.4 Snapshot Isolation

When a transaction begins, PostgreSQL creates a snapshot.

Example:

```sql id="j7kz0s"
BEGIN;
```

Snapshot:

```text id="gk65pi"
Visible Transactions
Active Transactions
Committed Transactions
```

The transaction continues using that snapshot until completion.

Benefits:

* Consistent reads
* No dirty reads
* Reduced lock contention

Example:

```text id="5rpxr5"
Transaction A
Reads Balance = 1000

Transaction B
Updates Balance = 500

Transaction A
Still Reads 1000
```

This behavior is known as snapshot isolation.

---

## 5.5 Why Readers Do Not Block Writers

Traditional systems:

```text id="3pccod"
Reader
  |
Lock
  |
Writer Waits
```

PostgreSQL:

```text id="vzjdcx"
Reader
   |
Old Version

Writer
   |
New Version
```

Both transactions proceed simultaneously.

Advantages:

* High throughput
* Reduced contention
* Better scalability

This is one of the primary reasons PostgreSQL performs well under concurrent workloads.

---

## 5.6 Dead Tuples

A consequence of MVCC is that old tuple versions remain in heap pages.

Example:

```text id="slq2hh"
Version 1
Version 2
Version 3
```

Only one version may currently be visible.

The others become:

```text id="ywdbxj"
Dead Tuples
```

These consume storage space until removed.

---

## 5.7 Why VACUUM Is Necessary

Since PostgreSQL keeps old versions:

```text id="9pynax"
Heap File
    |
Dead Tuples
    |
More Dead Tuples
```

Storage would continuously grow without cleanup.

PostgreSQL uses:

```sql id="h45c7n"
VACUUM
```

to reclaim space.

VACUUM:

* Removes dead tuples
* Updates visibility information
* Improves storage efficiency
* Prevents transaction ID wraparound

Without VACUUM, table bloat would eventually degrade performance.

---

## 5.8 MVCC and the Experiment

During the experiment:

```sql id="nuwvl6"
SELECT
    c.customer_name,
    p.product_name,
    o.quantity
FROM orders o
JOIN customers c
JOIN products p
...
```

Multiple users could theoretically execute this query while other transactions perform inserts, updates, or deletes.

MVCC ensures:

```text id="v0u3es"
Readers
     |
Do Not Block
     |
Writers
```

Each transaction sees a consistent snapshot of the database.

This capability is a major advantage over simpler locking models and is one of the key architectural reasons PostgreSQL is widely used in multi-user systems.

---

## 5.9 Summary

MVCC is one of PostgreSQL's defining architectural features.

It enables:

* High concurrency
* Consistent reads
* Snapshot isolation
* Reduced lock contention

The trade-off is additional storage overhead caused by multiple tuple versions and the need for VACUUM to reclaim space.

This represents a classic database engineering trade-off: PostgreSQL accepts additional storage and maintenance complexity in exchange for significantly improved concurrency and scalability.

# 6. Write-Ahead Logging (WAL)

Durability is one of the core properties of ACID transactions.

The durability guarantee states:

> Once a transaction commits, its changes must survive crashes, power failures, and system restarts.

PostgreSQL achieves this through a mechanism called **Write-Ahead Logging (WAL)**.

Instead of writing modified data pages directly to disk immediately, PostgreSQL first records changes in a sequential log known as the WAL.

This design significantly improves both performance and reliability.

---

## 6.1 Why WAL Is Necessary

Consider the following transaction:

```sql id="7wkh90"
UPDATE accounts
SET balance = balance - 100
WHERE account_id = 1;
```

Suppose PostgreSQL updates the data page directly.

```text id="v0nps0"
Data Page Updated
```

If the system crashes before the modified page reaches disk:

```text id="3h7rsv"
Crash
```

the update may be lost.

This violates durability.

PostgreSQL therefore follows a different approach.

---

## 6.2 Write-Ahead Logging Principle

The fundamental rule is:

> WAL records must be written to stable storage before modified data pages are written.

Workflow:

```text id="xpxm5f"
Transaction
      |
      v
Generate WAL Record
      |
      v
Flush WAL
      |
      v
Commit
      |
      v
Write Data Page Later
```

As long as the WAL record is safely stored, PostgreSQL can reconstruct the change even if the actual data page has not yet been written.

---

## 6.3 WAL Records

Every change made to a database page generates a WAL record.

Examples:

```text id="96m59d"
INSERT
UPDATE
DELETE
INDEX MODIFICATION
```

Example:

```sql id="5bzydr"
INSERT INTO customers
VALUES (1,'Alice');
```

Produces a WAL record describing:

```text id="8t5v6f"
Page Modified
Tuple Inserted
Location Changed
```

The WAL does not store SQL statements.

Instead, it stores low-level physical changes that can be replayed during recovery.

---

## 6.4 WAL File Organization

WAL records are stored in:

```text id="5w7a0f"
pg_wal/
```

inside the PostgreSQL data directory.

Example:

```text id="rcc4l5"
PGDATA/
   |
   +-- pg_wal/
```

WAL files are written sequentially.

Advantages:

* Fast disk writes
* Reduced random I/O
* Efficient crash recovery

Sequential writes are significantly faster than repeatedly updating random data pages.

---

## 6.5 Commit Processing

Consider:

```sql id="o1gdz5"
BEGIN;

UPDATE accounts
SET balance = balance - 100
WHERE account_id = 1;

COMMIT;
```

PostgreSQL performs:

```text id="clqvhi"
1. Modify Page In Memory
2. Generate WAL Record
3. Flush WAL
4. Commit Transaction
```

Only after WAL is safely stored does PostgreSQL report:

```text id="29l3cz"
COMMIT
```

to the client.

This guarantees durability.

---

## 6.6 Checkpointing

Eventually PostgreSQL must write dirty pages from Shared Buffers to disk.

This process is coordinated through checkpoints.

```text id="2fjs9w"
Shared Buffers
      |
Dirty Pages
      |
Checkpoint
      |
Disk
```

A checkpoint records:

```text id="k9lcrs"
Recovery Starting Point
```

and ensures modified pages are written to permanent storage.

Benefits:

* Reduces crash recovery time
* Limits WAL replay requirements
* Controls buffer usage

---

## 6.7 Crash Recovery

Suppose:

```text id="q5aw3g"
Checkpoint
      |
Transaction A
      |
Transaction B
      |
Crash
```

Some modified pages may never have reached disk.

Recovery proceeds as follows:

### Step 1

Locate the most recent checkpoint.

```text id="z3j8s5"
Checkpoint Record
```

### Step 2

Read WAL records generated after that checkpoint.

```text id="1w3j2k"
WAL Replay
```

### Step 3

Reapply changes.

```text id="mfph2w"
Database Restored
```

Because WAL records contain all necessary modifications, PostgreSQL can reconstruct the correct database state.

---

## 6.8 WAL and the Buffer Manager

The Buffer Manager and WAL subsystem work together.

When a page becomes dirty:

```text id="4yx6a8"
Page Modified
      |
Dirty Buffer
```

Before that page can be written:

```text id="t6e5wy"
Flush WAL
      |
Write Page
```

This rule is known as the **WAL Protocol**.

```text id="fnj2ax"
WAL First
Data Page Second
```

It is one of the most important correctness guarantees inside PostgreSQL.

---

## 6.9 WAL and Performance

At first glance WAL appears to add additional work.

However it often improves performance.

Without WAL:

```text id="hj6zmu"
Random Page Writes
Random Page Writes
Random Page Writes
```

With WAL:

```text id="4v8v9w"
Sequential WAL Writes
```

Benefits:

* Better disk throughput
* Reduced random I/O
* Efficient batching
* Faster transaction processing

This is one reason PostgreSQL scales well under write-heavy workloads.

---

## 6.10 Summary

Write-Ahead Logging is the foundation of PostgreSQL's durability model.

Key responsibilities include:

* Recording changes before page writes
* Supporting transaction durability
* Enabling crash recovery
* Coordinating with checkpoints
* Improving write performance through sequential logging

The combination of WAL, Shared Buffers, and Checkpointing allows PostgreSQL to provide strong ACID guarantees while maintaining high performance and reliability.

# 7. EXPLAIN ANALYZE Experiment

To understand how PostgreSQL's internal components interact during query execution, a practical experiment was conducted using three related tables: customers, products, and orders.

The objective was to observe how the query planner chooses an execution strategy and how collected statistics influence those decisions.

---

## 7.1 Experimental Setup

Three tables were created:

```sql id="v7l3i1"
customers
products
orders
```

Dataset sizes:

| Table     |    Rows |
| --------- | ------: |
| customers |  10,000 |
| products  |   1,000 |
| orders    | 100,000 |

Indexes were created on the foreign key columns:

```sql id="0uh6h0"
CREATE INDEX idx_orders_customer
ON orders(customer_id);

CREATE INDEX idx_orders_product
ON orders(product_id);
```

Statistics were then collected using:

```sql id="6mwrr0"
ANALYZE;
```

This step is important because PostgreSQL relies heavily on collected statistics when estimating query costs.

---

## 7.2 Query Under Analysis

The following multi-table join query was executed:

```sql id="5tr7n8"
EXPLAIN ANALYZE
SELECT
    c.customer_name,
    p.product_name,
    o.quantity
FROM orders o
JOIN customers c
    ON o.customer_id = c.customer_id
JOIN products p
    ON o.product_id = p.product_id
WHERE o.quantity >= 5;
```

This query joins three tables and applies a filter on the orders table.

---

## 7.3 Execution Plan

PostgreSQL produced the following high-level plan:

```text id="v7wwj0"
Hash Join
├── Hash Join
│   ├── Seq Scan on orders
│   └── Hash on customers
└── Hash on products
```

Execution summary:

```text id="5hmvx0"
Planning Time: 4.983 ms
Execution Time: 105.415 ms
Rows Returned: 64,790
```

The planner selected:

* Sequential Scan on orders
* Sequential Scan on customers
* Sequential Scan on products
* Two Hash Joins

No Index Scan operations were used.

---

## 7.4 Analysis of the Sequential Scan

The first operation was:

```text id="a29i7l"
Seq Scan on orders
```

Plan output:

```text id="j6fhv7"
Rows Returned: 64,790
Rows Removed by Filter: 35,210
```

The predicate:

```sql id="p2g2f6"
quantity >= 5
```

matched approximately:

```text id="q8n1ol"
64.8%
```

of all rows.

This means the majority of the table satisfied the condition.

---

### Why PostgreSQL Avoided an Index

Although indexes existed on:

```text id="l4v5xg"
customer_id
product_id
```

they were not useful for the filter condition:

```sql id="z2ny1x"
quantity >= 5
```

Furthermore, because nearly 65% of rows qualified, PostgreSQL estimated that reading the entire table once would be cheaper than:

1. Traversing an index.
2. Reading thousands of index entries.
3. Performing thousands of heap lookups.

The planner therefore selected a Sequential Scan.

This demonstrates an important principle:

> PostgreSQL does not use indexes simply because they exist. It uses them only when they reduce estimated execution cost.

---

## 7.5 Analysis of Hash Joins

PostgreSQL selected Hash Joins for both table joins.

First join:

```text id="nnhq8x"
orders
  JOIN
customers
```

Second join:

```text id="5umh8z"
result
  JOIN
products
```

Execution:

```text id="l4vlgv"
Hash Join
(cost=327.50..2555.37)
(actual rows=64790)
```

---

### Why Hash Join Was Chosen

Hash joins are particularly effective when:

* Join conditions use equality operators.
* Large portions of both tables participate.
* Input relations fit comfortably in memory.

Example:

```sql id="z4lsqb"
o.customer_id = c.customer_id
```

and

```sql id="5y8jlp"
o.product_id = p.product_id
```

are equality joins.

PostgreSQL therefore:

1. Built a hash table for customers.
2. Built a hash table for products.
3. Probed those hash tables while scanning orders.

Simplified view:

```text id="c3fxv7"
customers
     |
 Build Hash Table
     |
orders Scan
     |
 Lookup Matching Rows
```

This strategy avoids repeated index traversals and is highly efficient for large joins.

---

## 7.6 Planner Estimates vs Actual Results

One of the most important outputs of EXPLAIN ANALYZE is the comparison between estimated and actual values.

Example:

```text id="2vwr22"
Estimated Rows: 64,777
Actual Rows:    64,790
```

Difference:

```text id="s1lh2n"
13 rows
```

out of approximately:

```text id="1e1wyo"
65,000 rows
```

This represents extremely accurate estimation.

The planner's estimate differs from reality by less than:

```text id="4e5szh"
0.03%
```

Such accuracy indicates that PostgreSQL's statistics were highly representative of the underlying data distribution.

---

## 7.7 Relationship with pg_stats

Statistics used by the planner are stored in PostgreSQL system catalogs and exposed through:

```sql id="pxshm9"
pg_stats
```

Observed statistics:

```text id="n5h09d"
customer_id : 9824 distinct values
product_id  : 1000 distinct values
quantity    : 11 distinct values
order_date  : 366 distinct values
```

---

### Meaning of n_distinct

Example:

```text id="wqzb2v"
product_id = 1000
```

indicates approximately:

```text id="4n4a09"
1000 unique product values
```

in the table.

The planner uses these values to estimate:

* Selectivity
* Join cardinality
* Scan costs
* Hash table sizes

Without accurate statistics, query plans would often be suboptimal.

---

## 7.8 Relationship with the Buffer Manager

Every page accessed during execution passed through PostgreSQL's Buffer Manager.

Execution flow:

```text id="vhc8jh"
Query
   |
Seq Scan
   |
Buffer Manager
   |
Shared Buffers
   |
Disk (if needed)
```

During the first execution:

```text id="aolr0w"
Buffer Misses
```

cause pages to be loaded from disk.

Subsequent executions would likely benefit from:

```text id="9pbzfr"
Buffer Hits
```

because pages remain cached inside Shared Buffers.

This demonstrates the interaction between the Query Executor and the Buffer Manager.

---

## 7.9 Key Observations

Several important PostgreSQL design principles can be observed from this experiment:

### Statistics Drive Planning

The planner's decisions depend heavily on collected statistics.

### Indexes Are Not Always Beneficial

Indexes are used only when they reduce overall execution cost.

### Hash Joins Are Effective for Large Equality Joins

The planner selected Hash Joins because they were cheaper than repeated index lookups.

### Accurate Statistics Improve Performance

The planner's row estimates were remarkably close to actual execution results.

### Query Execution Relies on Shared Buffers

All pages flowed through the Buffer Manager, demonstrating the importance of PostgreSQL's caching infrastructure.

---

## 7.10 Summary

The experiment demonstrates how PostgreSQL combines statistics, cost estimation, indexing structures, and memory management to select efficient execution plans.

The planner correctly identified that:

* Sequential Scans were cheaper than index lookups.
* Hash Joins were optimal for the workload.
* Available statistics accurately represented the dataset.

The close agreement between estimated and actual row counts illustrates the effectiveness of PostgreSQL's query optimization framework and highlights the importance of statistics collection through ANALYZE.

# 8. Key Learnings

This study provided valuable insight into the internal architecture of PostgreSQL and how its core subsystems work together to deliver performance, concurrency, and reliability.

The most important learning was that PostgreSQL's behavior is largely determined by a small number of foundational components:

* The Buffer Manager reduces disk I/O through Shared Buffers and intelligent page caching.
* B-Tree indexes provide efficient logarithmic-time lookups while maintaining balanced tree structures through page splits.
* MVCC enables high concurrency by maintaining multiple tuple versions and allowing readers and writers to operate without blocking each other.
* Write-Ahead Logging ensures durability by recording changes before data pages are written to disk.
* The Query Planner uses collected statistics to estimate costs and choose efficient execution strategies.

The EXPLAIN ANALYZE experiment demonstrated that PostgreSQL's planner does not simply use indexes whenever they are available. Instead, it evaluates alternative execution plans and selects the strategy with the lowest estimated cost. In the observed workload, Sequential Scans and Hash Joins were chosen because a large percentage of rows satisfied the filter condition, making index-based access more expensive.

Another important observation was the accuracy of PostgreSQL's statistics system. The estimated row counts closely matched the actual execution results, showing how critical ANALYZE and pg_stats are for query optimization.

Overall, PostgreSQL demonstrates how modern database systems combine storage structures, memory management, transaction processing, and query optimization into a unified architecture capable of supporting large-scale, concurrent workloads while maintaining strong ACID guarantees.

Perhaps the most significant lesson is that database performance is not determined by any single component. Instead, it emerges from the interaction of multiple subsystems, each designed around carefully considered engineering trade-offs.

# 9. References

## Official PostgreSQL Documentation

1. PostgreSQL Documentation
   https://www.postgresql.org/docs/

2. PostgreSQL Storage System
   https://www.postgresql.org/docs/current/storage.html

3. PostgreSQL Buffer Manager
   https://www.postgresql.org/docs/current/runtime-config-resource.html

4. PostgreSQL B-Tree Indexes
   https://www.postgresql.org/docs/current/btree.html

5. PostgreSQL Index Types
   https://www.postgresql.org/docs/current/indexes-types.html

6. PostgreSQL MVCC Documentation
   https://www.postgresql.org/docs/current/mvcc.html

7. PostgreSQL Transaction Isolation
   https://www.postgresql.org/docs/current/transaction-iso.html

8. PostgreSQL WAL Documentation
   https://www.postgresql.org/docs/current/wal-intro.html

9. PostgreSQL Checkpointing
   https://www.postgresql.org/docs/current/wal-configuration.html

10. PostgreSQL Query Planning
    https://www.postgresql.org/docs/current/planner-optimizer.html

11. PostgreSQL EXPLAIN Documentation
    https://www.postgresql.org/docs/current/using-explain.html

12. PostgreSQL Statistics Collector
    https://www.postgresql.org/docs/current/planner-stats.html

## PostgreSQL Source Code

13. PostgreSQL GitHub Repository
    https://github.com/postgres/postgres

14. Buffer Manager Source
    src/backend/storage/buffer/

15. B-Tree Implementation Source
    src/backend/access/nbtree/

## Experimental Work

16. PostgreSQL 16 Database Environment Used For This Study

17. EXPLAIN ANALYZE Experiments Performed On Custom customers, orders, and products Datasets

18. Statistics Analysis Performed Using pg_stats and ANALYZE

# Appendix: Raw Experiment Transcript

<details>
<summary>Click to view the raw psql transcript</summary>

```sql
(base) kavya-dhyani@Modern-14-B10MW:~$ sudo -u postgres psql
psql (16.14 (Ubuntu 16.14-0ubuntu0.24.04.1))
Type "help" for help.

postgres=# CREATE DATABASE pg_internals;
CREATE DATABASE
postgres=# \c pg_internals;
You are now connected to database "pg_internals" as user "postgres".
pg_internals=# CREATE TABLE customers (
    customer_id SERIAL PRIMARY KEY,
    customer_name TEXT,
    city TEXT
);

CREATE TABLE products (
    product_id SERIAL PRIMARY KEY,
    product_name TEXT,
    price NUMERIC(10,2)
);

CREATE TABLE orders (
    order_id SERIAL PRIMARY KEY,
    customer_id INTEGER REFERENCES customers(customer_id),
    product_id INTEGER REFERENCES products(product_id),
    quantity INTEGER,
    order_date DATE
);
CREATE TABLE
CREATE TABLE
CREATE TABLE
pg_internals=# INSERT INTO customers(customer_name, city)
SELECT
    'Customer_' || g,
    'City_' || (g % 20)
FROM generate_series(1,10000) g;
INSERT 0 10000
pg_internals=# INSERT INTO products(product_name, price)
SELECT
    'Product_' || g,
    (random()*1000)::numeric(10,2)
FROM generate_series(1,1000) g;
INSERT 0 1000
pg_internals=# INSERT INTO orders(customer_id, product_id, quantity, order_date)
SELECT
    (random()*9999 + 1)::int,
    (random()*999 + 1)::int,
    (random()*10 + 1)::int,
    CURRENT_DATE - ((random()*365)::int)
FROM generate_series(1,100000);
INSERT 0 100000
pg_internals=# CREATE INDEX idx_orders_customer
ON orders(customer_id);

CREATE INDEX idx_orders_product
ON orders(product_id);

ANALYZE;
CREATE INDEX
CREATE INDEX
ANALYZE
pg_internals=# EXPLAIN ANALYZE
SELECT
    c.customer_name,
    p.product_name,
    o.quantity
FROM orders o
JOIN customers c
    ON o.customer_id = c.customer_id
JOIN products p
    ON o.product_id = p.product_id
WHERE o.quantity >= 5;
                                                            QUERY PLAN                                                            
----------------------------------------------------------------------------------------------------------------------------------
 Hash Join  (cost=327.50..2555.37 rows=64777 width=28) (actual time=10.209..102.053 rows=64790 loops=1)
   Hash Cond: (o.product_id = p.product_id)
   ->  Hash Join  (cost=298.00..2355.11 rows=64777 width=21) (actual time=9.684..76.812 rows=64790 loops=1)
         Hash Cond: (o.customer_id = c.customer_id)
         ->  Seq Scan on orders o  (cost=0.00..1887.00 rows=64777 width=12) (actual time=0.006..24.898 rows=64790 loops=1)
               Filter: (quantity >= 5)
               Rows Removed by Filter: 35210
         ->  Hash  (cost=173.00..173.00 rows=10000 width=17) (actual time=9.658..9.660 rows=10000 loops=1)
               Buckets: 16384  Batches: 1  Memory Usage: 636kB
               ->  Seq Scan on customers c  (cost=0.00..173.00 rows=10000 width=17) (actual time=0.005..5.265 rows=10000 loops=1)
   ->  Hash  (cost=17.00..17.00 rows=1000 width=15) (actual time=0.514..0.515 rows=1000 loops=1)
         Buckets: 1024  Batches: 1  Memory Usage: 55kB
         ->  Seq Scan on products p  (cost=0.00..17.00 rows=1000 width=15) (actual time=0.014..0.230 rows=1000 loops=1)
 Planning Time: 4.983 ms
 Execution Time: 105.415 ms
(15 rows)

pg_internals=# SELECT
    attname,
    n_distinct
FROM pg_stats
WHERE tablename='orders';
   attname   | n_distinct 
-------------+------------
 order_id    |         -1
 customer_id |       9824
 quantity    |         11
 product_id  |       1000
 order_date  |        366
(5 rows)
```
</details>
