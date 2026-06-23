#pragma once

#include "concurrency/transaction.h"
#include "concurrency/lock_manager.h"
#include "catalog/catalog.h"
#include <atomic>
#include <memory>
#include <unordered_map>

namespace minidb {

class TransactionManager {
private:
    std::atomic<txn_id_t> next_txn_id_{1};
    LockManager* lock_manager_;
    std::unordered_map<txn_id_t, std::unique_ptr<Transaction>> active_transactions_;
    std::mutex latch_;

public:
    explicit TransactionManager(LockManager* lock_manager) : lock_manager_(lock_manager) {}

    Transaction* Begin() {
        txn_id_t txn_id = next_txn_id_++;
        auto txn = std::make_unique<Transaction>(txn_id);
        Transaction* txn_ptr = txn.get();
        
        std::lock_guard<std::mutex> lock(latch_);
        active_transactions_[txn_id] = std::move(txn);
        return txn_ptr;
    }

    void Commit(Transaction* txn) {
        txn->SetState(TransactionState::COMMITTED);

        auto shared_locks = txn->GetSharedLockSet();
        auto exclusive_locks = txn->GetExclusiveLockSet();

        for (const auto& lock_key : shared_locks) {
            lock_manager_->Unlock(txn, lock_key);
        }
        for (const auto& lock_key : exclusive_locks) {
            lock_manager_->Unlock(txn, lock_key);
        }

        std::lock_guard<std::mutex> lock(latch_);
        active_transactions_.erase(txn->GetTransactionId());
    }
};

} // namespace minidb