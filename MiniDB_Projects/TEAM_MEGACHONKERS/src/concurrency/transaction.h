#pragma once

#include <unordered_set>
#include <string>
#include <mutex>
#include "common/config.h"

namespace minidb {

enum class TransactionState { 
    GROWING, 
    SHRINKING, 
    COMMITTED, 
    ABORTED 
};

class Transaction {
private:
    txn_id_t txn_id_;
    TransactionState state_;

    // Sets to track which row keys this transaction currently has locked.
    // We use the encoded InternalKey as the string identifier.
    std::unordered_set<std::string> shared_lock_set_;
    std::unordered_set<std::string> exclusive_lock_set_;

public:
    explicit Transaction(txn_id_t txn_id) 
        : txn_id_(txn_id), state_(TransactionState::GROWING) {}

    txn_id_t GetTransactionId() const { return txn_id_; }
    TransactionState GetState() const { return state_; }
    void SetState(TransactionState state) { state_ = state; }

    // Lock Tracking Helpers
    void AddSharedLock(const std::string& lock_key) { shared_lock_set_.insert(lock_key); }
    void AddExclusiveLock(const std::string& lock_key) { exclusive_lock_set_.insert(lock_key); }
    
    void RemoveSharedLock(const std::string& lock_key) { shared_lock_set_.erase(lock_key); }
    void RemoveExclusiveLock(const std::string& lock_key) { exclusive_lock_set_.erase(lock_key); }

    const std::unordered_set<std::string>& GetSharedLockSet() const { return shared_lock_set_; }
    const std::unordered_set<std::string>& GetExclusiveLockSet() const { return exclusive_lock_set_; }
};

} // namespace minidb