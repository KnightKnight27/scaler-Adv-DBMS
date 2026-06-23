# PostgreSQL Internal Architecture

**Author:** Abhiroop Sistu

**Roll Number:** 24BCS10287

I studied four core parts of PostgreSQL: the Buffer Manager, the B-Tree Index (nbtree), MVCC, and WAL. My focus was understanding why each component exists and what trade-offs it makes.

---

## 1. Problem Background

PostgreSQL must serve many users simultaneously while ensuring that committed data is never lost. To achieve this, it solves four major problems:

| Problem | Solution |
|----------|-----------|
| Disk is slow | Buffer Manager caches hot pages in memory |
| Finding rows quickly | B-Tree indexes (nbtree) |
| Many users at once | MVCC (Multi-Version Concurrency Control) |
| Crashes happen | WAL, checkpoints, and recovery |

---

## 2. Architecture Overview

```text
clients -> postmaster -> forks one backend process per client
                              |
   Parser -> Rewriter -> Planner -> Executor
                              |  reads/writes pages
   SHARED MEMORY: shared_buffers (page cache) + WAL buffers + locks
        |  flush pages                 |  flush log
        v                              v
   data files (8 KB pages)         WAL files (pg_wal/)

helpers: WAL writer, checkpointer, background writer, autovacuum
```

### Execution Flow

1. A backend process receives SQL from a client.
2. The Parser checks syntax.
3. The Rewriter expands views and rules.
4. The Planner chooses an execution plan using statistics.
5. The Executor accesses pages through the Buffer Manager.
6. Changes are written to WAL first.
7. Modified pages remain in memory.
8. COMMIT flushes WAL to disk, making the transaction durable.

---

## 3. Internal Design

### Buffer Manager

Location:

```text
src/backend/storage/buffer/
```

`shared_buffers` is a shared-memory cache consisting of 8 KB page slots accessible by all backend processes.

#### Page Lookup

```text
(table, block number)
          |
      Hash Table
          |
      Buffer Slot
```

On a cache miss:

1. PostgreSQL reads the page from disk.
2. A victim buffer is selected.
3. The new page is loaded into memory.

#### Clock-Sweep Replacement

Instead of true LRU, PostgreSQL uses a clock-sweep algorithm.

**Benefits**

- Lower overhead than LRU
- Good approximation of page popularity
- Scales well under high concurrency

Pages currently being used are **pinned**, preventing eviction.

Dirty pages are written later because their modifications are already protected by WAL.

---

### B-Tree Index (nbtree)

The default PostgreSQL index type.

#### Structure

```text
Root
 |
 +-- Internal Nodes
 |
 +-- Leaf Pages (Key + TID)
```

Leaf pages store:

- Indexed key
- TID (Tuple Identifier)

Leaf pages are linked left-to-right, making range scans efficient.

#### Search Process

```text
Root
  |
Binary Search
  |
Child Node
  |
Leaf Page
  |
TID
  |
Heap Row
```

#### Splits

When a leaf page becomes full:

1. The page splits.
2. A separator key moves upward.
3. Right-link pointers maintain concurrent access during the split.

---

### MVCC (Multi-Version Concurrency Control)

Each row version contains hidden metadata:

| Field | Meaning |
|---------|----------|
| xmin | Transaction that created the row |
| xmax | Transaction that deleted the row |

#### UPDATE Behavior

PostgreSQL never overwrites a row.

Instead:

1. Create a new row version.
2. Mark the old version as expired.
3. Readers choose versions visible to their snapshot.

```text
Old Version (xmax set)
        |
        v
New Version (xmin set)
```

This allows:

- Readers and writers to proceed concurrently
- High transaction throughput
- Consistent snapshots

#### Why VACUUM Exists

MVCC creates dead row versions.

VACUUM:

- Reclaims storage
- Updates the visibility map
- Enables index-only scans
- Prevents transaction ID wraparound

---

### Write-Ahead Logging (WAL)

The core WAL rule:

```text
Log Before Data
```

Every change is written to WAL before the corresponding data page is written.

#### Commit Process

```text
Transaction
      |
Write WAL Record
      |
Flush WAL (fsync)
      |
COMMIT Success
```

Even if data pages remain only in memory, committed transactions survive crashes because WAL is already durable.

#### Recovery

After a crash:

1. Locate the most recent checkpoint.
2. Replay WAL records forward.
3. Restore the database to a consistent state.

#### Additional Uses

WAL also powers:

- Streaming Replication
- Point-in-Time Recovery (PITR)

---

### Query Planning

The Planner uses statistics stored in:

```text
pg_statistic
```

Examples of collected statistics:

- Row counts
- Most common values
- Histograms
- Correlation information

These statistics help PostgreSQL choose between:

- Sequential Scans
- Index Scans
- Hash Joins
- Merge Joins
- Nested Loop Joins

Stale statistics often produce poor plans, which is why running `ANALYZE` is important.

---

## 4. Design Trade-Offs

### Shared Buffer Cache

**Advantages**

- High cache hit rates
- Reduced disk I/O

**Costs**

- Requires synchronization and locking
- Needs an eviction algorithm

---

### MVCC

**Advantages**

- Excellent concurrency
- Readers rarely block writers

**Costs**

- Creates dead tuples
- Requires VACUUM
- Can cause table bloat

**Comparison with InnoDB**

| PostgreSQL | InnoDB |
|------------|---------|
| Old versions stay in table | Old versions stored in undo logs |
| Requires VACUUM | Requires purge thread |
| More table bloat | More undo-space overhead |

---

### Heap + Separate Indexes

**Advantages**

- Updates are relatively cheap
- Flexible storage layout

**Costs**

- Index lookups usually require an additional heap fetch

---

### WAL

**Advantages**

- Fast sequential logging
- Strong durability guarantees

**Costs**

- Data is effectively written twice
- Requires checkpoints and log management

---

### Process Per Connection

**Advantages**

- Strong isolation
- Stable architecture

**Costs**

- High connection counts consume memory
- Connection poolers such as PgBouncer are often required

---

## 5. Experiments / Observations

**Environment:** PostgreSQL 16.14

Test Data:

- customers: 2,000 rows
- orders: 35,000 rows

After loading data, I executed:

```sql
ANALYZE;
```

### Join Query Analysis

```text
HashAggregate (rows=2000) (actual time=18.8..18.9 rows=2000)
  Group Key: c.name
  -> Hash Join (cost=56.00..738.78 rows=21013) (actual rows=20996)
       Hash Cond: (o.customer_id = c.id)
       -> Seq Scan on orders o
            Filter: created_at >= '2026-01-01'
            Rows Removed by Filter: 14004
       -> Hash
            -> Seq Scan on customers c (rows=2000)

Planning Time: 3.236 ms
Execution Time: 19.541 ms
```

### Interpretation

- Estimated rows = 21,013
- Actual rows = 20,996

The estimates are extremely close, indicating that statistics are fresh and reliable.

The planner selected:

- Hash Join
- Sequential Scan on orders

Reason:

Although a date filter exists, most rows still qualify.

```text
35,000 total rows
14,004 removed
20,996 remaining
```

An index would not provide significant benefit in this case.

---

### Index Scan vs Sequential Scan

After creating:

```sql
CREATE INDEX idx_orders_cust
ON orders(customer_id);
```

#### Indexed Query

```sql
WHERE customer_id = 42
```

Plan:

```text
Bitmap Index Scan on idx_orders_cust
Execution Time: 0.046 ms
```

#### Non-Indexed Query

```sql
WHERE amount = 500
```

Plan:

```text
Seq Scan
Rows Removed by Filter: 34968
Execution Time: 2.131 ms
```

Observation:

The planner uses indexes when available and beneficial, otherwise it falls back to sequential scans.

---

## 6. Key Learnings

- Pages move through the Buffer Manager using hash lookup → pin → use, while clock-sweep selects eviction candidates.
- Delaying dirty-page writes is safe because WAL already contains the durable record.
- MVCC relies on row versions (`xmin` and `xmax`) rather than extensive locking.
- Dead row versions are the reason VACUUM exists.
- WAL guarantees durability through a simple rule: log before data.
- Recovery works by replaying WAL records from the latest checkpoint.
- Query plans depend heavily on accurate statistics.
- `EXPLAIN ANALYZE` is the best way to compare planner estimates with actual execution.
- PostgreSQL repeatedly trades memory usage and background maintenance for strong concurrency and durability.

---

## References

1. PostgreSQL Source Code
   - `src/backend/storage/buffer/`
   - `src/backend/access/nbtree/`

2. PostgreSQL Documentation
   - MVCC
   - WAL
   - Vacuuming
   - Planner Statistics
   - EXPLAIN

3. *The Internals of PostgreSQL*