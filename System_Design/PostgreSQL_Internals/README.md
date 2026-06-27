# System Design Study: PostgreSQL Internals

**Name:** Pratham Onkar Singh

**Roll No:** 24bcs10136


A database engine is fundamentally an exercise in resource management. Operating system RAM is fast but wipes clean when power is lost; spinning disks and SSDs hold data permanently but are thousands of times slower to read and write. On top of that, dozens of client connections may try to read and modify the exact same data simultaneously.

PostgreSQL solves these physical bottlenecks through four tightly integrated subsystems:

- Buffer Manager
- `nbtree` indexing engine
- Multi-Version Concurrency Control (MVCC)
- Write-Ahead Logging (WAL)

This document explores how these internal mechanisms work together.

---

# 1. Problem Background

Early database systems handled consistency using brute force.

If one user updated a record, the engine locked the entire table. Any other user attempting to read from that table simply waited.

Even worse, if power failed halfway through writing a page to disk, the database could become permanently corrupted.

PostgreSQL's architecture was designed around three engineering goals:

1. **Zero Read-Write Blocking**
   - Readers must never block writers.
   - Writers must never block readers.

2. **Crash Resilience**
   - Unexpected failures should never leave partially written pages.

3. **Memory Bus Optimization**
   - RAM should be the primary working area.
   - Slow storage should be accessed only when necessary.

---

# 2. Architecture Overview

When a client sends a SQL query, PostgreSQL does **not** access table files directly.

Instead, the request flows through several internal components.

```text
+------------------------------------------------------------------+
| Client Connection (SQL Query)                                    |
+------------------------------------------------------------------+
                              │
                              ▼
+------------------------------------------------------------------+
| Query Planner & Optimizer                                        |
| Reads statistics from pg_statistic                               |
+------------------------------------------------------------------+
                              │
                              ▼
+------------------------------------------------------------------+
| Executor                                                         |
| Uses nbtree index to resolve physical Tuple ID (TID)             |
+------------------------------------------------------------------+
                              │
                              ▼
+------------------------------------------------------------------+
| Buffer Manager                                                   |
| Searches shared_buffers for the required 8 KB page               |
+------------------------------------------------------------------+
              │                               │
        Cache Hit                       Cache Miss
              │                               │
              ▼                               ▼
     Return Memory Pointer          Clock Sweep Eviction
                                            │
                                            ▼
                                   Fetch 8 KB Page from Disk
```

### Query Execution Flow

1. The **Planner** parses SQL and chooses the cheapest execution strategy.
2. The **Executor** queries the B-Tree (`nbtree`) index to locate the row's **Tuple ID (TID)**.
3. The **Buffer Manager** checks whether the required page already exists inside `shared_buffers`.
4. If present, the page is returned immediately.
5. Otherwise, Clock Sweep selects a victim page, loads the requested page from disk, and returns it.

---

# 3. Internal Design

## 3.1 Buffer Manager (`src/backend/storage/buffer/`)

Backend worker processes are **never allowed to access database files directly**.

Every read and write passes through a shared memory cache named:

```text
shared_buffers
```

Each cached page occupies **8 KB**.

### Why Standard LRU Fails

Traditional software often uses **Least Recently Used (LRU)** caching.

In databases, this performs poorly.

Suppose someone executes:

```sql
SELECT *
FROM five_year_transaction_history;
```

A traditional LRU cache would evict frequently accessed production pages simply to cache historical pages that may never be used again.

---

### Clock Sweep Algorithm

PostgreSQL replaces LRU with the **Clock Sweep** algorithm.

Each buffer stores:

- `usage_count` (0–5)
- Pin count

When loading a new page:

- **Pinned page** → Skip
- **usage_count > 0** → Decrement and continue
- **usage_count == 0** → Evict

This approach approximates LRU while preventing large analytical scans from destroying cache locality.

---

### Ring Buffer Isolation

Large sequential scans bypass the normal cache.

Instead, PostgreSQL allocates a small private **Ring Buffer** (typically **256 KB**) for those scans.

This prevents one analytical query from evicting frequently accessed transactional pages.

---

## 3.2 B-Tree Implementation (`nbtree`)

Traditional B-Trees struggle under heavy concurrent inserts.

If a page fills up, the parent node usually must be locked while splitting.

PostgreSQL instead implements the **Lehman & Yao B-Tree**, introducing **Right-Link pointers**.

```text
                [ Parent Interior Node ]
                          │
        ┌─────────────────┴─────────────────┐
        ▼                                   ▼
 [ Left Leaf Node ] ─── Right-Link ───► [ Right Leaf Node ]
 (High Key: 100)                         (Keys > 100)
```

### Page Layout

Each index page occupies one **8 KB** block.

Its trailing area (`BTPageOpaqueData`) stores:

- Tree level
- Left sibling
- Right sibling

Each index tuple stores:

- Indexed key
- Physical heap **Tuple ID (TID)**

---

### Concurrent Page Splits

Imagine:

- Worker A searches for `id = 150`.
- Worker B simultaneously inserts data.
- Page 8 splits into Page 8 and Page 9.

Instead of restarting:

1. Worker A notices:

```text
150 > High Key (100)
```

2. It follows the **Right-Link**.
3. The search continues on Page 9.

No upper-tree lock is required.

---

## 3.3 Multi-Version Concurrency Control (MVCC)

PostgreSQL never overwrites existing rows.

Instead:

- Old tuple remains.
- Old tuple receives an expiration transaction.
- New tuple is inserted elsewhere.

Each tuple stores:

- `t_xmin` — Creating Transaction ID
- `t_xmax` — Deleting/Updating Transaction ID (`0` if still live)

---

### Transaction Snapshots

When a query begins, PostgreSQL generates a snapshot:

```text
xmin:xmax:xip_list
```

This snapshot tells the executor:

- Which transactions are committed
- Which are still active
- Which occur in the future

Visibility is determined entirely by comparing tuple headers with the snapshot.

---

### Why `VACUUM` Is Mandatory

Since updates leave previous versions behind, dead tuples accumulate.

Without maintenance:

### 1. Storage Bloat

Tables continue growing indefinitely.

Sequential scans become slower because dead tuples must be skipped.

### 2. Transaction ID Wraparound

Transaction IDs are 32-bit integers.

Eventually they wrap around.

Without freezing old transaction IDs, PostgreSQL could mistakenly treat old tuples as future transactions.

To prevent this, **autovacuum** continuously:

- Removes dead tuples
- Updates the Free Space Map
- Freezes old XIDs

---

## 3.4 Write-Ahead Logging (WAL)

PostgreSQL follows the **Write-Ahead Logging Rule**:

> A modified data page may never reach disk before its corresponding WAL record is safely persisted.

### Commit Workflow

When executing:

```sql
COMMIT;
```

The following occurs:

1. `walwriter` appends the transaction to `pg_wal/`.
2. `fsync()` guarantees persistence.
3. PostgreSQL reports success.
4. Modified table pages remain dirty in RAM.

---

### Checkpointing

The background **checkpointer** periodically:

- Flushes dirty pages
- Writes a `CHECKPOINT` record into the WAL

---

### Crash Recovery

After an unexpected shutdown:

1. PostgreSQL locates the latest checkpoint.
2. Replays every WAL record after that checkpoint (**REDO**).
3. Ignores incomplete transactions because MVCC naturally hides them.

Therefore, PostgreSQL does **not** require a traditional UNDO phase.

---

# 4. Design Trade-Offs

| Architectural Choice | Primary Advantage | Engineering Trade-Off |
|----------------------|-------------------|-----------------------|
| **Append-Only MVCC** | Fast rollbacks and simple crash recovery | Dead tuples require continuous `VACUUM` maintenance |
| **Out-of-Place Updates** | Readers never block writers | Updating indexed columns increases index write amplification (unless HOT optimization applies) |
| **Clock Sweep Buffer Pool** | Protects frequently accessed pages from analytical scans | Data often exists simultaneously in both `shared_buffers` and the operating system page cache |

---

# 5. Experiments & Observations

To observe planner behavior, we created a simple schema.

## Test Schema

```sql
CREATE TABLE departments (
    dept_id SERIAL PRIMARY KEY,
    dept_name TEXT
);

CREATE TABLE employees (
    emp_id SERIAL PRIMARY KEY,
    dept_id INT REFERENCES departments(dept_id),
    salary NUMERIC
);

INSERT INTO departments (dept_name)
VALUES
('Backend'),
('Frontend'),
('DevOps');

INSERT INTO employees (dept_id, salary)
VALUES
(1, 120000),
(1, 95000),
(2, 105000),
(3, 130000);

ANALYZE departments;
ANALYZE employees;
```

### Execution Plan

```sql
EXPLAIN (ANALYZE, BUFFERS)
SELECT
    d.dept_name,
    e.salary
FROM departments d
JOIN employees e
ON d.dept_id = e.dept_id;
```

### Output

```text
Hash Join  (cost=1.04..2.12 rows=4 width=40)
(actual time=0.031..0.034 rows=4 loops=1)

Hash Cond: (e.dept_id = d.dept_id)

Buffers: shared hit=2

-> Seq Scan on employees
   Buffers: shared hit=1

-> Hash
   Buckets: 1024
   Batches: 1
   Memory Usage: 9 kB

   Buffers: shared hit=1

   -> Seq Scan on departments
      Buffers: shared hit=1

Planning:
  Buffers: shared hit=168

Planning Time: 0.412 ms

Execution Time: 0.065 ms
```

---

## Observations & Analysis

### Join Algorithm Selection

The planner selected a **Hash Join**.

Execution steps:

1. Scan `departments`.
2. Build an in-memory hash table using **9 KB**.
3. Scan `employees`.
4. Probe the hash table.

For small datasets, this is more efficient than traversing B-Tree indexes.

---

### Execution vs. Planning Buffers

Actual execution required:

- **2 shared buffer hits**

Planning required:

- **168 shared catalog buffer hits**

Most planning work involved:

- Reading catalog metadata
- Checking foreign keys
- Reading statistics
- Resolving data types

---

### Catalog Statistics Integration

Planner estimates:

```text
Estimated rows = 4
Actual rows = 4
```

The close estimate comes from `ANALYZE`, which stores statistics inside `pg_statistic`.

These statistics include:

- `n_distinct`
- Histograms
- Null fractions

Accurate statistics allow PostgreSQL to choose an efficient **Hash Join**.

Stale statistics could instead lead to a slower **Nested Loop Join**.

---

# 6. Key Learnings

## RAM Is Heavily Guarded

Using:

- Clock Sweep
- Ring Buffer isolation

PostgreSQL carefully protects frequently accessed pages from being displaced by large analytical workloads.

---

## MVCC Trades Storage for Concurrency

By writing new tuple versions instead of overwriting existing rows:

- Readers never block writers.
- Writers never block readers.

The trade-off is increased storage usage and the ongoing need for `VACUUM`.

---

## WAL Decouples Durability from Latency

PostgreSQL provides durable commits without immediately flushing table pages.

Instead:

1. WAL records are written sequentially.
2. Clients receive fast commit acknowledgements.
3. Dirty pages are written later by background processes.

This architecture balances durability with high throughput.