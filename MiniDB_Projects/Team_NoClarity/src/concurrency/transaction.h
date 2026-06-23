#ifndef TRANSACTION_H
#define TRANSACTION_H

#include "common/config.h"
#include "common/rid.h"
#include <unordered_set>
#include <set>
#include <mutex>

namespace minidb {

// Comparator to allow RID to be used in std::set
inline bool operator<(const RID& lhs, const RID& rhs) {
    if (lhs.GetPageId() != rhs.GetPageId()) {
        return lhs.GetPageId() < rhs.GetPageId();
    }
    return lhs.GetSlotNum() < rhs.GetSlotNum();
}

enum class TransactionState { GROWING, SHRINKING, COMMITTED, ABORTED };

/**
 * Transaction class tracking transaction state and held locks under SS2PL.
 */
class Transaction {
public:
    explicit Transaction(txn_id_t txn_id) : txn_id_(txn_id), state_(TransactionState::GROWING) {}

    inline txn_id_t GetTxnId() const { return txn_id_; }
    
    inline TransactionState GetState() const { return state_; }
    inline void SetState(TransactionState state) { state_ = state; }

    inline void AddSharedLock(const RID& rid) {
        std::lock_guard<std::mutex> guard(latch_);
        shared_locks_.insert(rid);
    }

    inline void AddExclusiveLock(const RID& rid) {
        std::lock_guard<std::mutex> guard(latch_);
        exclusive_locks_.insert(rid);
    }

    inline const std::set<RID>& GetSharedLocks() const { return shared_locks_; }
    inline const std::set<RID>& GetExclusiveLocks() const { return exclusive_locks_; }

    inline void ClearLocks() {
        std::lock_guard<std::mutex> guard(latch_);
        shared_locks_.clear();
        exclusive_locks_.clear();
    }

private:
    txn_id_t txn_id_;
    TransactionState state_;
    std::mutex latch_;
    std::set<RID> shared_locks_;
    std::set<RID> exclusive_locks_;
};

} // namespace minidb

#endif // TRANSACTION_H
