# System Design: Two-Phase Locking (2PL) vs Multi-Version Concurrency Control (MVCC)

**Name:** Shah Musharaf ul Islam
**College ID:** 24bcs10447
**OS:** Arch Linux | **Shell:** zsh

---

## 1. Concurrency Control Mechanisms

To guarantee transaction isolation (the 'I' in ACID), databases must control concurrent access to shared data. The two primary paradigms for this are pessimistic concurrency control (using locks, e.g., 2PL) and multi-version concurrency control (MVCC).

### Two-Phase Locking (2PL)
2PL is a pessimistic protocol that ensures serializability by forcing transactions to acquire locks before reading or writing data. It has two phases:
1. **Growing Phase:** A transaction may acquire locks but cannot release any.
2. **Shrinking Phase:** A transaction may release locks but cannot acquire new ones.

**The Locking Rules:**
- **Shared (S) Lock:** Required to read a record. Multiple transactions can hold S-locks on the same record concurrently.
- **Exclusive (X) Lock:** Required to write/update a record. Only one transaction can hold an X-lock, blocking all other reads and writes.

### Multi-Version Concurrency Control (MVCC)
MVCC is a concurrency control method where updates do not overwrite existing data in place. Instead, each update creates a new version of the record. 

When a transaction reads, it reads a consistent snapshot of the database from a point in time (usually the start of the transaction). This means readers never block writers, and writers never block readers.

---

## 2. Head-to-Head Comparison

| Metric | Two-Phase Locking (2PL) | Multi-Version Concurrency Control (MVCC) |
| :--- | :--- | :--- |
| **Reader-Writer Contention** | High. Readers block writers, and writers block readers. | None. Readers and writers do not block each other. |
| **Writer-Writer Contention** | Handled by exclusive locks (blocks the second writer). | Handled by write-conflict detection (usually aborts one). |
| **Deadlocks** | Common. Occur when transactions wait for locks in circular dependencies. | Rare. Only occur if mixed with explicit locking. |
| **Storage Overhead** | Low. Modifies data in-place; only locks are stored in memory. | High. Multiple versions of records are stored on disk/undo logs. |
| **Garbage Collection** | None needed. | Required (e.g., PostgreSQL `VACUUM` or InnoDB Undo Log purge). |
| **Typical Databases** | DB2, SQLite (file-level locking), SQL Server (by default). | PostgreSQL, MySQL (InnoDB), Oracle, CockroachDB. |

---

## 3. Concurrency Scenario Walkthrough

Let's look at how both systems handle a concurrent execution of two transactions on a record `X`:

```
Transaction 1 (T1)       Transaction 2 (T2)
------------------       ------------------
1. Begin                 1. Begin
2. Read X
                         2. Read X
3. Write X
                         3. Read X (again)
4. Commit
                         4. Commit
```

### Scenario Under 2PL:
1. **T1 Read X:** T1 acquires a Shared (S) lock on `X`.
2. **T2 Read X:** T2 also acquires a Shared (S) lock on `X`. (Shared locks are compatible).
3. **T1 Write X:** T1 attempts to upgrade its S-lock to an Exclusive (X) lock to write.
   - **Conflict!** T2 still holds an S-lock on `X`. T1 is blocked and must wait for T2 to release its lock.
   - If T2 now tries to write to `X`, it will also block waiting for T1's S-lock, causing a **deadlock**.
   - Otherwise, T1 remains blocked until T2 commits/aborts and releases its S-lock.

### Scenario Under MVCC:
1. **T1 Read X:** T1 reads version `X_0` (snapshot at T1 start). No locks acquired.
2. **T2 Read X:** T2 reads version `X_0` (snapshot at T2 start). No locks acquired.
3. **T1 Write X:** T1 creates a new version `X_1` marked with its transaction ID. T2 is completely unaffected.
4. **T2 Read X (again):** Depending on the isolation level:
   - **Read Committed:** T2 sees `X_0` (since T1 is not yet committed).
   - **Repeatable Read:** T2 sees `X_0` (snapshot isolation keeps its view frozen at the start of T2).
   - *In neither case is T2 blocked!*
5. **T1 Commit:** `X_1` becomes the active version for new transactions.

---

## 4. Key Learnings & Takeaways

- **"Readers do not block writers, and writers do not block readers"** is the fundamental value proposition of MVCC. It makes MVCC highly superior for read-heavy, mixed workloads.
- **MVCC trades storage space for concurrency:** By keeping old versions of records, MVCC avoids lock overhead but introduces database bloat. This requires a background cleaning process (`VACUUM` in Postgres, Undo Purge in MySQL) which can consume I/O.
- **2PL is simpler but prone to deadlocks:** In systems with highly concurrent writes to the same resources, 2PL spends significant time managing lock queues and detecting deadlocks, degrading throughput.
