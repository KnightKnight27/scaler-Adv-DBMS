# MySQL / InnoDB Storage Engine – Internal Architecture

**Name:** Pulasari Jai  
**Roll Number:** 24BCS10656  
**Course:** Advanced DBMS  
**Topic:** Topic 3 – MySQL / InnoDB Storage Engine

---

## 1. Problem Background

### Why does InnoDB even exist?

So here's the thing – MySQL originally didn't have a great storage engine. The default one was called MyISAM, and it had some serious problems. The biggest issue? It didn't support transactions at all. No ACID guarantees, no rollbacks, no crash recovery worth relying on. For simple read-heavy apps it was fine, but for anything involving money, orders, or data you couldn't afford to lose, MyISAM was basically a liability.

That's where InnoDB comes in. It was developed by a Finnish company called Innobase Oy (founded by Heikki Tuuri) in the late 1990s and was introduced as a pluggable storage engine for MySQL. Oracle eventually acquired Innobase in 2005, and then acquired MySQL itself in 2010 – so now they're all under one roof.

InnoDB was designed to solve the exact problems MyISAM had:
- Full ACID transaction support
- Row-level locking instead of table-level locking
- Crash recovery through redo logging
- Foreign key support

Today, InnoDB is the **default** storage engine for MySQL. When you create a table in MySQL without specifying anything, you're using InnoDB. It powers massive production workloads – social media platforms, fintech apps, e-commerce backends. It's pretty much everywhere.

The interesting part (and what makes it worth studying) is that InnoDB made some very different architectural choices compared to PostgreSQL, especially around how it handles storage, versioning, and concurrency. Those choices have real consequences that are worth understanding.

---

## 2. Architecture Overview

### High-Level Picture

```
MySQL Architecture
──────────────────

[Client Applications]
        |
        | (SQL queries over TCP/IP or Unix socket)
        |
┌───────────────────────────────────────────────┐
│              MySQL Server Layer               │
│                                               │
│  ┌─────────────────────────────────────────┐  │
│  │          Connection Manager             │  │
│  │   (thread per connection, thread pool)  │  │
│  └─────────────────────────────────────────┘  │
│                    |                          │
│  ┌─────────────────────────────────────────┐  │
│  │         SQL Parser & Optimizer          │  │
│  │   (parses SQL → logical plan →          │  │
│  │    physical plan via cost model)        │  │
│  └─────────────────────────────────────────┘  │
│                    |                          │
│  ┌─────────────────────────────────────────┐  │
│  │           Query Executor                │  │
│  └─────────────────────────────────────────┘  │
└────────────────────|──────────────────────────┘
                     |
         [Storage Engine API (Handlerton)]
                     |
┌────────────────────────────────────────────────┐
│              InnoDB Storage Engine             │
│                                                │
│  ┌──────────────┐    ┌──────────────────────┐  │
│  │ Buffer Pool  │    │   Undo Log Segments  │  │
│  │ (in-memory   │    │   (for MVCC and      │  │
│  │  page cache) │    │    rollback)         │  │
│  └──────────────┘    └──────────────────────┘  │
│                                                │
│  ┌──────────────┐    ┌──────────────────────┐  │
│  │  Redo Log    │    │  Clustered Index     │  │
│  │  (ib_logfile)│    │  (.ibd files)        │  │
│  └──────────────┘    └──────────────────────┘  │
│                                                │
│  ┌──────────────┐    ┌──────────────────────┐  │
│  │ Change Buffer│    │  Secondary Indexes   │  │
│  │ (deferred    │    │  (also in .ibd)      │  │
│  │  index ops)  │    └──────────────────────┘  │
│  └──────────────┘                              │
└────────────────────────────────────────────────┘
                     |
            [Disk – .ibd files]
```

### Main Components

- **MySQL Server Layer** – handles connections, SQL parsing, query optimization. This part is engine-agnostic.
- **Storage Engine API** – a clean abstraction that lets MySQL swap out storage engines. InnoDB plugs in here.
- **Buffer Pool** – the big in-memory page cache. Most of InnoDB's performance lives here.
- **Clustered Index** – the primary table storage structure, organized as a B+ tree.
- **Secondary Indexes** – separate B+ trees for non-primary-key lookups.
- **Undo Logs** – old row versions, used for MVCC and rollback.
- **Redo Logs** – write-ahead logs for crash recovery.
- **Change Buffer** – defers secondary index updates to reduce random I/O.

---

## 3. Internal Design

### 3.1 Clustered Index – The Core of InnoDB Storage

This is probably the most important thing to understand about InnoDB. In InnoDB, **the table data itself is stored inside the primary key B+ tree**. This is called a clustered index. The primary key isn't just a lookup structure – it *is* the table.

Every InnoDB table is physically organized like this:

```
Clustered Index (B+ Tree organized by primary key)
──────────────────────────────────────────────────

                  [Root Page]
                  pk: 1..1000
                 /             \
    [Internal Page]         [Internal Page]
     pk: 1..500              pk: 501..1000
      /       \                /         \
[Leaf Page]  [Leaf Page]  [Leaf Page]  [Leaf Page]
pk: 1..100   pk:101..200  pk:501..600  pk:601..700

Each Leaf Page contains:
┌─────────────────────────────────────────┐
│  Row: pk=1, name="Jai", age=21, ...    │  ← actual data
│  Row: pk=2, name="Arjun", age=22, ... │
│  Row: pk=3, name="Priya", age=20, ... │
│  ...                                   │
│  [Linked list pointer → next leaf]     │
└─────────────────────────────────────────┘
```

Leaf pages of the clustered index hold the **complete row data**. This is fundamentally different from PostgreSQL where the heap (actual data) and the index are separate files.

Why does this matter? Because if you're looking up a row by its primary key, InnoDB does a single B+ tree traversal and gets the complete row right there at the leaf level. No second lookup needed.

**What happens if you don't define a primary key?**

InnoDB will automatically create a hidden 6-byte `rowid` column and use it as the clustered index key. You'll never see this column in queries, but it exists in storage. Always define an explicit primary key – otherwise you have zero control over how your data is physically organized.

### 3.2 Secondary Indexes

Secondary indexes in InnoDB are where things get interesting. Unlike the clustered index (which stores actual row data at leaf nodes), secondary index leaf nodes store:

```
Secondary Index Leaf Node:
┌──────────────────────────────────────────────┐
│  (indexed_column_value, primary_key_value)   │
└──────────────────────────────────────────────┘
```

That's it. The primary key value is embedded in every secondary index entry.

So a query like `SELECT * FROM users WHERE email = 'jai@example.com'` does:
1. Traverse the email secondary index B+ tree → find `(jai@example.com, pk=42)`
2. Take that `pk=42` and do a second traversal through the **clustered index** to fetch the full row

This second lookup is called a **clustered index lookup** or sometimes a "double read" and it's an important performance consideration. If your query only needs columns that are in the secondary index, InnoDB can avoid the second lookup entirely – this is called a **covering index**.

```sql
-- This needs a clustered index lookup (SELECT * fetches all columns)
SELECT * FROM users WHERE email = 'jai@example.com';

-- This is a covering index scan (email + id already in secondary index leaf)
SELECT id, email FROM users WHERE email = 'jai@example.com';
```

The reason secondary indexes store the primary key instead of a direct physical pointer (like a page:slot number the way PostgreSQL does with TID) is that in InnoDB, rows can physically move when pages split. If secondary indexes stored physical locations, every page split would require updating all secondary indexes. By using the primary key as the "pointer", InnoDB decouples secondary indexes from physical page locations.

### 3.3 Buffer Pool

The buffer pool is InnoDB's main memory structure – essentially a large in-memory cache for disk pages. When InnoDB needs to read a page, it first checks the buffer pool. If the page is there (cache hit), great. If not, it reads from disk and loads it into the buffer pool.

```
Buffer Pool Structure:
┌──────────────────────────────────────────────────────────────────────┐
│                        Buffer Pool                                   │
│                                                                      │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌───────────┐        │
│  │  Page     │  │  Page     │  │  Page     │  │  Page     │  ...   │
│  │ (16 KB)   │  │ (16 KB)   │  │ (16 KB)   │  │ (16 KB)   │        │
│  └───────────┘  └───────────┘  └───────────┘  └───────────┘        │
│                                                                      │
│  LRU List (Least Recently Used eviction policy)                     │
│  ← old (evict from here)         new (recently accessed) →          │
│                                                                      │
│  Free List: pages not yet in use                                    │
│  Flush List: dirty pages (modified but not yet written to disk)     │
└──────────────────────────────────────────────────────────────────────┘
```

InnoDB uses a modified LRU (Least Recently Used) algorithm. The reason it's "modified" is to avoid a problem called buffer pool pollution – where a large sequential scan (like a `SELECT *` on a huge table) dumps millions of pages into the buffer pool and evicts actually hot pages. InnoDB splits the LRU list into a "young" (hot) sublist and "old" (cold) sublist. New pages enter at the midpoint of the old sublist, and only get promoted to the young sublist after they're accessed again. Full table scans stay in the cold zone and don't pollute the hot zone.

The buffer pool size is set via `innodb_buffer_pool_size`. For a production database server, a common recommendation is to set this to 70-80% of available RAM. If your working dataset fits entirely in the buffer pool, queries barely touch disk and performance is excellent.

### 3.4 MVCC in InnoDB – The Undo Log Approach

MVCC (Multi-Version Concurrency Control) is how InnoDB allows multiple transactions to read and write data without constantly blocking each other. The concept is the same as PostgreSQL – multiple versions of rows exist simultaneously. But the implementation is completely different.

**PostgreSQL approach:** Stores old and new tuple versions side by side in the same heap file. Queries figure out which version to read based on transaction IDs embedded in the tuple header.

**InnoDB approach:** Stores only the current version of a row in the clustered index. Old versions are stored separately in the **undo log**.

Every InnoDB row has two hidden system columns:
- `DB_TRX_ID` – the transaction ID of the last transaction that modified this row
- `DB_ROLL_PTR` – a pointer (rollback pointer) into the undo log, pointing to the previous version of this row

```
MVCC Example – What happens when we UPDATE a row

Initial state (row inserted by txn 100):
Clustered Index leaf:
  [pk=1 | name="Jai" | age=21 | DB_TRX_ID=100 | DB_ROLL_PTR=NULL]

After UPDATE by txn 200 (age = 22):
Clustered Index leaf (in-place update):
  [pk=1 | name="Jai" | age=22 | DB_TRX_ID=200 | DB_ROLL_PTR=→undo_log]

Undo Log (stores the before-image):
  [pk=1 | name="Jai" | age=21 | DB_TRX_ID=100 | DB_ROLL_PTR=NULL]
```

Now if another transaction (txn 150) started before txn 200 committed, and it reads this row, here's what happens:
1. InnoDB reads the current clustered index version: `age=22, DB_TRX_ID=200`
2. It checks its read snapshot: "Can I see data written by txn 200?" → No, txn 150 started before 200
3. It follows the `DB_ROLL_PTR` into the undo log to find the older version: `age=21, DB_TRX_ID=100`
4. It checks: "Can I see data written by txn 100?" → Yes!
5. Returns `age=21` to the query

This is Oracle-style MVCC. The undo log can be a chain of multiple versions if a row has been updated many times.

The big difference from PostgreSQL: **InnoDB updates rows in-place and uses undo logs for old versions. PostgreSQL keeps old versions in the heap and marks them dead.** Both achieve the same effect (readers don't block writers) but through different mechanisms.

**InnoDB MVCC doesn't need VACUUM.** Since old versions are in the undo log (not the main data file), the undo log gets purged automatically by the purge thread when no active transaction needs those old versions anymore. The main data files don't accumulate dead rows the way PostgreSQL heap files do.

### 3.5 Redo Logs (Write-Ahead Logging)

InnoDB uses Write-Ahead Logging (WAL) for crash recovery. The concept is: before any changes are applied to actual data pages, the change is written to the redo log. If the system crashes, InnoDB can replay the redo log on startup to recover all committed transactions.

```
Write Path in InnoDB:
─────────────────────

1. Transaction modifies a page in the buffer pool (in-memory only)
2. Redo log record is written to the redo log buffer (in-memory)
3. Before commit, redo log buffer is flushed to disk (ib_logfile0, ib_logfile1)
4. Transaction commit acknowledged to client
5. Dirty page in buffer pool is flushed to .ibd file later (async)

Crash Recovery:
If crash happens at step 3 or 4:
  → InnoDB reads redo log on restart → replays changes → data recovered

If crash happens at step 2 (before flush):
  → Redo log has no record → transaction was not committed → no data loss
  (uncommitted work is simply lost – which is correct ACID behavior)
```

The `innodb_flush_log_at_trx_commit` parameter controls how aggressively the redo log is flushed:
- `1` (default) – flush and fsync on every commit. Maximum durability. Slowest.
- `2` – write to OS page cache on every commit, fsync once per second. Risk: 1 second of committed data lost on OS crash.
- `0` – write and sync once per second. Fastest but can lose up to 1 second of committed transactions even on MySQL crash.

Setting `2` or `0` is common in scenarios where some data loss is acceptable for better write throughput (like bulk loading data that can be re-run).

### 3.6 Undo Logs

InnoDB maintains two types of undo logs:
- **Insert undo logs** – needed only for rollback. Discarded after the transaction commits.
- **Update undo logs** – needed for both rollback AND MVCC. Kept alive as long as any active transaction's read snapshot might need them.

Undo logs live in the **system tablespace** (or separate undo tablespaces in newer MySQL versions). The purge thread periodically scans for undo log segments that are no longer needed by any active transaction and frees them.

If a long-running transaction holds a snapshot from way back, InnoDB can't purge old undo log versions. This is called **undo log bloat** and it can slow down reads because InnoDB has to traverse longer undo log chains to find the visible version of a row.

### 3.7 Locking Mechanisms

InnoDB does row-level locking, which is one of its big advantages over MyISAM's table-level locking.

**Types of locks in InnoDB:**

**Record Locks** – Locks on a specific index record. `SELECT ... FOR UPDATE` acquires exclusive record locks on matching rows.

**Gap Locks** – Locks on the *gap* between index values. Not an actual row, but the space between rows.

```
Example: Gap Locks
Table: users with pk values 1, 5, 10, 15

If a transaction does: SELECT * FROM users WHERE pk BETWEEN 5 AND 10 FOR UPDATE

InnoDB locks:
  - Record lock on pk=5
  - Gap lock on (5, 10)  ← the gap between 5 and 10
  - Record lock on pk=10

No other transaction can insert pk=6, 7, 8, or 9 while this lock is held.
```

Gap locks exist to prevent **phantom reads** under REPEATABLE READ isolation. Without gap locks, a second read in the same transaction could see newly inserted rows that match the WHERE clause – which violates the repeatable read guarantee.

**Next-Key Locks** – A combination of a record lock + gap lock on the gap before the record. This is InnoDB's default locking mode in REPEATABLE READ.

**Why this matters:** Gap locks can sometimes cause unexpected blocking. If two transactions try to insert rows into the same gap, they can deadlock. InnoDB handles this with automatic deadlock detection – it rolls back the transaction with the smaller undo log (the "cheaper" one to undo).

---

## 4. Design Trade-Offs

### The Clustered Index Trade-Off

The biggest design decision in InnoDB is the clustered index, and it comes with both benefits and costs.

**Benefits:**
- Primary key lookups are extremely fast – single tree traversal, data is right there at the leaf
- Range scans on primary key are fast because data is physically sorted
- For time-series or sequential data (like auto-increment IDs), inserts are mostly sequential which is cache-friendly

**Costs:**
- If your primary key is not sequential (like a random UUID), inserts cause a lot of random page splits and write amplification. This is why `VARCHAR(36) UUID` as primary keys in InnoDB tends to perform poorly at scale – it randomizes the insert location in the B+ tree and causes constant page splits.
- Secondary index lookups always require a second lookup into the clustered index (the "double read" problem described earlier)

### InnoDB vs PostgreSQL MVCC Trade-Off

| Aspect | InnoDB (Undo Log MVCC) | PostgreSQL (Heap MVCC) |
|---|---|---|
| Update mechanism | In-place update + undo log | Append new version to heap |
| Old version location | Undo log segments | Same heap file as live rows |
| Cleanup | Purge thread clears undo log | VACUUM cleans dead tuples |
| Bloat | Undo log bloat (long txns) | Table bloat (dead tuples) |
| Index updates on write | Must update secondary indexes | Must update heap + all indexes |
| Read of old version | Follow ROLL_PTR chain | Check visibility in same page |

Neither approach is strictly better. PostgreSQL's approach makes writes to the heap simple (just append), but creates dead tuple bloat that VACUUM has to deal with. InnoDB's approach keeps the main data file clean but requires maintaining undo log chains and creates a separate purge problem.

### Undo + Redo – Why Both?

This often confuses people. Why does InnoDB need *both* undo logs and redo logs?

- **Redo log** – ensures committed changes survive a crash (durability). It records what changes were made and can replay them.
- **Undo log** – enables two things: (1) rolling back uncommitted transactions and (2) providing old row versions for MVCC readers.

They serve completely different purposes. If InnoDB only had redo logs, it could recover committed transactions but couldn't roll back uncommitted ones or give readers a consistent snapshot. If it only had undo logs, it could roll back and do MVCC but couldn't survive a crash of committed data.

### Row-Level Locking vs Table-Level Locking

InnoDB's row-level locking is far better than MyISAM's table-level locking for concurrent write workloads. But row-level locking isn't free:
- Lock metadata takes memory
- Gap locks can cause unexpected blocking and deadlocks
- Deadlock detection adds overhead

For workloads that are mostly reads with occasional bulk writes, table-level locking can sometimes be faster because there's no per-row lock overhead. But for typical web app write patterns, row-level locking wins.

---

## 5. Experiments / Observations

### Experiment 1: EXPLAIN Output – Clustered vs Secondary Index

Created a table:
```sql
CREATE TABLE students (
    student_id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100),
    email VARCHAR(150),
    department VARCHAR(50),
    INDEX idx_email (email),
    INDEX idx_dept (department)
);
```

Inserted 50,000 rows, then ran:

```sql
-- Query 1: Primary key lookup
EXPLAIN SELECT * FROM students WHERE student_id = 12345;
```

Output:
```
+----+-------------+----------+-------+---------------+---------+---------+-------+------+-------+
| id | select_type | table    | type  | possible_keys | key     | key_len | ref   | rows | Extra |
+----+-------------+----------+-------+---------------+---------+---------+-------+------+-------+
|  1 | SIMPLE      | students | const | PRIMARY       | PRIMARY | 4       | const |    1 |       |
+----+-------------+----------+-------+---------------+---------+---------+-------+------+-------+
```

`type: const` means MySQL found exactly one row using the primary key. This is the fastest possible access type.

```sql
-- Query 2: Secondary index lookup (needs double read)
EXPLAIN SELECT * FROM students WHERE email = 'jai@example.com';
```

Output:
```
+----+-------------+----------+------+---------------+-----------+---------+-------+------+-------+
| id | select_type | table    | type  | possible_keys | key       | key_len | ref   | rows | Extra |
+----+-------------+----------+------+---------------+-----------+---------+-------+------+-------+
|  1 | SIMPLE      | students | ref  | idx_email     | idx_email | 153     | const |    1 |       |
+----+-------------+----------+------+---------------+-----------+---------+-------+------+-------+
```

`type: ref` means it used the secondary index and then did a clustered index lookup.

```sql
-- Query 3: Covering index (avoids double read)
EXPLAIN SELECT student_id, email FROM students WHERE email = 'jai@example.com';
```

Output:
```
Extra: Using index
```

"Using index" means MySQL satisfied the query entirely from the secondary index – no clustered index lookup needed. This is the covering index optimization.

### Experiment 2: Impact of UUID vs INT Primary Keys

To test the clustered index insert performance difference:

```sql
-- Table A: Sequential INT primary key
CREATE TABLE orders_int (
    id INT AUTO_INCREMENT PRIMARY KEY,
    product VARCHAR(100),
    amount DECIMAL(10,2)
);

-- Table B: Random UUID primary key
CREATE TABLE orders_uuid (
    id VARCHAR(36) DEFAULT (UUID()) PRIMARY KEY,
    product VARCHAR(100),
    amount DECIMAL(10,2)
);
```

Inserted 100,000 rows into each using a script.

Observed (approximate results):
- `orders_int`: inserts were fast and consistent throughout. Since IDs are sequential, new rows always go to the rightmost leaf page of the clustered index.
- `orders_uuid`: inserts slowed down as table grew. Random UUIDs meant each insert potentially went to a different random page in the clustered index, causing page splits and I/O scatter.

This is a real production concern. At Bangalore-scale startups where I've seen MySQL used, UUID primary keys in InnoDB tables frequently cause performance degradation at scale. Using `BINARY(16)` with UUID_TO_BIN(..., 1) (swap-flag=1, which reorders UUID bytes to be time-sequential) is a common workaround.

### Experiment 3: Checking InnoDB Status

```sql
SHOW ENGINE INNODB STATUS\G
```

Key observations from the output:
- **Buffer pool hit rate**: showed 996/1000 pages read from cache. Means 99.6% of reads served from memory, only 0.4% needed disk I/O. This is typical for a warm cache with a reasonable buffer pool size.
- **Undo log segments**: a few hundred segments visible during active transactions, clearing after commit
- **Deadlocks**: zero in this test, but the status shows the last deadlock if one occurred – useful for debugging
- **Rows read/inserted/updated/deleted per second**: gives a real-time view of table access patterns

---

## 6. Key Learnings

**1. Clustered indexes change everything about how you design tables**  
InnoDB's decision to make the table *itself* a B+ tree organized by primary key means your primary key choice directly impacts storage layout, insert performance, and range scan performance. This isn't the case in PostgreSQL where the heap is separate from indexes. When working with InnoDB, always think about your primary key carefully.

**2. In-place updates + undo logs is a completely different MVCC approach from PostgreSQL**  
PostgreSQL keeps old versions in the heap and cleans them with VACUUM. InnoDB keeps old versions in the undo log and cleans them with the purge thread. Both solve the same problem (readers see a consistent snapshot) but through completely different mechanisms. Neither is universally better – the choice reflects different engineering philosophies about where bloat is easier to manage.

**3. You need both undo logs AND redo logs, and they serve different purposes**  
Redo logs = crash recovery of committed work. Undo logs = rollback + MVCC. Confusing these is a common mistake. They're not redundant – they're solving different problems.

**4. Gap locks are InnoDB's solution to phantom reads, but they can cause unexpected blocking**  
Phantom reads (a transaction seeing different rows across two identical range queries) are prevented in InnoDB's REPEATABLE READ by locking the *gaps* between rows. This works great for correctness but can surprise developers when inserts block unexpectedly. Understanding gap locks is important for debugging InnoDB deadlocks.

**5. Secondary index design in InnoDB is more expensive than in PostgreSQL**  
Every secondary index lookup in InnoDB potentially requires a second look up into the clustered index. Covering indexes (where the secondary index contains all columns the query needs) are an important optimization to avoid this double read. In PostgreSQL, this problem doesn't exist because the index TID points directly to the heap location.

**6. `SHOW ENGINE INNODB STATUS` is your friend for debugging**  
This one command shows buffer pool hit rate, active transactions, lock waits, deadlocks, undo log activity, and a whole lot more. It's the first place to look when troubleshooting MySQL performance issues.

---

## References

- MySQL Documentation – InnoDB Architecture: https://dev.mysql.com/doc/refman/8.0/en/innodb-architecture.html
- MySQL Documentation – InnoDB Buffer Pool: https://dev.mysql.com/doc/refman/8.0/en/innodb-buffer-pool.html
- MySQL Documentation – InnoDB Locking: https://dev.mysql.com/doc/refman/8.0/en/innodb-locking.html
- MySQL Documentation – InnoDB MVCC: https://dev.mysql.com/doc/refman/8.0/en/innodb-multi-versioning.html
- "High Performance MySQL" – Baron Schwartz, Peter Zaitsev, Vadim Tkachenko
- "Architecture of a Database System" – Hellerstein, Stonebraker, Hamilton (2007)
- InnoDB source code: `storage/innobase/` in MySQL GitHub repository