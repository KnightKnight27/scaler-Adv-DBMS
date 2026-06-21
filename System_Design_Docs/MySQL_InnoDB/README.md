# MySQL InnoDB Storage Engine Analysis

## 1. Problem Background

MySQL's original default storage engine, MyISAM, had no transactions, no crash recovery, and only table-level locking — fine for simple read-heavy workloads, but unworkable for applications that needed correctness under concurrent writes (banking, e-commerce, anything with real money or real users editing data at the same time). InnoDB was built by Heikki Tuuri (Innobase Oy) specifically to solve that gap: a storage engine offering ACID-compliant transactions, MVCC, row-level locking, and crash recovery, pluggable into MySQL. Oracle acquired Innobase in 2005, and InnoDB became MySQL's default storage engine from MySQL 5.5 onward, replacing MyISAM as the safe-by-default choice.

This investigation studies InnoDB's internal mechanisms directly — clustered storage, secondary indexes, the buffer pool, undo/redo logging, and locking — using a simple `employees` table, and compares the resulting design against PostgreSQL's heap-based, WAL-driven architecture studied in Topic 2.

---

## 2. Architecture Overview

```text
Client Query
     ↓
SQL Layer
     ↓
InnoDB Storage Engine
     ├── Buffer Pool
     ├── Clustered Index
     ├── Secondary Indexes
     ├── Undo Logs
     ├── Redo Logs
     └── Lock Manager
```

A query enters through MySQL's SQL layer, which hands off storage and execution to the pluggable InnoDB engine underneath. InnoDB routes reads/writes through the Buffer Pool (its in-memory page cache), resolves row lookups via the Clustered Index (and Secondary Indexes when the query filters on a non-primary-key column), generates an Undo Log entry for every change (for rollback and MVCC), generates a Redo Log entry before the change is considered durable, and uses the Lock Manager to enforce row-level and gap locking for concurrent transactions.

---

## 3. Internal Design

### 3.1 Clustered Indexes & Primary Key Storage

A clustered index stores the actual table rows inside the leaf pages of the primary key B-Tree — the row data and the primary key index are the same physical structure, not two separate things.

```sql
CREATE TABLE employees (
    id INT PRIMARY KEY,
    name VARCHAR(50),
    department VARCHAR(50),
    salary INT
) ENGINE=InnoDB;
```

Since `id` is the primary key, InnoDB automatically clusters the table on it:

```text
PRIMARY KEY B-TREE

1 → Alice
2 → Bob
3 → Charlie
4 → David
5 → Eva
```

**Why this improves lookup performance:** because the row data lives directly in the leaf node, a primary-key lookup is a single B-Tree descent that ends with the actual row already in hand — no second structure to consult. This also gives strong cache locality for range scans on the primary key, since rows with adjacent keys are physically adjacent on disk.

### 3.2 Secondary Indexes

```sql
CREATE INDEX idx_department
ON employees(department);
```

Unlike the clustered index, a secondary index does not store full rows — only the indexed value plus a pointer back to the primary key:

```text
Indexed Value
+
Primary Key
```

```text
IT        → 1
IT        → 2
HR        → 3
Finance   → 4
Marketing → 5
```

A lookup via a secondary index is therefore a two-step process: find the matching primary key(s) in the secondary index, then perform a second lookup into the clustered index to retrieve the full row. This is the structural cost secondary indexes pay in InnoDB that they don't pay in a heap-organized table like PostgreSQL's.

### 3.3 Buffer Pool

The Buffer Pool is InnoDB's in-memory cache for table and index pages, reducing disk I/O.

```text
Disk
 ↓
Buffer Pool
 ↓
Query Execution
```

From `SHOW ENGINE INNODB STATUS\G`:

```text
Buffer pool size   8192
Free buffers       7034
Database pages     1154
Modified db pages  0
```

`Buffer pool size` is measured in **pages**, not bytes — at InnoDB's default 16 KB page size, 8192 pages corresponds to roughly 128 MB, which is InnoDB's classic default `innodb_buffer_pool_size`. Of those 8192 pages, 1154 currently hold cached database content and 0 are "dirty" (modified but not yet flushed to disk) — consistent with a small, lightly-loaded test workload where most of the pool remains free.

### 3.4 Undo Logs

Undo logs store the *previous* version of a row before it's modified — InnoDB's mechanism for rollback and for MVCC visibility (an older transaction with an earlier snapshot can read the undo log to reconstruct the version of the row it's entitled to see).

```sql
UPDATE employees
SET salary=60000
WHERE id=1;
```

Undo Log stores:

```text
Previous Salary = 50000
```

If the transaction aborts:

```sql
ROLLBACK;
```

the previous value (`50000`) is restored from the undo log.

### 3.5 Redo Logs

Redo logs record changes *before* the modified data pages themselves are written to disk — this is InnoDB's write-ahead logging mechanism, conceptually equivalent to PostgreSQL's WAL.

From InnoDB Status:

```text
Log sequence number      174862975
Log written up to        174862975
Log flushed up to        174862975
Last checkpoint at       174862975
```

All four values being equal indicates a quiescent system: every log record generated has been written, flushed to disk, and the checkpoint has caught up to the current log position — i.e., there's no pending unflushed redo work outstanding.

### 3.6 Why InnoDB Needs Both Undo and Redo Logs

```text
Undo Logs  →  Go Backward   →  Rollback, MVCC visibility
Redo Logs  →  Go Forward    →  Crash recovery, Durability
```

These solve two different problems and can't substitute for each other: undo logs let a transaction (or the system) *undo* a change that shouldn't have happened; redo logs let the system *redo* a change that should have happened but wasn't yet reflected on disk when the system crashed. A system with only redo logs couldn't roll back an in-progress transaction without crashing; a system with only undo logs couldn't recover committed-but-unflushed changes after a crash.

### 3.7 Row-Level Locking

```sql
BEGIN;

UPDATE employees
SET salary = 65000
WHERE id = 1;
```

Only the row matching `id = 1` is locked — other rows remain freely accessible to concurrent transactions. This is a direct structural advantage over table-level or page-level locking: higher concurrency, less blocking, better scalability under many simultaneous writers.

### 3.8 Gap Locks

```sql
SELECT *
FROM employees
WHERE id BETWEEN 5 AND 10
FOR UPDATE;
```

InnoDB locks both the existing rows in that range *and* the gaps between index entries:

```text
Existing Rows
+
Gaps Between Rows
```

Gap locks exist to prevent **phantom reads** — without them, another transaction could insert a new row with `id = 7` mid-range, and a repeated read within the same transaction would see a row that wasn't there before, violating the isolation guarantee.

### 3.9 Isolation Levels

InnoDB's default isolation level is **REPEATABLE READ**, which is exactly why gap locks exist at all: under REPEATABLE READ, a transaction must see a consistent set of rows for the *entire* transaction, including across range queries — gap locks are the mechanism that enforces this by blocking phantom inserts into a locked range.

PostgreSQL's default isolation level is **READ COMMITTED**, a weaker guarantee where each statement sees the latest committed data but phantoms between statements within the same transaction are possible. PostgreSQL achieves its stronger isolation levels (`REPEATABLE READ`, `SERIALIZABLE`) without gap locks — instead relying on its tuple-versioning/snapshot mechanism (Topic 2) to detect and reject conflicting concurrent writes (serialization failures) rather than physically locking the space between rows.

This is a real architectural fork: InnoDB prevents phantoms by *locking ranges in advance*; PostgreSQL prevents them by *detecting conflicts after the fact* and aborting one of the conflicting transactions.

### 3.10 Transaction Processing

```text
BEGIN
 ↓
Read Data
 ↓
Modify Data
 ↓
Undo Log Generated
 ↓
Redo Log Generated
 ↓
COMMIT
```

Every modification within a transaction generates both an undo log entry (so it can be rolled back) and a redo log entry (so it survives a crash once committed) before the transaction is allowed to commit — this ordering is what makes InnoDB ACID-compliant.

---

## 4. Design Trade-Offs

| Mechanism | What it buys you | What it costs |
|---|---|---|
| **Clustered Index** | Single B-Tree descent retrieves the full row directly — fast PK lookups, efficient range scans, strong cache locality | Secondary index lookups become two-step (index → clustered index), and the table must be physically reorganized as it grows since rows are stored in PK order |
| **Secondary Indexes** | Fast lookups on non-PK columns without storing full row copies | Every secondary index lookup pays an extra clustered-index hop to fetch the actual row |
| **Undo Logs** | Rollback support, MVCC visibility without bloating the live table | Long-running transactions cause undo chains to grow, increasing read/rollback cost |
| **Redo Logs** | Cheap, fast commits (sequential log write) and crash recovery | Data pages are still eventually written to disk separately — write amplification over time, same as PostgreSQL's WAL |
| **Row-Level + Gap Locking** | High concurrency, phantom-read prevention under REPEATABLE READ | Locking ranges in advance can reduce concurrency compared to PostgreSQL's optimistic, conflict-detection approach, especially under contention |

### InnoDB vs PostgreSQL

| Feature                | PostgreSQL               | InnoDB             |
| ----------------------- | ------------------------ | ------------------- |
| Storage Layout          | Heap Storage             | Clustered Storage    |
| Updates                 | Append New Tuple Version | In-place Update      |
| MVCC                    | Tuple Versioning         | Undo Log Based        |
| Cleanup                 | VACUUM                   | Purge Thread          |
| Primary Key Storage     | Separate Heap            | Clustered Index       |
| Secondary Index Lookup  | Direct Heap Pointer      | Primary Key Lookup    |
| Recovery                | WAL                      | Redo Logs             |
| Rollback                | Visibility Rules         | Undo Logs             |

**Why PostgreSQL chose a different MVCC model:**

PostgreSQL stores multiple tuple versions directly in the table itself.

*Advantages:* simple visibility checks (just look at `xmin`/`xmax` on the tuple itself), strong snapshot isolation, no dependency on undo chains.

*Disadvantages:* table bloat from old row versions, requires `VACUUM` to reclaim space.

InnoDB instead stores older row versions in separate Undo Logs, keeping the table itself compact.

*Advantages:* smaller tables, less storage bloat in the primary structure.

*Disadvantages:* more complex MVCC machinery, and undo chains grow longer (and more expensive to traverse) under heavy update workloads.

Neither approach is strictly better — PostgreSQL trades table compactness for visibility simplicity; InnoDB trades visibility/rollback simplicity for table compactness. Each is a deliberate, defensible engineering choice.

---

## 5. Experiments / Observations

### Clustered index lookup
```sql
EXPLAIN
SELECT *
FROM employees
WHERE id = 3;
```
```text
key = PRIMARY
type = const
rows = 1
```
The `PRIMARY` clustered index was used, with `type = const` (MySQL recognizes this as a single-row constant lookup against a unique key) and only **1** row examined. This confirms the efficiency of clustered storage for primary-key access — the row was retrieved in a single B-Tree descent.

### Secondary index lookup
```sql
EXPLAIN
SELECT *
FROM employees
WHERE department='IT';
```
```text
key = idx_department
type = ref
rows = 2
```
MySQL selected the `idx_department` secondary index (`type = ref`, a non-unique index lookup), expecting **2** matching rows. After locating the matching primary keys in the secondary index, InnoDB performed the additional clustered-index lookups needed to retrieve the full rows — exactly the two-step process described in Section 3.2.

### Buffer Pool state
```text
Buffer pool size   8192
Free buffers       7034
Database pages     1154
Modified db pages  0
```
**8192** total pages, **1154** holding live database content, **0** modified (dirty) at the time of observation — most of the pool remained free (**7034** buffers) because the test workload was small relative to the pool's capacity.

### Redo log state
```text
Log sequence number      174862975
Log written up to        174862975
Log flushed up to        174862975
Last checkpoint at       174862975
```
All four LSN values matching at **174862975** confirms a healthy, fully-flushed redo logging system with no outstanding unflushed work.

### InnoDB Monitor — transaction & row activity
```text
History list length 6
```
A history list length of **6** indicates undo history exists and is available for MVCC visibility — i.e., older row versions that some active transaction may still need to see.

```text
Number of rows inserted 5
updated 0
deleted 0
read 1
```
These figures match the experimental workload directly: **5** rows inserted (Alice through Eva), **0** updates and **0** deletes recorded at the time of this snapshot, and **1** row read (consistent with the `WHERE id = 3` lookup).

```text
Buffer pool size 8192
Database pages 1154
```
Repeats the buffer pool figures above, confirming pages were successfully cached in memory across the session.

```text
Log flushed up to 174862975
```
Confirms successful redo log flushing, consistent with the redo log state captured separately above.

---

## 6. Key Learnings

1. **Clustered storage collapses two lookups into one.** Because InnoDB stores row data directly in the primary key B-Tree's leaf pages, a PK lookup (`rows = 1`, `type = const`) needs only a single tree descent — there's no separate "index" and "table" to consult.
2. **Secondary indexes always cost an extra hop in InnoDB.** The `department='IT'` lookup (`type = ref`, `rows = 2`) had to resolve through the secondary index and then back into the clustered index — a structural cost PostgreSQL's heap-pointer secondary indexes don't pay the same way.
3. **Undo and redo logs solve opposite-direction problems and neither can substitute for the other** — undo enables going backward (rollback, MVCC visibility), redo enables going forward (crash recovery, durability).
4. **Gap locks are a direct consequence of InnoDB's default isolation level.** REPEATABLE READ requires phantom-read prevention across the whole transaction, and InnoDB achieves that by locking ranges; PostgreSQL achieves similar guarantees through conflict detection on its tuple-versioning system instead, without needing gap locks at all.
5. **PostgreSQL and InnoDB chose opposite trade-offs for the same underlying problem (MVCC):** PostgreSQL keeps the table simple (multiple tuple versions in place) at the cost of bloat and VACUUM; InnoDB keeps the table compact (undo logs hold history elsewhere) at the cost of MVCC and rollback complexity.
6. **A buffer pool reading of `8192` only makes sense once you know it's measured in pages, not bytes** — at the default 16 KB page size that's ~128 MB, InnoDB's classic default pool size; raw monitor numbers need their units made explicit to be meaningful in a report.
7. **`History list length 6` is a live signal of MVCC cost** — it's the same conceptual thing as PostgreSQL's `n_dead_tup`, just implemented as outstanding undo-log entries instead of dead heap tuples.