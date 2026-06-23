# Lab 6: Transaction Manager - MVCC + 2PL + Deadlock Detection

**Student:** Pulasari Jai  
**Roll No:** 24BCS10656  
**Date:** June 23, 2026

---

## Overview

This lab implements a complete transaction manager combining three critical concurrency control mechanisms used in PostgreSQL:

1. **MVCC (Multi-Version Concurrency Control)** - Version chains for snapshot isolation
2. **Strict 2PL (Two-Phase Locking)** - Lock protocol with growing/shrinking phases
3. **Deadlock Detection** - Waits-for graph cycle detection

Together, these enable high-concurrency ACID transactions with snapshot isolation.

---

## Objectives

1. ✅ Implement MVCC version chains (xmin, xmax, visibility rules)
2. ✅ Implement Strict 2PL (locks held until commit/abort)
3. ✅ Implement deadlock detection via cycle detection
4. ✅ Demonstrate snapshot isolation (readers don't block writers)
5. ✅ Handle concurrent transactions with proper isolation

---

## Directory Structure

```
lab_6/
├── README.md                # This file
├── transaction_manager.cpp  # Complete implementation
├── compile.sh               # Build script
└── transaction_manager      # Compiled binary
```

---

## Architecture

### Component Interaction

```
┌──────────────────────────────────────────────┐
│  TransactionManager (Public API)             │
│  - begin(), commit(), abort()                │
│  - read(), insert(), update(), remove()      │
└────────────┬─────────────────┬───────────────┘
             │                 │
             ↓                 ↓
┌────────────────────┐  ┌────────────────────┐
│   Lock Manager     │  │   MVCC Heap        │
│   (Strict 2PL)     │  │   (Version Chains) │
│                    │  │                    │
│ - SHARED locks     │  │ - xmin (creator)   │
│ - EXCLUSIVE locks  │  │ - xmax (deleter)   │
│ - Deadlock detect  │  │ - Visibility rules │
└────────────────────┘  └────────────────────┘
```


---

## Part 1: MVCC (Multi-Version Concurrency Control)

### Concept

Instead of overwriting data, MVCC creates a new version for each update. Each version has:
- **xmin:** Transaction ID that created this version
- **xmax:** Transaction ID that deleted/updated this version (0 = still live)

### Version Chain

```
Key: "balance"

Version Chain (newest → oldest):
┌─────────────────────────────────┐
│ value=3000, xmin=6, xmax=0      │ ← Current (created by TX 6)
├─────────────────────────────────┤
│ value=2000, xmin=3, xmax=6      │ ← Deleted by TX 6
├─────────────────────────────────┤
│ value=1000, xmin=1, xmax=3      │ ← Deleted by TX 3
└─────────────────────────────────┘
```

### Visibility Rules

A version is visible to transaction T if:

```cpp
bool is_visible(version, snapshot_xid, reader_xid) {
    // 1. Version must be created by committed transaction
    //    (or by reader itself for "own writes")
    if (version.xmin != reader_xid) {
        if (!is_committed(version.xmin))
            return false;
        if (version.xmin >= snapshot_xid)
            return false;
    }
    
    // 2. Version must not be deleted (or deletion not visible)
    if (version.xmax == 0)
        return true;  // Not deleted
    
    if (version.xmax == reader_xid)
        return false;  // We deleted it
    
    if (is_committed(version.xmax) && version.xmax < snapshot_xid)
        return false;  // Deletion is visible
    
    return true;
}
```

### Snapshot Isolation

Each transaction gets a snapshot XID at BEGIN:
```cpp
TxID snapshot_xid = current_xid;
// Transaction sees all commits < snapshot_xid
```

**Benefits:**
- ✅ Readers never block writers
- ✅ Writers never block readers  
- ✅ Consistent read view throughout transaction

---

## Part 2: Strict Two-Phase Locking (2PL)

### Phases

```
Transaction Lifecycle:

GROWING PHASE           SHRINKING PHASE
(acquire locks)         (release locks)
       │                       │
       │ Lock A                │
       │ Lock B                │
       │ ...                   │
       │                       │
       └───────────────────────┼─ COMMIT/ABORT
                               │
                               │ Release all locks
                               │
```

**Strict 2PL:** Shrinking phase only happens at commit/abort (not during execution).

### Lock Modes

| Mode | Shared (S) | Exclusive (X) |
|------|------------|---------------|
| **S** | ✅ Compatible | ❌ Conflict |
| **X** | ❌ Conflict | ❌ Conflict |

### Implementation

```cpp
acquire_lock(key, xid, mode):
    1. Check if in shrinking phase → ERROR
    2. Check if lock already held
    3. Add request to queue
    4. Wait until no conflicts
    5. Grant lock

release_locks(xid):
    1. Mark transaction in shrinking phase
    2. Remove all locks held by xid
    3. Notify waiting transactions
```


---

## Part 3: Deadlock Detection

### Waits-For Graph

```
Transaction T8 holds lock on A, wants lock on B
Transaction T9 holds lock on B, wants lock on A

Waits-For Graph:
    T8 → T9
    T9 → T8

Cycle detected! → Abort one transaction
```

### Algorithm

```cpp
1. Maintain waits-for graph: waiter → {holders}
2. When transaction blocks:
   - Add edges to graph
   - Run DFS cycle detection
   - If cycle found → throw DeadlockException
3. Abort younger transaction
```

### DFS Cycle Detection

```cpp
bool has_cycle(start, graph):
    visited = {}, stack = {}
    
    dfs(node):
        visited.add(node)
        stack.add(node)
        
        for neighbor in graph[node]:
            if neighbor in stack:
                return true  // Back edge = cycle
            if neighbor not in visited:
                if dfs(neighbor):
                    return true
        
        stack.remove(node)
        return false
    
    return dfs(start)
```

**Time Complexity:** O(V + E) where V = transactions, E = wait edges

---

## Testing Results

### Scenario 1: MVCC Snapshot Isolation

```
T1: INSERT balance = 1000 → COMMIT
T2: BEGIN (snapshot_xid = 2)
T3: BEGIN → UPDATE balance = 2000 → COMMIT
T2: READ balance

Expected: 1000 (T2's snapshot doesn't see T3's commit)
Got: 1000 ✅
```

**Analysis:** T2's snapshot was taken before T3 committed, so T2 sees the old version even though T3 committed.

### Scenario 2: Concurrent Shared Locks

```
T4: READ balance  → SHARED lock granted
T5: READ balance  → SHARED lock granted (no conflict)
```

**Analysis:** Multiple transactions can hold shared locks simultaneously. Readers don't block readers.

### Scenario 3: Exclusive Lock Blocks Others

```
T6: UPDATE balance = 3000  → EXCLUSIVE lock acquired
T7: READ balance           → BLOCKS (waits for T6)
T6: COMMIT                 → Releases lock
T7: Lock granted           → READ balance = 3000
```

**Analysis:** Exclusive lock prevents all other access. T7 blocks until T6 releases the lock at commit.

### Scenario 4: Deadlock Detection

```
T8: Lock A (granted)
T9: Lock B (granted)
T8: Try lock B → BLOCKS (waits for T9)
T9: Try lock A → DEADLOCK DETECTED → ABORT T9
T8: Gets lock B → COMMIT
```

**Analysis:** Waits-for graph detects cycle (T8→T9→T8). System aborts T9 (younger transaction) to break the cycle.

---

## Building and Running

### Compile

```bash
chmod +x compile.sh
./compile.sh
```

Or manually:
```bash
g++ -std=c++17 -pthread -O2 -Wall -Wextra -o transaction_manager transaction_manager.cpp
```

**Note:** `-pthread` flag required for threading support!

### Run

```bash
./transaction_manager
```

---

## Key Concepts Demonstrated

### 1. Snapshot Isolation

```
Timeline:
T1: ───INSERT───COMMIT───
T2: ──────BEGIN──────────READ───COMMIT
T3: ────────BEGIN──UPDATE──COMMIT

T2 reads old value because:
- T2's snapshot_xid = 2
- T3's xmin = 3
- 3 >= 2 → not visible to T2
```

### 2. Lock Compatibility Matrix

| Holder\Request | SHARED | EXCLUSIVE |
|----------------|--------|-----------|
| **SHARED** | ✅ Grant | ⏸️ Wait |
| **EXCLUSIVE** | ⏸️ Wait | ⏸️ Wait |
| **None** | ✅ Grant | ✅ Grant |

### 3. Deadlock Cycle

```
    T8 ──wants──→ B
     ↑            │
     │            │ held by
  holds A         ↓
     │            T9
     │            │
     └──wants─────┘

Cycle: T8 → T9 → T8
```


## Performance Analysis

### Time Complexity

| Operation | Best Case | Average Case | Worst Case |
|-----------|-----------|--------------|------------|
| **Read** | O(1) | O(V) | O(V) |
| **Insert** | O(1) | O(1) | O(1) |
| **Update** | O(V) | O(V) | O(V) |
| **Delete** | O(V) | O(V) | O(V) |
| **Deadlock Check** | O(1) | O(V + E) | O(V + E) |

Where:
- V = number of versions in chain
- E = number of wait edges in graph

### Space Complexity

- **MVCC Heap:** O(V × K) where V = versions, K = keys
- **Lock Table:** O(L) where L = total locks
- **Waits-For Graph:** O(T²) where T = transactions (worst case: all pairs)

### Optimization Opportunities

1. **Version Chain Pruning:** Periodically clean old versions invisible to all active transactions
2. **Lock Escalation:** Upgrade row-level locks to table-level when too many rows locked
3. **Deadlock Prevention:** Use wait-die or wound-wait instead of detection (prevents graph overhead)

---

## Real-World Connections

### PostgreSQL MVCC

Our implementation mirrors PostgreSQL's approach:

```sql
-- PostgreSQL stores xmin/xmax in tuple header
CREATE TABLE accounts (id INT, balance INT);
INSERT INTO accounts VALUES (1, 1000);

-- Behind the scenes:
-- (xmin=100, xmax=0, id=1, balance=1000)

UPDATE accounts SET balance = 2000 WHERE id = 1;

-- New version created:
-- (xmin=101, xmax=0, id=1, balance=2000)
-- Old version marked:
-- (xmin=100, xmax=101, id=1, balance=1000)
```

**Key Differences:**
- PostgreSQL stores versions in-place (heap tuples)
- We use in-memory vectors (simplified)
- Both use same visibility logic

### MySQL InnoDB (2PL)

InnoDB uses Strict 2PL with MVCC:
```sql
-- Locks acquired during growing phase
BEGIN;
SELECT * FROM accounts WHERE id = 1 FOR UPDATE;  -- X lock
UPDATE accounts SET balance = 2000 WHERE id = 1;
COMMIT;  -- Locks released (shrinking phase)
```

### Oracle (MVCC + Undo Logs)

Oracle uses undo segments instead of version chains:
- Old versions stored in separate undo tablespace
- Same visibility rules apply
- Automatic cleanup via undo retention policy

---

## Common Pitfalls & Solutions

### 1. Lost Updates

**Problem:**
```
T1: READ balance = 1000
T2: READ balance = 1000
T1: UPDATE balance = 1000 + 500 = 1500
T2: UPDATE balance = 1000 + 300 = 1300  ❌ (Lost T1's update)
```

**Solution:** Use `SELECT FOR UPDATE` (exclusive lock on read)
```cpp
// Our implementation prevents this:
// T2's UPDATE waits for T1's EXCLUSIVE lock
```

### 2. Write Skew

**Problem:** Two transactions read overlapping data, make decisions, write disjoint data
```
T1: READ X, READ Y → UPDATE X
T2: READ X, READ Y → UPDATE Y
Both commit → inconsistent state
```

**Solution:** Serializable isolation level (not implemented in our lab)

### 3. Deadlock vs Livelock

**Deadlock:** Circular wait (we detect and abort)
**Livelock:** Transactions keep retrying, none make progress

Our implementation handles deadlock but not livelock (would need exponential backoff).

---

## Testing Checklist

- [x] **MVCC Snapshot Isolation**
  - [x] Transaction sees consistent snapshot
  - [x] Concurrent updates don't affect ongoing reads
  - [x] Committed changes visible to new transactions

- [x] **Two-Phase Locking**
  - [x] Shared locks allow concurrent reads
  - [x] Exclusive locks block all others
  - [x] Locks held until commit/abort (strict)
  - [x] Growing/shrinking phase enforcement

- [x] **Deadlock Detection**
  - [x] Cycle detection works correctly
  - [x] Younger transaction aborted
  - [x] Waits-for graph updates properly
  - [x] No false positives

- [x] **Integration**
  - [x] Read operations acquire shared locks
  - [x] Write operations acquire exclusive locks
  - [x] Abort rolls back MVCC versions
  - [x] Commit finalizes changes

- [x] **Edge Cases**
  - [x] Reading non-existent key returns empty
  - [x] Updating non-existent key handled gracefully
  - [x] Multiple transactions on same key
  - [x] Transaction reading own writes

---

## Key Takeaways

### 1. MVCC Benefits
- ✅ **Non-blocking reads:** Readers never wait for writers
- ✅ **Consistent snapshots:** Repeatable read isolation
- ✅ **Concurrency:** Multiple transactions progress simultaneously
- ❌ **Storage overhead:** Multiple versions consume space

### 2. 2PL Trade-offs
- ✅ **Serializability:** Strong consistency guarantees
- ✅ **Deadlock prevention:** Strict protocol avoids some anomalies
- ❌ **Throughput:** Exclusive locks reduce concurrency
- ❌ **Deadlock possible:** Needs detection mechanism

### 3. Combined Approach (MVCC + 2PL)
- MVCC handles reads (snapshot isolation)
- 2PL handles writes (serializability)
- Deadlock detection breaks cycles
- Best of both worlds: **high read concurrency + strong write isolation**

---

## Extensions & Future Work

1. **Write-Ahead Logging (WAL)**
   - Persist transactions to disk for crash recovery
   - Implement REDO/UNDO logs

2. **Serializable Snapshot Isolation (SSI)**
   - Detect read-write conflicts
   - Prevent write skew anomalies

3. **Lock Escalation**
   - Upgrade row locks to table locks automatically
   - Reduce lock table memory usage

4. **VACUUM Process**
   - Background thread to clean old MVCC versions
   - Reclaim storage space

5. **Savepoints**
   - Partial rollback within transaction
   - Nested transaction support

---

## References

### Academic Papers
1. **MVCC:** "Multiversion Concurrency Control—Theory and Algorithms" - Bernstein & Goodman (1983)
2. **2PL:** "Concurrency Control and Recovery in Database Systems" - Bernstein, Hadzilacos, Goodman (1987)
3. **Deadlock Detection:** "Deadlock Detection in Distributed Databases" - Knapp (1987)

### Database Documentation
1. PostgreSQL Internals: https://www.postgresql.org/docs/current/mvcc.html
2. MySQL InnoDB Locking: https://dev.mysql.com/doc/refman/8.0/en/innodb-locking.html
3. Oracle MVCC: https://docs.oracle.com/en/database/oracle/oracle-database/

### Books
1. **"Database Internals"** - Alex Petrov (O'Reilly, 2019)
   - Chapter 5: Transaction Processing
   - Chapter 6: Concurrency Control

2. **"Designing Data-Intensive Applications"** - Martin Kleppmann (O'Reilly, 2017)
   - Chapter 7: Transactions

3. **"Transaction Processing: Concepts and Techniques"** - Jim Gray, Andreas Reuter (1992)
   - The definitive reference on transaction systems

---

## Acknowledgments

This lab implements concepts from:
- PostgreSQL's MVCC implementation
- MySQL InnoDB's locking protocol
- Standard DFS cycle detection algorithms

Special thanks to the course instructors for designing this comprehensive lab that integrates multiple database internals concepts.

---

**Lab Completed:** June 23, 2026  
**Total Implementation Time:** ~8 hours  
**Lines of Code:** ~600 (transaction_manager.cpp)

---

## License

Educational use only - Scaler Advanced DBMS Course 2026
