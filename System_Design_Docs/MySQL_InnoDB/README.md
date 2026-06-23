# MySQL InnoDB Storage Engine

## 1. Problem Background

InnoDB is MySQL's default storage engine since 2010. It was designed to address limitations of earlier storage engines:

- **Earlier engines** (MyISAM): Table-level locks, no ACID, poor concurrency
- **InnoDB** (with Oracle acquisition): ACID compliance, row-level locks, crash recovery

InnoDB takes a fundamentally different architectural approach than PostgreSQL: **in-place updates with undo/redo logs** instead of **append-only versions**.

## 2. Architecture Overview

```
Query Execution
        ↓
Concurrency Control
├─ Intent Locks (table level)
├─ Record Locks (row level)
└─ Gap Locks (range level)
        ↓
Storage Layer
├─ Clustered Index (table sorted by primary key)
├─ Secondary Indexes (point to primary key)
└─ Buffer Pool (LRU with dirty page tracking)
        ↓
Transaction Management
├─ Undo Logs (for rollback and MVCC)
├─ Redo Logs (for durability)
└─ Double-Write Buffer (crash safety)
        ↓
Disk
```

## 3. Internal Design

### Clustered Index

Table itself is a B-tree sorted by primary key. Leaf nodes contain full row data, not just keys.

```
Traditional approach (PostgreSQL):
- Table: Unordered heap
- Index: B-tree pointing to rows
- Lookup: Index scan + heap fetch (2 I/Os)

InnoDB approach (Clustered):
- Table: B-tree sorted by primary key
- Lookup: B-tree traversal (1 I/O) ✓
- Secondary indexes point to primary key
```

**Benefit**: Primary key lookups and range scans are fast (data colocated).

**Cost**: Table organization fixed by primary key choice; secondary indexes larger.

### Undo and Redo Logs

**Undo logs**: Store old values for transaction rollback and MVCC snapshot reading.
- Created before each UPDATE
- Kept until all transactions seeing old version complete
- Traversed to reconstruct old row versions

**Redo logs**: Store changes for durability (crash recovery).
- Created before UPDATE applied to disk
- Replayed from checkpoint on recovery
- Ensures all committed changes persist

**Together**: Undo + redo provide ACID without full-page writes.

### Row-Level Locking with Hierarchy

- **Intent Locks** (I-Lock, IX-Lock): Signals that row locks will follow
- **Record Locks**: Lock specific rows
- **Gap Locks**: Prevent phantom inserts (prevent range violations)

**Benefit**: High concurrency (different transactions can update different rows).

**Cost**: Deadlock risk; InnoDB detects and rolls back one transaction.

### Double-Write Buffer

Crash safety mechanism: Write each page to buffer, then to actual location.

**Without it**: Crash during page write → corrupted page on disk.

**With it**: Crash during page write → page recoverable from double-write buffer.

**Cost**: 2x write I/O for durability guarantee.

### Buffer Pool & Dirty Page Management

LRU-based buffer pool similar to PostgreSQL, plus:
- **Dirty pages**: Modified but not written to disk
- **Clean pages**: Unmodified
- **Free pages**: Unused

Write-ahead logging (redo logs) ensures dirty pages eventually flushed to disk.

## 4. Design Trade-Offs

### Clustered Index Trade-Off

**InnoDB choice**:
- ✓ Fast primary key lookups (1 I/O)
- ✓ Efficient range scans on primary key
- ✗ Table sort order fixed
- ✗ Secondary indexes larger

**Alternative** (PostgreSQL):
- ✓ Flexible table order
- ✗ Extra I/O for non-PK lookups

**Conclusion**: Optimization for common case (PK access) vs. flexibility.

### In-Place Updates Trade-Off

**InnoDB choice**:
- ✓ Small undo/redo logs
- ✓ Efficient for many small updates
- ✗ Complex recovery (undo chain)
- ✗ Deadlock risk

**Alternative** (PostgreSQL tuple versioning):
- ✓ Simple: Multiple versions on heap
- ✗ Bloat from accumulating versions
- ✗ VACUUM overhead

**Conclusion**: Space efficiency for typical OLTP workloads.

### Row Locks vs Table Locks

**InnoDB choice**:
- ✓ Concurrent writes to different rows
- ✗ Deadlock detection overhead
- ✗ More complex lock state

**Alternative** (table locks):
- ✓ Simple
- ✗ No concurrent writes

**Conclusion**: Concurrency worth complexity.

## 5. Experiments & Observations

### Buffer Pool Hit Ratio

```sql
SELECT (innodb_buffer_pool_read_requests) / 
       (innodb_buffer_pool_read_requests + innodb_buffer_pool_reads) 
       AS hit_ratio
FROM information_schema.INNODB_CMP_PER_INDEX;
-- Target: > 0.99
```

### Undo Log Size

```sql
SELECT innodb_undo_tablespaces, 
       innodb_undo_log_truncate_size
FROM information_schema.INNODB_TRXS;
-- Monitor undo accumulation
```

### Lock Waits and Deadlocks

```sql
SELECT * FROM INFORMATION_SCHEMA.INNODB_LOCKS;
SELECT * FROM INFORMATION_SCHEMA.INNODB_LOCK_WAITS;
-- Debug deadlock scenarios
```

## 6. Key Learnings

1. **Clustered indexes optimize for primary key access**: Single I/O for PK lookups, efficient range scans.

2. **In-place updates with undo logs trade complexity for space efficiency**: Suitable for OLTP workloads with many small updates.

3. **Hierarchical locking balances concurrency with deadlock detection**: Intent locks make checking compatible locks fast.

4. **Double-write buffer provides absolute crash safety**: Extra write I/O worth the guarantee.

5. **InnoDB vs PostgreSQL represents different trade-offs**: PostgreSQL emphasizes flexibility and read concurrency; InnoDB emphasizes update efficiency and space.

6. **Undo log management is critical operational concern**: Long-running transactions prevent undo purging; requires monitoring and tuning.

