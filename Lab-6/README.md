# Lab 6 — Transaction Manager with MVCC + Strict 2PL + Deadlock Detection

**Course:** Advanced DBMS  
**Author:** Vedanshu Nishad (24BCS10285)  
**Language:** C++17

---

## Objective

Implement a transaction manager that combines three critical database concepts:

1. **MVCC (Multi-Version Concurrency Control)** — Each write creates a new version; readers see consistent snapshots
2. **Strict 2PL (Two-Phase Locking)** — Transactions acquire locks in "growing phase", release all in "shrinking phase"
3. **Deadlock Detection** — Waits-for graph cycle detection to abort one transaction when deadlock forms

This mirrors PostgreSQL's concurrency architecture and how modern OLTP databases handle concurrent workloads.

---

## Core Concepts

### MVCC - Version Visibility Rules

Every row version has:
- `xmin` = ID of transaction that created it
- `xmax` = ID of transaction that deleted it (0 if not deleted)

A version is **visible** to transaction T if:
```
xmin <= T.snapshot_xid  AND  (xmax == 0 OR xmax > T.snapshot_xid)
```

**Example Timeline:**
```
TX1 writes row X = "value1" (xmin=1, xmax=0)
                ↓
TX2 reads row X (snapshot=2) → sees TX1's version ✓
                ↓
TX3 updates row X, marks old version xmax=3, creates new version (xmin=3, xmax=0)
                ↓
TX2 reads row X again → still sees old version! (because xmax=3 > 2)
                ↓
TX4 reads row X (snapshot=5) → sees TX3's version (xmin=3 <= 5)
```

**Why this works:** Readers see consistent snapshots without blocking writers!

### Strict 2PL - Two Phases

```
GROWING PHASE:
- Transaction may acquire NEW locks
- May NOT release any locks
- Example: TX acquires READ lock on A, then WRITE lock on B

SHRINKING PHASE:
- Transaction may release locks
- May NOT acquire new locks
- Example: TX releases both locks before/at commit

Strict 2PL: All locks held until COMMIT or ABORT
```

**Benefit:** Guarantees serializability and avoids dirty reads/lost updates

### Deadlock Detection - Waits-For Graph

```
Waits-For Graph: Edge from TX_A → TX_B means "A waits for B"

Example Deadlock:
TX1 holds WRITE(row_X), waits for READ(row_Y)
TX2 holds READ(row_Y),  waits for WRITE(row_X)

Cycle detected: TX1 → TX2 → TX1
Solution: Abort younger transaction (TX2)
```

---

## Files

| File | Purpose |
|------|---------|
| `transaction_manager.cpp` | Complete implementation: MVCC, 2PL, deadlock detection |
| `CMakeLists.txt` | Build configuration with pthread support |
| `README.md` | This document |

---

## Build & Run

```bash
mkdir -p build
cd build
cmake ..
make
./txn_manager
```

---

## Architecture

### 1. Version Chain (MVCC)

```cpp
struct VersionChain {
    TxID   xmin;       // creator
    TxID   xmax;       // deleter
    std::string value;
};

struct RowVersions {
    std::vector<VersionChain> versions;  // immutable chain
};
```

All writes append to version chain. Reads traverse chain to find visible version.

### 2. Lock Manager (Strict 2PL)

```cpp
class LockManager {
    std::unordered_map<RowKey, std::set<TxID>> read_locks;
    std::unordered_map<RowKey, TxID> write_locks;
    
    bool acquireReadLock(TxID tx_id, const RowKey& key);
    bool acquireWriteLock(TxID tx_id, const RowKey& key);
    void releaseAllLocks(TxID tx_id, const std::set<RowKey>& keys);
};
```

**Lock Compatibility Matrix:**
```
          READ  WRITE
READ       ✓     ✗
WRITE      ✗     ✗
```

- Multiple readers can hold READ lock on same row
- WRITE lock is exclusive
- Acquisition is blocking (simplified version)

### 3. Deadlock Detector

```cpp
class DeadlockDetector {
    std::unordered_map<TxID, std::unordered_set<TxID>> waits_for;
    
    bool detectCycle(TxID& victim_out);  // DFS to find cycles
};
```

Maintains waits-for graph. On lock wait, adds edge. Detects cycles with DFS.

### 4. Transaction Manager

```cpp
class TransactionManager {
    TxID beginTransaction();
    std::optional<std::string> read(TxID tx_id, const RowKey& key);
    bool write(TxID tx_id, const RowKey& key, const std::string& value);
    bool commit(TxID tx_id);
    bool abort(TxID tx_id);
};
```

**Transaction Lifecycle:**
```
BEGIN → [READ/WRITE operations] → COMMIT/ABORT

BEGIN:
- Allocate TxID
- Create snapshot (current xmin for visibility)

READ:
- Acquire READ lock (if not already held)
- Find visible version in version chain
- Add row to read_set

WRITE:
- Acquire WRITE lock (exclusive)
- Create new version (xmin = current TX)
- Mark old versions as deleted (xmax = current TX)
- Add row to write_set

COMMIT:
- Enter shrinking phase
- Release all locks
- Mark transaction as COMMITTED
- Versions created by this TX become visible to future snapshots

ABORT:
- Enter shrinking phase
- Rollback: remove versions created by this TX
- Release all locks
- Mark transaction as ABORTED
```

---

## Sample Execution

```
=== Transaction Manager with MVCC + 2PL + Deadlock Detection ===

--- Test 1: Basic MVCC (Multiple Versions) ---

[TX] BEGIN TX1 (snapshot=1)
[WRITE] TX1 writes account:1 = 'balance=1000'
[COMMIT] TX1 committed

[TX] BEGIN TX2 (snapshot=2)
[TX] BEGIN TX3 (snapshot=3)
[READ] TX2 reads account:1 = 'balance=1000'
[WRITE] TX3 writes account:1 = 'balance=900'
[COMMIT] TX3 committed
[READ] TX2 reads account:1 = 'balance=1000'  ← MVCC: still sees old version!
[COMMIT] TX2 committed

=== Data State ===
account:1: [xmin=1 xmax=3 val='balance=1000'] -> [xmin=3 xmax=0 val='balance=900']
```

**Key observation:** TX2 (snapshot=2) still sees version with xmax=3 because 3 > 2!

---

## Key Implementation Details

### 1. Lock Acquisition (Blocking)

```cpp
bool acquireWriteLock(TxID tx_id, const RowKey& key) {
    // Check if anyone else holds lock
    if ((other_reader) || (other_writer)) {
        wait_queue[key].push(tx_id);
        return false;  // Simplified: caller must retry
    }
    write_locks[key] = tx_id;
    return true;
}
```

In this simplified version, lock acquisition returns false if lock is held. In real DB:
- Add callback mechanism
- Use condition variables for wait notification
- Implement lock timeout detection

### 2. Version Visibility Check

```cpp
std::optional<std::string> read(TxID tx_id, const RowKey& key) {
    // Find visible version
    for (const auto& v : data[key].versions) {
        if (v.xmin <= tx.snapshot_xid &&
            (v.xmax == 0 || v.xmax > tx.snapshot_xid)) {
            return v.value;  // ✓ visible
        }
    }
    return std::nullopt;  // ✗ not visible
}
```

Traverses version chain from oldest to newest.

### 3. Version Creation on Write

```cpp
bool write(TxID tx_id, const RowKey& key, const std::string& value) {
    // Mark old versions as deleted
    for (auto& v : data[key].versions) {
        if (v.xmax == 0) {
            v.xmax = tx_id;  // Current TX deletes old version
        }
    }
    
    // Create new version
    VersionChain new_v{tx_id, 0, value};
    data[key].versions.push_back(new_v);
    return true;
}
```

Append-only approach: never modify existing versions, only add new ones.

---

## Trade-offs

### MVCC Advantages
✓ Readers don't block writers  
✓ Writers don't block readers  
✓ Snapshot isolation without complex locking  

### MVCC Disadvantages
✗ Version chain overhead (storage)  
✗ Garbage collection required (VACUUM in PostgreSQL)  
✗ Blind writes cause version explosion  

### 2PL Advantages
✓ Guarantees serializability  
✓ Simple to reason about  
✓ Prevents all anomalies  

### 2PL Disadvantages
✗ Writers block readers  
✗ Readers block writers  
✗ High contention on hot data  

### MVCC + 2PL Benefits
✓ Combines best of both: readers non-blocking + serializability  
✓ PostgreSQL, Oracle, SQL Server all use this approach

---

## Real-World Examples

### PostgreSQL MVCC

```sql
-- TX1: BEGIN (snapshot = 100)
-- TX2: BEGIN (snapshot = 105)

-- TX2: UPDATE account SET balance = 900 WHERE id = 1;
-- Creates version: (xmin=105, xmax=0)

-- TX1: SELECT * FROM account WHERE id = 1;
-- Sees old version: (xmin=100, xmax=0)
-- Because xmin <= 100 and xmax > 100

-- TX2: COMMIT;
-- Version (xmin=100, xmax=0) marked: xmax=105

-- TX1: SELECT * FROM account WHERE id = 1;
-- Still sees old version! (xmax=105 > 100)
```

### Deadlock Scenario

```sql
-- Connection A
START TRANSACTION;
UPDATE account SET balance = 900 WHERE id = 1;

-- Connection B
START TRANSACTION;
UPDATE account SET balance = 1100 WHERE id = 2;

-- Connection A
UPDATE account SET balance = ... WHERE id = 2;  ← BLOCKED on B's lock

-- Connection B
UPDATE account SET balance = ... WHERE id = 1;  ← BLOCKED on A's lock
-- DEADLOCK! PostgreSQL detects, aborts B
```

---

## Performance Considerations

### Version Chain Lookups
- **Linear search** through versions O(V) where V = # versions per row
- **Optimization:** Index by xmin/xmax for binary search
- **PostgreSQL:** Maintains visibility map cache

### Lock Contention
- **Hot rows:** Many transactions compete for same lock
- **Optimization:** Reduce transaction duration
- **Optimization:** Use lock-free data structures where possible

### Garbage Collection
- **VACUUM** must clean up dead versions
- **Overhead:** Background process scanning entire table
- **Trade-off:** More frequent VACUUM = more CPU, less storage

---

## Testing Scenarios

### Test 1: Snapshot Isolation
```cpp
TX1: write(A=1)  commit
TX2: snapshot_xid=2, read(A) → sees 1
TX3: snapshot_xid=3, write(A=2), commit
TX2: read(A) → still sees 1 (MVCC working)
```

### Test 2: Write Conflict
```cpp
TX1: write(A=1)  ← acquires WRITE lock
TX2: write(A=2)  ← blocks waiting for WRITE lock
```

### Test 3: Deadlock
```cpp
TX1: read(A), then write(B)
TX2: read(B), then write(A)
→ Cycle detected, one aborted
```

---

## Limitations of Implementation

1. **Simplified lock acquisition:** Real DB uses callbacks/condition variables
2. **No timeouts:** Deadlock detection only, no timeout-based resolution
3. **Single-threaded:** No actual concurrent execution (simplified demo)
4. **No WAL:** No Write-Ahead Logging (UNDO/REDO recovery not implemented)
5. **No MVCC cleanup:** Version chains grow unbounded (need VACUUM)

---

## Production DBMS Comparison

| Aspect | This Lab | PostgreSQL | MySQL/InnoDB | Oracle |
|--------|----------|-----------|--------------|--------|
| **MVCC Type** | Tuple-based | Tuple-based | Clustered index | Rollback segment |
| **Lock Type** | Simple | Predicate locks | Row locks | Optimistic |
| **GC** | Manual | VACUUM | Auto-purge | Auto-purge |
| **Isolation Level** | Snapshot | Read Committed, Serializable | Read Committed, etc | Read Committed, etc |

---

## Key Learnings

1. **MVCC enables concurrency** without locking everything
2. **2PL guarantees correctness** but reduces concurrency  
3. **Combining both** is the sweet spot for OLTP systems
4. **Version chains have storage cost** → need garbage collection
5. **Deadlock detection is complex** but necessary for correctness

---

## References

- PostgreSQL Source: `src/backend/access/heap/heapam.c` (MVCC)
- PostgreSQL Source: `src/backend/storage/lmgr/lock.c` (Lock Manager)
- "Transaction Processing" by Bernstein, Hadzilacos, Goodman
- CMU 15-445: Database Systems - Concurrency Control lectures
- "Database Internals" by Alex Petrov - Chapters on Transactions

---

## Follow-up Enhancements

1. **Actual Multithreading:** Use condition variables for real concurrent execution
2. **WAL (Write-Ahead Logging):** Implement UNDO/REDO for crash recovery
3. **MVCC Cleanup:** Implement VACUUM to remove dead versions
4. **Optimistic Locking:** Use timestamps instead of locks
5. **Predicate Locks:** Support range queries, not just row-level
6. **Conflict Detection:** Detect conflicts before commit
7. **Performance Tuning:** Benchmark different MVCC strategies

