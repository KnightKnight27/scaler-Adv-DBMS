#include "concurrency/lock_manager.h"
#include "common/logger.h"
#include <stdexcept>

namespace minidb {

bool LockManager::LockShared(Transaction* txn, const std::string& lock_key) {
    if (txn->GetState() == TransactionState::SHRINKING) {
        txn->SetState(TransactionState::ABORTED);
        throw std::logic_error("Transaction " + std::to_string(txn->GetTransactionId()) + 
                               " violated 2PL: requested lock in Shrinking phase.");
    }

    std::unique_lock<std::mutex> lock(latch_);
    LockRequestQueue& queue = lock_table_[lock_key];

    // Wait until no transaction holds an exclusive write lock on this row
    queue.cv_.wait(lock, [&queue]() { return !queue.is_exclusive_active_; });

    queue.shared_count_++;
    txn->AddSharedLock(lock_key);
    
    return true;
}

bool LockManager::LockExclusive(Transaction* txn, const std::string& lock_key) {
    if (txn->GetState() == TransactionState::SHRINKING) {
        txn->SetState(TransactionState::ABORTED);
        throw std::logic_error("Transaction " + std::to_string(txn->GetTransactionId()) + 
                               " violated 2PL: requested lock in Shrinking phase.");
    }

    std::unique_lock<std::mutex> lock(latch_);
    LockRequestQueue& queue = lock_table_[lock_key];

    // Wait until NO other transaction holds a shared lock AND no one holds an exclusive lock
    queue.cv_.wait(lock, [&queue]() { 
        return queue.shared_count_ == 0 && !queue.is_exclusive_active_; 
    });

    queue.is_exclusive_active_ = true;
    txn->AddExclusiveLock(lock_key);

    return true;
}

bool LockManager::Unlock(Transaction* txn, const std::string& lock_key) {
    std::unique_lock<std::mutex> lock(latch_);
    auto it = lock_table_.find(lock_key);
    if (it == lock_table_.end()) {
        return false;
    }

    LockRequestQueue& queue = it->second;

    // Strict 2PL: The moment we release a lock, we enter the shrinking phase
    if (txn->GetState() == TransactionState::GROWING) {
        txn->SetState(TransactionState::SHRINKING);
    }

    if (txn->GetSharedLockSet().count(lock_key) > 0) {
        queue.shared_count_--;
        txn->RemoveSharedLock(lock_key);
    } else if (txn->GetExclusiveLockSet().count(lock_key) > 0) {
        queue.is_exclusive_active_ = false;
        txn->RemoveExclusiveLock(lock_key);
    }

    // Wake up any threads sleeping in LockShared or LockExclusive waiting for this row
    queue.cv_.notify_all();

    return true;
}

} // namespace minidb