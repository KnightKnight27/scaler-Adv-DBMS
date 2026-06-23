#ifndef MINIDB_LOCK_MANAGER_H
#define MINIDB_LOCK_MANAGER_H

#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "DeadlockDetector.h"
#include "Transaction.h"

/**
 * LockManager implements Strict Two-Phase Locking (S2PL) with
 * deadlock detection.
 *
 * ═══════════════════════════════════════════════════════════════════════
 * LOCK COMPATIBILITY MATRIX:
 *
 *             Requested
 *             S       X
 *  Held  S    ✓       ✗
 *        X    ✗       ✗
 *
 * - Multiple transactions can hold Shared (S) locks simultaneously
 * - Exclusive (X) locks are mutually exclusive with everything
 *
 * LOCK GRANULARITY:
 * We lock at the RECORD level, identified by int64_t:
 *   rid = (tableId << 32) | recordId
 *
 * Record-level locking provides maximum concurrency but has higher
 * overhead compared to page-level or table-level locking. The choice
 * mirrors InnoDB's default behavior.
 *
 * LOCK UPGRADE:
 * If a transaction holds S and requests X on the same record,
 * we upgrade the lock (if no other S holders exist).
 *
 * DEADLOCK HANDLING:
 * Before blocking on a lock request, we check the Wait-For Graph.
 * If adding the wait edge would create a cycle, we immediately
 * abort the requesting transaction (no-wait variant for simplicity).
 * ═══════════════════════════════════════════════════════════════════════
 */

enum class LockMode { SHARED, EXCLUSIVE };

class LockManager {
public:
    LockManager() = default;

    /**
     * Acquire a lock on a record.
     *
     * Returns true if the lock was granted.
     * Returns false if a deadlock would occur (transaction should abort).
     */
    bool lock(Transaction* txn, int64_t rid, LockMode mode);

    /** Release a specific lock. */
    void unlock(Transaction* txn, int64_t rid);

    /** Release ALL locks held by a transaction (called on commit/abort). */
    void unlockAll(Transaction* txn);

    /** Encode (tableId, recordId) into a single int64_t for lock identification. */
    static int64_t encodeRid(int tableId, int recordId) {
        return (static_cast<int64_t>(tableId) << 32) | static_cast<int64_t>(recordId);
    }

private:
    /** Internal lock table entry for a single record. */
    struct LockEntry {
        LockMode mode = LockMode::SHARED;
        std::unordered_set<int> holders;  // txnIds currently holding this lock
    };

    std::mutex mutex_;
    std::unordered_map<int64_t, LockEntry> lockTable_;
    DeadlockDetector deadlockDetector_;
};

#endif // MINIDB_LOCK_MANAGER_H
