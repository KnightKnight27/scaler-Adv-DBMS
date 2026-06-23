#pragma once

#include "transaction/lock_manager.h"
#include "common/types.h"
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <string>

// ─── TxStatus ─────────────────────────────────────────────────────────────────

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

// ─── TxManager ────────────────────────────────────────────────────────────────
//
// Public API for transaction management.
// Wraps the LockManager and tracks transaction state.
//
// Usage:
//   TxID xid = tm.begin();
//   tm.lockRead(xid, "students:5");   // acquire shared lock
//   tm.lockWrite(xid, "students:5");  // acquire exclusive lock
//   tm.commit(xid);                   // release all locks
//   tm.abort(xid);                    // release all locks
//
// The resource key convention used for locking is "tableName:key".
// This gives per-row granularity locking.

class TxManager {
public:
    explicit TxManager(LockManager& lm);

    // Start a new transaction. Returns a unique TxID.
    TxID begin();

    // Acquire a shared (read) lock on a resource.
    // Throws DeadlockException on deadlock detection.
    void lockRead(TxID xid, const std::string& resource_key);

    // Acquire an exclusive (write) lock on a resource.
    // Throws DeadlockException on deadlock detection.
    void lockWrite(TxID xid, const std::string& resource_key);

    // Commit a transaction: release all locks, mark as committed.
    void commit(TxID xid);

    // Abort a transaction: release all locks, mark as aborted.
    void abort(TxID xid);

    // Check if a transaction is active.
    bool isActive(TxID xid) const;

    // Get status of a transaction.
    TxStatus status(TxID xid) const;

    // Helper: resource key for a table row lock.
    static std::string rowKey(const std::string& table, int32_t key) {
        return table + ":" + std::to_string(key);
    }

    // Helper: resource key for a table-level lock (used for DDL).
    static std::string tableKey(const std::string& table) {
        return "TABLE:" + table;
    }

private:
    LockManager&                          lm_;
    std::atomic<TxID>                     next_xid_{1};
    mutable std::mutex                    tx_mutex_;
    std::unordered_map<TxID, TxStatus>    tx_table_;
};
