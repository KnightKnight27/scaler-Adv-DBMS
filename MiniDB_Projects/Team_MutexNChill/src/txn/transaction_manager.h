#pragma once
#include <string>
#include <map>
#include <set>
#include <stdexcept>

// Thrown when a deadlock is detected.
struct DeadlockException : std::runtime_error {
    int victim_txn;
    DeadlockException(int txn)
        : std::runtime_error("Deadlock detected — aborting txn " + std::to_string(txn)),
          victim_txn(txn) {}
};

// Thrown when a lock is held by another transaction (and no deadlock).
struct LockConflictException : std::runtime_error {
    LockConflictException(int txn, int holder)
        : std::runtime_error("Txn " + std::to_string(txn) +
                             " blocked by txn " + std::to_string(holder)) {}
};

enum class LockMode { SHARED, EXCLUSIVE };

// Who holds the lock on one (table, row_id) key.
struct LockEntry {
    LockMode        mode;
    std::set<int>   holders; // transaction IDs currently holding the lock
};

// TransactionManager implements Strict Two-Phase Locking (2PL).
//
// Growing phase:  transactions acquire locks during execution.
// Shrinking phase: all locks are released at once on commit or abort.
//
// Deadlock detection: we maintain a waits-for graph.
// If acquiring a lock would create a cycle, we throw DeadlockException.
class TransactionManager {
public:
    // Start a new transaction. Returns its ID.
    int begin();

    // Release all locks and mark the transaction as committed.
    void commit(int txn_id);

    // Release all locks and mark the transaction as aborted.
    void abort(int txn_id);

    // Acquire a SHARED lock on (table, row_id).
    // Multiple transactions can hold shared locks simultaneously.
    // Throws if an EXCLUSIVE lock is held by another transaction.
    void acquireShared(int txn_id, const std::string& table, int row_id);

    // Acquire an EXCLUSIVE lock on (table, row_id).
    // Only one transaction can hold this at a time.
    // Throws if anyone else holds any lock on this row.
    void acquireExclusive(int txn_id, const std::string& table, int row_id);

    // Convenience: record that txn_id is waiting for blocker_id (for demo purposes).
    void registerWait(int txn_id, int blocker_id);

    bool isActive(int txn_id);

private:
    int next_txn_id = 1;

    // lock_table key: "table:row_id"
    std::map<std::string, LockEntry> lock_table;

    // Per-transaction: set of lock keys this transaction holds.
    std::map<int, std::set<std::string>> held_by;

    // Waits-for graph: txn_id -> the txn_id it is waiting for.
    std::map<int, int> waits_for;

    std::set<int> active_txns;

    std::string lockKey(const std::string& table, int row_id);

    // DFS cycle detection starting from 'start'.
    // Returns true if there is a cycle reachable from 'start'.
    bool hasCycle(int start);

    void releaseLocks(int txn_id);
};
