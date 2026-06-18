# Transaction Manager

## 1. Overview

This program implements a compact in-memory transaction manager for a key-value store where each key is a warehouse inventory item and each value is an integer stock count. The focus is on demonstrating core DBMS transaction control ideas in a readable C++17 program.

## 2. Data structures used

- `Version`: stores a committed value, creation timestamp, expiry timestamp, writer transaction id, and a small deleted flag placeholder.
- `Transaction`: stores transaction id, snapshot timestamp, current state, local write set, and held write locks.
- `LockInfo`: stores the current exclusive write-lock owner for a key.
- `TransactionManager`: manages version chains, lock table, waits-for graph, transaction records, commit timestamps, and vacuum cleanup.

## 3. Transaction lifecycle

When a transaction begins, it receives a unique transaction id and a snapshot timestamp based on the current commit counter. Reads use that snapshot. Writes first acquire an exclusive lock, then store the new value in the transaction's local write set. On commit, the pending writes become new committed versions. On abort, the local writes are discarded and locks are released.

## 4. MVCC visibility rule

Each key keeps a chain of committed versions. A transaction reads the newest version visible to its snapshot:

- `created_timestamp <= snapshot`
- and `expired_timestamp == 0` or `expired_timestamp > snapshot`

If the same transaction has already written the key, it sees its own latest uncommitted value from the local write set.

## 5. Strict 2PL write locking

Before writing a key, a transaction must hold the exclusive lock for that key. The lock is kept until commit or abort. This follows strict two-phase locking for writes, so another transaction cannot overwrite the same key while the first transaction is still active.

## 6. Waits-for graph and deadlock detection

If one transaction tries to write a key that is already locked by another active transaction, an edge is added in the waits-for graph. A DFS-based cycle check is then used to detect deadlocks. If a cycle is found, the youngest transaction id in the cycle is chosen as the victim, aborted, and removed from the graph.

## 7. Abort and rollback behavior

Abort clears the transaction's pending writes, releases all held locks, marks the transaction as aborted, and removes graph edges related to it. Since only committed versions are stored in the version chains, rollback simply discards the local uncommitted values.

## 8. Vacuum / garbage collection

Vacuum looks at the minimum snapshot among active transactions. Old expired versions that are no longer needed by any active snapshot are removed. The latest live version is always preserved.

## 9. Build and run commands

```bash
cd Lab8/24bcs10108_Lekhana_Dinesh/transaction-manager
make
make run
make clean
```

Direct compile command:

```bash
g++ -std=c++17 -Wall -Wextra -pthread main.cpp -o tx_manager
./tx_manager
```

Windows PowerShell note:

```powershell
g++ -std=c++17 -Wall -Wextra -pthread main.cpp -o tx_manager
.\tx_manager.exe
```

## 10. Expected demo scenarios

The `main()` function demonstrates:

1. A basic commit where a later transaction sees the committed stock value
2. Snapshot isolation where an older transaction keeps seeing the old version
3. A strict 2PL conflict where a second writer must retry after the first commit
4. Rollback where an aborted write never becomes visible
5. A deadlock where the youngest transaction is chosen as the victim
6. Vacuum cleanup where old expired versions are removed after old snapshots finish

## 11. Limitations

- The system is in-memory only and does not persist data.
- Only integer values are supported.
- Only exclusive write locks are modeled.
- The demo uses deterministic single-threaded calls instead of real concurrent worker threads.
- Deletes are not exercised even though the version struct keeps a placeholder deleted flag.

## 12. Learning outcome

This lab helped connect theory with implementation by showing how snapshot reads, lock-based writes, deadlock detection, rollback, and version cleanup interact in a small transaction manager.


