// =============================================================================
// include/transaction/transaction_manager.h
// -----------------------------------------------------------------------------
// TransactionManager owns the active-transaction set, allocates ids,
// and implements the MVCC visibility test. It also owns a LockManager
// for the 2PL baseline path.
// =============================================================================
#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "common/status.h"
#include "common/types.h"
#include "transaction/lock_manager.h"
#include "transaction/transaction.h"

namespace minidb::transaction {

class TransactionManager {
public:
    TransactionManager();
    ~TransactionManager();

    TransactionManager(const TransactionManager&)            = delete;
    TransactionManager& operator=(const TransactionManager&) = delete;

    TransactionId begin();
    Status        commit(TransactionId txn);
    Status        abort (TransactionId txn);

    // MVCC visibility.
    bool          isVisible(TransactionId rowCreator,
                            TransactionId rowDeleter,
                            const Transaction& reader);

    // Look up the Transaction object for an id (nullptr if unknown / committed
    // and evicted). Used by executors to obtain the reader's snapshot.
    const Transaction* getTransaction(TransactionId id) const;

    // 2PL baseline.
    LockManager&  lockManager() { return lockMgr_; }

    // MVCC write-set bookkeeping.
    void          recordWrite(TransactionId txn, RecordId rid);
    bool          hasConflict(TransactionId txn);   // write-write conflict?

    // For recovery.
    std::unordered_set<TransactionId> activeTxns() const;

private:
    mutable std::mutex                                       mu_;
    TransactionId                                            next_ = 1;
    // MVCC snapshot high-water mark: the highest txn id that has COMMITTED.
    // A reader's snapshot = committedHighWater_ at its BEGIN time, so a row
    // created by an as-yet-uncommitted txn (even one that began before the
    // reader) is correctly invisible. This is real snapshot isolation; the
    // earlier "id - 1" rule leaked uncommitted writes.
    TransactionId                                            committedHighWater_ = 0;
    std::unordered_map<TransactionId, std::unique_ptr<Transaction>> txns_;
    // per-txn write set for MVCC conflict detection
    std::unordered_map<TransactionId, std::unordered_set<RecordId>> writes_;
    std::unordered_map<RecordId, TransactionId>                    committedWrites_;
    LockManager                                              lockMgr_;

    bool          hasConflictLocked(TransactionId txn) const;
};

} // namespace minidb::transaction
