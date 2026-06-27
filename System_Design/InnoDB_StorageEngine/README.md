# System Design Analysis: MySQL InnoDB Storage Engine

**Name:** Pratham Onkar Singh

**Roll No:** 24bcs10136

For years, MySQL was widely viewed as a fast but relatively basic database. Its original default storage engine, **MyISAM**, lacked transaction support and relied on table-level locks.

If an application crashed while writing data, files could become corrupted. Likewise, concurrent updates to different rows in the same table forced users into a waiting queue.

**InnoDB** transformed MySQL by introducing:

- ACID transactions
- Row-level locking
- Multi-Version Concurrency Control (MVCC)

This report explores InnoDB's internal architecture and compares its design with PostgreSQL.

---

# 1. Problem Background

Transactional systems such as payment gateways and reservation platforms must guarantee both:

- Correctness (ACID)
- High throughput under heavy concurrent access

InnoDB was engineered around three primary goals:

1. **Disk Seek Minimization**
   - Primary-key lookups should require as few disk accesses as possible.

2. **In-Place Row Updates**
   - Small modifications should avoid relocating entire rows.

3. **Phantom Read Prevention**
   - Transactions should not suddenly observe newly inserted matching rows.

---

# 2. Architecture Overview

Unlike PostgreSQL—which runs as multiple operating system processes—InnoDB is integrated directly into the MySQL server as a pluggable storage engine.

```text
+---------------------------------------------------------------------+
| MySQL Server                                                        |
| Connection Handling • SQL Parser • Optimizer                        |
+---------------------------------------------------------------------+
                              │
                              ▼
+---------------------------------------------------------------------+
| InnoDB Storage Engine                                               |
|                                                                     |
| Buffer Pool                                                         |
| ├── Young Sublist (5/8)                                             |
| ├── Old Sublist (3/8)                                               |
| ├── Redo Log Buffer                                                 |
| └── Undo Page Cache                                                 |
+---------------------------------------------------------------------+
                              │
                              ▼
+---------------------------------------------------------------------+
| Physical Storage                                                    |
| ├── Tablespaces (.ibd)                                              |
| ├── Redo Logs (ib_logfile*)                                         |
| └── Undo Tablespaces                                                |
+---------------------------------------------------------------------+
```

### Query Flow

1. The MySQL server parses and optimizes SQL.
2. The execution plan is passed to InnoDB.
3. InnoDB performs reads and writes inside the **Buffer Pool**.
4. Slow disk writes are deferred through logging and background flushing.

---

# 3. Internal Design

## 3.1 Clustered Index Storage

Unlike PostgreSQL, where tables and indexes are separate structures, **InnoDB stores the table inside the primary index**.

Each table is organized as a **Clustered B+Tree** using **16 KB pages**.

### Primary Key Storage

The leaf pages of the clustered index contain:

- Primary key
- Entire row payload

If no primary key exists:

1. InnoDB searches for a unique NOT NULL index.
2. If none exists, it automatically creates a hidden 6-byte row identifier.

---

### Secondary Indexes

Secondary indexes do **not** store physical row addresses.

Instead, each secondary index stores:

- Indexed value
- Corresponding primary key

---

### Double Lookup

Suppose the query is:

```sql
SELECT *
FROM inventory
WHERE sku = 'LAPTOP-99';
```

Execution occurs in two steps:

1. Traverse the secondary index to locate the primary key.
2. Traverse the clustered primary index to retrieve the complete row.

---

## 3.2 Buffer Pool Scan Resistance

InnoDB caches:

- Table pages
- Index pages
- Internal metadata

inside the **Buffer Pool** (`innodb_buffer_pool_size`).

### Why Traditional LRU Fails

A nightly report scanning a massive table could otherwise evict frequently accessed production pages.

To avoid this, InnoDB divides the cache into two regions.

### Young Sublist (5/8)

Stores frequently reused pages.

### Old Sublist (3/8)

Stores newly loaded pages.

New pages always enter the **Old Sublist**.

Only if the page is accessed again after approximately **1000 ms** is it promoted into the Young Sublist.

This prevents one-time scans from polluting the cache.

---

## 3.3 Transaction Log Pipeline

To satisfy ACID guarantees while modifying pages directly in memory, InnoDB maintains two independent logging systems.

```text
                 Transaction Begins
                        │
                        ▼
          Write Undo Log Record
                        │
                        ▼
      Modify Buffer Pool Page In Place
                        │
                        ▼
      Append Physical Changes to
           Redo Log Buffer
                        │
                        ▼
             Flush Redo Log to Disk
```

### Undo Logs

Undo logs record **logical reverse operations**.

Example:

```text
Stock changed:

50 → 40

Undo record:

Restore stock to 50
```

Undo logs enable:

- Transaction rollback
- MVCC snapshots
- Non-blocking readers

---

### Redo Logs

Redo logs record **physical page modifications**.

Example:

```text
Page 814

Offset 32

Write bytes 0xCC
```

Redo logs provide:

- Crash recovery
- Durable commits
- Fast sequential writes

When a transaction commits:

1. Redo records are flushed.
2. Data pages remain dirty in memory.
3. Background threads later flush pages to tablespaces.

---

## 3.4 Next-Key Locking & Phantom Reads

InnoDB implements several lock types.

### Record Lock

Locks one specific index entry.

### Gap Lock

Locks the empty space between index records.

### Next-Key Lock

Combines:

- Record Lock
- Gap Lock

---

### Phantom Read Prevention

Default isolation level:

```text
REPEATABLE READ
```

Suppose a transaction executes:

```sql
SELECT *
FROM products
WHERE price BETWEEN 100 AND 200
FOR UPDATE;
```

InnoDB locks:

- Existing matching rows
- The gaps between those rows

Another transaction attempting to insert:

```text
price = 150
```

must wait until the first transaction commits.

This completely prevents phantom reads.

---

# 4. Design Trade-Offs

| Architectural Dimension | MySQL / InnoDB | PostgreSQL |
|--------------------------|----------------|------------|
| **Row Modification** | In-place updates | Append-only MVCC |
| **Historical Versions** | Stored in Undo Segments | Stored directly inside heap pages |
| **Garbage Collection** | Background Purge Thread | `VACUUM` |
| **Secondary Index Cost** | Every secondary index stores the primary key | Updating rows can require index pointer updates (unless HOT optimization applies) |
| **Primary-Key Lookup** | Very fast because row data resides inside clustered index | Requires index lookup followed by heap lookup |

---

# 5. Experiments & Observations

## Test Schema

```sql
CREATE TABLE inventory (
    item_id INT PRIMARY KEY,
    product_name TEXT,
    sku VARCHAR(50),
    INDEX idx_sku (sku)
) ENGINE=InnoDB;

INSERT INTO inventory
VALUES
(
    1,
    'Mechanical Keyboard',
    'KEY-M-01'
);
```

---

## Tablespace Footprint

Query:

```sql
SELECT
    FILE_SIZE,
    ALLOCATED_SIZE
FROM information_schema.innodb_tablespaces
WHERE NAME LIKE '%inventory%';
```

Result:

```text
FILE_SIZE      = 114688 bytes
ALLOCATED_SIZE = 114688 bytes
```

### Observation

Despite containing only one row, the `.ibd` file occupies **112 KB**.

This is because InnoDB allocates storage using structured extents containing:

- Header pages
- Bitmap pages
- Inode pages
- Index root pages

This layout improves filesystem alignment and future growth.

---

## Execution Plan (EXPLAIN)

Query:

```sql
EXPLAIN
SELECT
    item_id,
    product_name
FROM inventory
WHERE sku = 'KEY-M-01';
```

Output:

```text
id: 1
select_type: SIMPLE
table: inventory

type: ref

possible_keys: idx_sku

key: idx_sku

ref: const

rows: 1

Extra: NULL
```

### Analysis

Because `product_name` is **not stored inside the secondary index**, InnoDB performs a **double lookup**:

1. Traverse `idx_sku`
2. Retrieve the primary key
3. Traverse the clustered primary index
4. Read `product_name`

---

If the query changes to:

```sql
SELECT item_id
FROM inventory
WHERE sku = 'KEY-M-01';
```

`EXPLAIN` reports:

```text
Extra: Using index
```

Since the secondary index already stores the primary key, InnoDB answers the query entirely from the index without reading the clustered table pages.

This is known as an **Index-Only Scan** (or **Covering Index** access).

---

# 6. Key Learnings

## Why Does InnoDB Need Both Undo and Redo Logs?

They solve different problems.

### Redo Logs

Provide:

- Durability
- Fast commits
- Crash recovery

They record **physical page changes**.

---

### Undo Logs

Provide:

- Transaction rollback
- MVCC
- Consistent snapshots

They record **logical reverse operations**.

---

## What Advantages Do Clustered Indexes Provide?

Clustered indexes store entire rows directly inside the primary-key B+Tree.

Advantages include:

- No additional heap lookup
- Excellent primary-key performance
- Strong cache locality for range scans

For example:

```sql
WHERE item_id BETWEEN 10 AND 50
```

often accesses adjacent rows stored on the same 16 KB pages.

---

## Why Did PostgreSQL Choose a Different MVCC Model?

PostgreSQL chose an append-only heap architecture instead of Undo tablespaces.

Advantages include:

- Simpler crash recovery
- Flexible storage architecture
- Easier support for diverse index types

Because PostgreSQL indexes point to generic **Tuple IDs (TIDs)** rather than clustered primary keys, advanced index types such as:

- B-Tree
- GIN
- GiST
- BRIN

can all coexist using the same storage abstraction.