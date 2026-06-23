# MySQL / InnoDB Storage Engine

## 1. Problem Background

MySQL has been around for a long time, but its original storage engine (MyISAM) was honestly kind of limited. MyISAM was fast for reads and great for simple web apps, but it didn't support transactions, didn't have foreign keys, and if the server crashed mid-write, you could end up with corrupted data. That's... not great for anything serious.

InnoDB was built specifically to fix these problems. It was originally developed by Innobase Oy (a Finnish company) and later acquired by Oracle. The whole point was to give MySQL proper ACID transactions, row-level locking, foreign key support, and crash recovery — basically everything you need when you're running a real production system where data integrity actually matters.

It became MySQL's default storage engine in version 5.5, and for good reason. Most MySQL deployments today are running InnoDB under the hood.

## 2. Architecture Overview

InnoDB sits as a storage engine layer below the MySQL server. The MySQL server handles connection management, parsing, optimization, etc., and then hands off the actual data storage/retrieval to InnoDB.

The main components:
- **Buffer Pool**: A big chunk of memory that caches both data pages and index pages. This is where most of the action happens — InnoDB tries to keep as much data in memory as possible to avoid disk I/O.
- **Log Buffer**: A small in-memory buffer for redo log entries before they get flushed to disk.
- **Redo Logs** (ib_logfile0, ib_logfile1): Sequential write-ahead log files for crash recovery. Similar in concept to PostgreSQL's WAL.
- **Undo Logs**: Stored in the system tablespace (or separate undo tablespaces). These hold the old versions of modified rows, used for rollback and MVCC reads.
- **Data Files** (.ibd files): Where the actual table data lives, organized as a clustered index.

```
  MySQL Server Layer (parser, optimizer, etc.)
         │
    InnoDB Engine
         │
    ┌────┴────────────────┐
  Buffer Pool           Log Buffer
    │                       │
  Data Files (.ibd)     Redo Logs (ib_logfile)
    │
  Undo Logs
```

## 3. Internal Design

### Clustered Index — This Changes Everything

This is probably the single most important thing about InnoDB, and it took me a while to really get what it means.

In InnoDB, the **primary key index IS the table**. The leaf nodes of the primary key B+Tree don't just contain pointers to the data — they contain the actual row data itself. So when you do a primary key lookup, you traverse the B+Tree and the data is right there in the leaf. No second hop needed.

This is called a "clustered index" or "Index-Organized Table" and it's fundamentally different from PostgreSQL's approach, where the table is a heap and all indexes (including the primary key index) just store pointers to the heap location.

**Secondary indexes** in InnoDB work differently too. Instead of storing a physical row location, they store the **primary key value**. So when you query by a secondary index:
1. InnoDB traverses the secondary index B+Tree to find the matching primary key value
2. Then traverses the clustered index (primary key B+Tree) again to get the actual row data

This is called a "double lookup" and it's one of the trade-offs of the clustered index design. Secondary index lookups are inherently slower than in PostgreSQL, where any index points directly to the heap.

### Undo Logs and Redo Logs

This is another area where InnoDB and PostgreSQL differ significantly.

**Redo logs** are conceptually similar to PostgreSQL's WAL. They record physical changes to pages, and they're used for crash recovery. The idea is the same: write the change to the redo log first, acknowledge the transaction as committed, and flush the actual data pages to disk whenever convenient. If the server crashes, replay the redo log to recover.

**Undo logs** are where it gets interesting. When InnoDB modifies a row in-place, it first copies the old version of the row to the undo log. This serves two purposes:

1. **Rollback**: If the transaction decides to abort, InnoDB reads the old row version from the undo log and puts it back. (In PostgreSQL, rollback is basically free — you just mark the transaction as aborted and the old tuple version is still in the heap.)

2. **MVCC reads**: If another transaction needs to read the row while it's being modified, InnoDB follows the undo log chain to find the version of the row that was valid for that transaction's snapshot. This is how InnoDB achieves snapshot isolation — not by keeping multiple versions in the main table (like PostgreSQL), but by reconstructing old versions from the undo log on demand.

**Why does InnoDB need BOTH?** Because they solve different problems. Redo = durability (what if the server crashes?). Undo = isolation and rollback (what if a transaction aborts, or another transaction needs to see an older version?). You can't use one for the other's job.

### Locking: Row-Level and Gap Locks

InnoDB does row-level locking, which is a big improvement over table-level locking (MyISAM). But it also has this thing called **gap locking** which is worth understanding.

**Gap locks** prevent phantom reads at the REPEATABLE READ isolation level. Say you run `SELECT * FROM orders WHERE amount > 100`. InnoDB doesn't just lock the rows that currently match — it also locks the "gaps" between index entries in that range. This prevents another transaction from inserting a new row with `amount = 150` while your transaction is still running. Without gap locks, you could run the same query twice in the same transaction and get different results (phantom reads).

Gap locks are one of those things that are clever but can cause unexpected deadlocks if you're not careful.

## 4. Design Trade-Offs: InnoDB vs PostgreSQL

### MVCC Approaches

| Aspect | PostgreSQL | InnoDB |
|--------|-----------|---------|
| Update strategy | Creates new tuple in heap | Modifies row in-place, old version → undo log |
| Where old versions live | In the main data files | In separate undo logs |
| Cleanup needed? | Yes — VACUUM removes dead tuples | Undo log space is reclaimed automatically (purge thread) |
| Rollback speed | Instant (just mark xact as aborted) | Slower (must apply undo log to restore old values) |
| Risk | Table bloat if VACUUM can't keep up | Undo log bloat if long-running transactions exist |

I think the fundamental philosophical difference is: PostgreSQL says "write a new version, clean up later" while InnoDB says "update in place, keep the old version on the side." Both work. Both have their pain points.

### Clustered vs Heap Storage

- **InnoDB (clustered)**: Primary key lookups are blazing fast. But secondary indexes are slower (double lookup) and larger (they store the full primary key, not just a small TID).
- **PostgreSQL (heap)**: All indexes are equal — they all store TIDs pointing to the heap. No double lookup penalty for secondary indexes. But primary key lookups aren't any faster than secondary index lookups.

## 5. Experiments / Observations

**Thought experiment: Updating a non-indexed column**

This is a scenario that really highlights the architectural differences:

- **In PostgreSQL**: Even though you're only changing one column, a whole new tuple gets written to the heap. The HOT optimization helps if the new tuple fits on the same page — it avoids updating all the indexes by linking the old and new tuples. But if the page is full, the new tuple goes on a different page, and now ALL indexes need to be updated to point to the new location. That's expensive.

- **In InnoDB**: The row gets updated in-place in the clustered index leaf node. The old value goes to the undo log. The key thing: secondary indexes don't need to be touched at all, because they point to the primary key value, not a physical location. And the primary key hasn't changed. This is actually a pretty elegant consequence of the clustered index design.

So for update-heavy workloads where you're changing non-indexed columns, InnoDB has a clear advantage in terms of write amplification.

## 6. Key Learnings

- **The undo + redo separation makes sense once you think about it.** Redo is about "what happened" (for crash recovery). Undo is about "what was there before" (for rollback and MVCC). They're complementary, not redundant.

- **Clustered indexes are a double-edged sword.** Yes, primary key lookups are great. But the cost is paid by every secondary index — they're bigger and slower because of the double lookup. If your workload is dominated by secondary index queries, PostgreSQL's heap model might actually be faster.

- **MVCC design choices have operational consequences.** PostgreSQL operators worry about VACUUM and table bloat. MySQL/InnoDB operators worry about long-running transactions causing undo log growth. Different MVCC approach, different operational headache.

- **InnoDB's in-place update model is genuinely clever for the common case.** Most real-world updates change a few columns without touching the primary key or indexed columns. In those cases, InnoDB barely breaks a sweat while PostgreSQL has to create a whole new tuple and potentially update indexes.
