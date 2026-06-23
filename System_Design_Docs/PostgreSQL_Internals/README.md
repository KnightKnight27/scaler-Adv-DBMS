# PostgreSQL Internal Architecture

## 1. Problem Background

PostgreSQL internals must solve concurrent multi-user access at scale:
- **Buffer Manager**: Efficiently cache millions of pages in limited memory
- **B-Tree Indexes**: Fast lookups across billions of rows
- **MVCC**: Enable concurrent reads and writes without blocking
- **WAL**: Guarantee durability even after crashes

These subsystems are interdependent; understanding each clarifies why PostgreSQL scales to enterprise workloads.

## 2. Architecture Overview

```
Query Execution Layer
        ↓
Executor (Access Methods)
├─ Seq Scan (full table scan)
├─ Index Scan (via B-tree lookup)
└─ Index Only Scan (data from index)
        ↓
Concurrency Control (MVCC + Locking)
├─ Visibility checking (xmin/xmax)
├─ Snapshot isolation
└─ Row locks
        ↓
Storage Layer
├─ Buffer Manager (8 KB pages)
├─ B-Tree Indexes (separate files)
└─ Heap Storage (unordered tuples)
        ↓
Durability Layer
├─ WAL (Write-Ahead Log)
└─ Checkpoints
        ↓
Disk
```

## 3. Internal Design

### Buffer Manager (Shared Memory)

**Purpose**: Cache frequently-accessed pages to avoid disk I/O.

**Key Algorithm**: Clock algorithm with usage counters.
```
Buffer Pool: N × 8 KB pages (e.g., 128 MB = 16,384 pages)
Clock Hand: Advances when eviction needed
Usage Count: Incremented on each access, decremented on eviction
Result: Frequently-used pages stay in memory, rarely-used pages evicted
```

**Performance Impact**:
- Hit ratio > 99% = almost all reads from memory (fast)
- Hit ratio < 50% = half the queries go to disk (slow)
- Tuning: Set shared_buffers to 25% of available RAM

### B-Tree Index Structure

**Organization**: Balanced tree with leaves containing row pointers.
```
       Root
      /    \
  Internal Internal
   /  \    /  \
Leaf Leaf Leaf Leaf → Tuples in heap
```

**Page Layout**: Items sorted, free space in middle for splits.

**Operations**:
- **Search**: O(log n) traversal from root to leaf
- **Range Query**: Leaf nodes linked; sequential scan from lower to upper bound
- **Insert**: Find position, possibly split leaf and internal nodes
- **Delete**: Mark as deleted (VACUUM cleans later)

### MVCC (Multi-Version Concurrency Control)

**Principle**: Keep multiple versions of each row; transaction sees consistent snapshot.

**Tuple Headers**:
```
xmin: Transaction ID that inserted this version
xmax: Transaction ID that deleted this version (or ∞)
ctid: Pointer to newer version if updated
```

**Visibility Rule**: Transaction sees tuple if inserted before its snapshot and not deleted.

**Benefit**: Reader sees consistent data without acquiring locks; writer doesn't wait.

**Cost**: Multiple versions accumulate; VACUUM reclaims space asynchronously.

### WAL (Write-Ahead Logging)

**Principle**: Write log record to disk BEFORE modifying data page.

**Implementation**:
- WAL Buffer: In-memory staging area
- WAL Files: /pg_wal/ directory contains sequential log files
- Checkpoint: Records stable position for incremental recovery

**Recovery Process**:
1. Read last checkpoint (database was consistent)
2. Replay WAL records from checkpoint onward
3. Roll back incomplete transactions (no commit record)
4. Database recovers to last complete transaction

**LSN (Log Sequence Number)**: Tracks position in WAL for incremental recovery.

## 4. Design Trade-Offs

### Buffer Manager Trade-Off

**Clock Algorithm** (PostgreSQL choice):
- ✓ Scalable (minimal lock contention)
- ✓ Cache-friendly (no linked list updates)
- ✗ Not perfect LRU (occasionally suboptimal)

**Alternative (Strict LRU)**:
- ✓ Optimal cache semantics
- ✗ Mutex bottleneck at high concurrency

**Conclusion**: Scalability > Perfect LRU for shared server.

### MVCC Trade-Off

**Tuple Versioning** (PostgreSQL choice):
- ✓ Readers never block (see old versions)
- ✓ Writers never blocked by readers
- ✗ Bloat from accumulating versions
- ✗ VACUUM overhead (background cleanup)

**Alternative (In-Place Updates)**:
- ✓ Simple (no old versions)
- ✓ No background cleanup
- ✗ Readers block writers (lock needed on update)

**Conclusion**: Concurrency benefit worth bloat cost for multi-user workloads.

### WAL Trade-Off

**Log-Based Recovery** (PostgreSQL choice):
- ✓ Small writes (log record, not full page)
- ✓ Incremental recovery (from checkpoint, not full log)
- ✓ Enables replication (standby reads WAL)
- ✗ Complex state management
- ✗ WAL files accumulate (must be archived)

**Alternative (Atomic Writes)**:
- ✓ Simple (single file, atomic write)
- ✗ Must write full pages (inefficient)
- ✗ No replication support

**Conclusion**: WAL complexity justified for replication and efficiency.

## 5. Experiments & Observations

### Buffer Hit Ratio

```sql
SELECT sum(blks_hit) / (sum(blks_hit) + sum(blks_read)) AS hit_ratio
FROM pg_statio_user_tables;
-- Target: > 0.99 (99% of reads from buffer)
```

### Index Usage Statistics

```sql
SELECT relname, idx_scan, idx_tup_read, idx_tup_fetch
FROM pg_stat_user_indexes
ORDER BY idx_scan DESC;
-- Identifies which indexes are actually used
```

### VACUUM Progress

```sql
SELECT phase, heap_blks_scanned, heap_blks_vacuumed
FROM pg_stat_progress_vacuum;
-- Monitors VACUUM progress in long-running cleanup
```

### Query Plans with EXPLAIN ANALYZE

```sql
EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM large_table WHERE id = 5;
-- Shows estimated vs actual rows, memory usage
-- Buffers line: cache hits vs disk reads
```

## 6. Key Learnings

1. **Buffer Management scales through lock-free algorithms**: Clock algorithm trades perfect LRU for scalability.

2. **MVCC enables concurrency through versioning**: Multiple versions coexist; transactions see consistent snapshots without blocking.

3. **WAL optimizes durability through log-based recovery**: Small log writes replace full page writes; recovery replays from checkpoint.

4. **B-Tree indexes maintain balance automatically**: Insert/delete operations ensure O(log n) search despite dynamic updates.

5. **Design choices reflect multi-user requirements**: PostgreSQL trades off simplicity for correctness, scalability, and advanced features required in production databases.

6. **Operational tuning matters**: shared_buffers, autovacuum frequency, checkpoint intervals directly impact performance and require knowledge of internal architecture.

