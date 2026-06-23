# MiniDB Capstone Project Verification Logs

This document lists the completed verification logs and outputs for all 6 milestones implemented inside `MiniDB_Projects/Team_NoClarity/`.

---

## Milestone 1: Foundational Storage & Cache
- **Disk Manager Persistence:** Writes distinct pages, re-opens files, and asserts data integrity.
- **Slotted Page Layout:** Inserts three tuples, deletes the middle tuple (confirming it is tombstoned), and compacts the page. Validates that slot indexes remain stable.
- **Buffer Pool Manager Eviction:** Evicts frames using the Clock Replacer when the pool is full, writing dirty pages back to disk before reuse.

### Console Output Log
```text
--- Starting Disk Manager Tests ---
[DISK MANAGER SUCCESS] Direct block read, write, and allocation verified.

--- Starting Slotted Page Tests ---
Successfully inserted 3 tuples. Deleting middle tuple (TupleB_Longer)...
Triggering compaction...
[SLOTTED PAGE SUCCESS] Slotted layout insert, delete, and stable-index compaction verified.

--- Starting Buffer Pool Manager Tests ---
Allocating page 4 (Should trigger clock eviction of page 0)...
Fetching page 0 (Should load from disk)...
[BUFFER POOL MANAGER SUCCESS] Eviction, cache hits/misses, and dirty writeback verified.
```

---

## Milestone 2: B+ Tree Indexing & Parser Connection
- **B+ Tree Structural Tests:** Checks that sequential inserts trigger page splits and routing key promotions up to parent pages. Check node borrowing and page merging on removal.
- **Query Optimization & Execution:** Selects `TableScanNode` for unindexed queries, and `IndexScanNode` for indexed search columns.

### Console Output Log
```text
--- Starting B+ Tree Indexing Tests ---
Inserting keys step-by-step...
Testing deletion borrows and merges...
[B+ TREE SUCCESS] Key insertion, splits, lookups, and deletions verified.

--- Starting Query Engine & Optimizer Tests ---
Executing Query: SELECT * FROM students WHERE id = 2 (NO INDEX)
[OPTIMIZER] No index found on column 'id'. Falling back to TableScan (Est. Cost: O(N)).
[SUCCESS] TableScan executed correctly. Result: Bob

Creating B+ Tree Index on 'id'...

Executing Query: SELECT * FROM students WHERE id = 2 (WITH INDEX)
[OPTIMIZER] Index found on column 'id'. Choosing IndexScan (Est. Cost: O(log N)).
[SUCCESS] IndexScan executed correctly. Result: Bob
[QUERY ENGINE SUCCESS] Optimizer plan selection and index-scans verified.
```

---

## Milestone 3: Query Execution Engine
- **SeqScan & Filter:** Streams records sequentially and evaluates predicates.
- **Nested Loop Join:** flattened loop structure yielding rows sequentially without blocking inner loops.
- **Hash Join:** Builds an in-memory hash table on the inner relation and probes it with the outer relation.
- **Aggregation:** Group-by map computing counting, averaging, and min/max.

### Console Output Log
```text
--- Starting Execution Engine (Milestone 3) Tests ---
Executing Test 1: SeqScan + Filter (student_dept_id = 10)
Match: Alice, Dept: 10
Match: Carol, Dept: 10
[SUCCESS] SeqScan + Filter verified.

Executing Test 2: Nested Loop Join (students JOIN departments)
Join Match: Student=Alice -> Dept=ComputerScience (ID: 10)
Join Match: Student=Bob -> Dept=Mathematics (ID: 20)
Join Match: Student=Carol -> Dept=ComputerScience (ID: 10)
[SUCCESS] Nested Loop Join verified.

Executing Test 3: In-Memory Hash Join (students JOIN departments)
Hash Join Match: Student=Alice -> Dept=ComputerScience
Hash Join Match: Student=Bob -> Dept=Mathematics
Hash Join Match: Student=Carol -> Dept=ComputerScience
[SUCCESS] Hash Join verified.

Executing Test 4: Aggregation (Group by student_dept_id, count, avg(gpa), max(gpa))
Group Dept: 10 | Count: 2 | Avg GPA: 3.85 | Max GPA: 3.9
Group Dept: 20 | Count: 1 | Avg GPA: 3.2 | Max GPA: 3.2
Group Dept: 30 | Count: 1 | Avg GPA: 2.8 | Max GPA: 2.8
[SUCCESS] Aggregation (Group by + Multi-aggregate) verified.
[EXECUTION ENGINE SUCCESS] All Milestone 3 executors passed.
```

---

## Milestone 4: Cost-Based Join Order Optimizer
- **DP Join Optimizer:** Estimates costs for SeqScans, IndexScans, Nested Loop Joins, and Hash Joins, choosing the cheapest plan.
- **Left-Deep Invariant:** Emits only Left-Deep structures where right children are strictly leaf scans.

### Console Output Log
```text
--- Starting Cost-Based Join Optimizer (Milestone 4) Tests ---
Executing Test 1: Single Table Path Optimization
Table 0 Access Path: - SEQ_SCAN(Table 0) [Cost: 110, Card: 100]
Table 1 Access Path: - INDEX_SCAN(Table 1) [Cost: 78.5105, Card: 200]

Executing Test 2: 3-Way Join Optimization (Tables: 0, 1, 2)
- HASH_JOIN [Cost: 13028.5, Card: 100000]
  - HASH_JOIN [Cost: 328.511, Card: 1000]
    - INDEX_SCAN(Table 1) [Cost: 78.5105, Card: 200]
    - SEQ_SCAN(Table 0) [Cost: 110, Card: 100]
  - SEQ_SCAN(Table 2) [Cost: 2200, Card: 2000]
[SUCCESS] 3-Way Join Optimizer and Left-Deep structure verified.

Executing Test 3: 4-Way Join Optimization (Tables: 0, 1, 2, 3)
- HASH_JOIN [Cost: 513338, Card: 5000000]
  - HASH_JOIN [Cost: 5738.18, Card: 50000]
    - HASH_JOIN [Cost: 328.511, Card: 1000]
      - INDEX_SCAN(Table 1) [Cost: 78.5105, Card: 200]
      - SEQ_SCAN(Table 0) [Cost: 110, Card: 100]
    - INDEX_SCAN(Table 3) [Cost: 109.672, Card: 1000]
  - SEQ_SCAN(Table 2) [Cost: 2200, Card: 2000]
[SUCCESS] 4-Way Join Optimizer verified.
[OPTIMIZER SUCCESS] Cost-based join order dynamic programming passed.
```

---

## Milestone 5: ARIES Crash Recovery
- **ARIES Phases:** Rebuilds active Transactions (TT) and Dirty Pages (DPT) in Analysis, repeats history in Redo, and rolls back loser transactions in Undo writing Compensation Log Records (CLRs).
- **Idempotency:** Re-running recovery on half-recovered database files does not duplicate undo updates.

### Console Output Log
```text
--- Starting ARIES Crash Recovery (Milestone 5) Tests ---
[RECOVERY SUCCESS] Analysis, Redo, and Undo rolled back uncommitted transactions successfully.
[IDEMPOTENCY SUCCESS] Nested crash recovery verified. Recovered state matches perfectly.
[ARIES CRASH RECOVERY SUCCESS] All ARIES recovery and idempotency tests passed.
```

---

## Milestone 6: Log Replication Layer
- **Log Replication:** Streams log frames over raw TCP sockets.
- **Sync/Async Modes:** Sync mode waits for ACK and times out after 500ms if replica is dead; Async mode broadcasts non-blocking.
- **Role Promotion:** Toggles nodes to primary.

### Console Output Log
```text
--- Starting Log Replication (Milestone 6) Tests ---
[SYNCHRONOUS REPLICATION SUCCESS] Record replicated and ACK received successfully.
[ASYNCHRONOUS REPLICATION SUCCESS] Record broadcasted asynchronously.
[TIMEOUT SUCCESS] Replica offline detected and handled cleanly.
[PROMOTION SUCCESS] Replica successfully promoted to Primary.
[REPLICATION LAYER SUCCESS] All log replication layer tests passed successfully!
```
