#pragma once

#include "common/types.h"
#include "transaction/transaction.h"
#include "transaction/lock_manager.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace minidb {

// Forward declaration
class WALManager;

/*
 * TransactionManager: orchestrates Begin/Commit/Abort.
 *
 * Policy: FORCE + NO-STEAL
 *   - FORCE: dirty pages are flushed at commit time.
 *   - NO-STEAL: dirty pages are NOT written before commit.
 *   - This means recovery only needs REDO (no UNDO required).
 */
class TransactionManager {
public:
    TransactionManager(LockManager* lock_mgr, WALManager* wal_mgr);

    // Begin a new transaction
    Transaction* Begin();

    // Commit a transaction (flush WAL, release locks)
    bool Commit(Transaction* txn);

    // Abort a transaction (release locks, discard changes)
    bool Abort(Transaction* txn);

    // Get a transaction by ID
    Transaction* GetTransaction(txn_id_t txn_id);

    LockManager* GetLockManager() const { return lock_mgr_; }
    WALManager* GetWALManager() const { return wal_mgr_; }

private:
    LockManager*                                               lock_mgr_;
    WALManager*                                                wal_mgr_;
    txn_id_t                                                   next_txn_id_ = 1;
    std::unordered_map<txn_id_t, std::unique_ptr<Transaction>> active_txns_;
    std::mutex                                                 mutex_;
};

} // namespace minidb
