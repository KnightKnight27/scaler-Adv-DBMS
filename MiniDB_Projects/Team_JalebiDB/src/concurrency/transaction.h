#pragma once

#include "common/config.h"
#include "common/types.h"
#include <unordered_set>
#include <mutex>

namespace minidb {

enum class TransactionState {
    GROWING = 0,
    SHRINKING,
    COMMITTED,
    ABORTED
};

enum class LockMode {
    SHARED = 0,
    EXCLUSIVE
};

class Transaction {
public:
    explicit Transaction(txn_id_t txn_id) : txn_id_(txn_id), state_(TransactionState::GROWING) {}

    txn_id_t GetTxnId() const { return txn_id_; }

    TransactionState GetState() const { return state_; }
    void SetState(TransactionState state) { state_ = state; }

    lsn_t GetPrevLSN() const { return prev_lsn_; }
    void SetPrevLSN(lsn_t prev_lsn) { prev_lsn_ = prev_lsn; }

    void AddLock(const RID &rid) {
        lock_set_.insert(rid);
    }
    const std::unordered_set<RID> &GetLockSet() const { return lock_set_; }
    void ClearLocks() { lock_set_.clear(); }

private:
    txn_id_t txn_id_;
    TransactionState state_;
    lsn_t prev_lsn_{INVALID_LSN};
    std::unordered_set<RID> lock_set_; // locks held by this txn
};

} // namespace minidb
