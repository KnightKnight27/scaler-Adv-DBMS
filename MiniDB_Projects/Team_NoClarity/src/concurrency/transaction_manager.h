#ifndef TRANSACTION_MANAGER_H
#define TRANSACTION_MANAGER_H

#include "concurrency/transaction.h"
#include "concurrency/lock_manager.h"
#include <mutex>
#include <unordered_map>
#include <memory>
#include <atomic>

namespace minidb {

class TransactionManager {
public:
    explicit TransactionManager(LockManager* lock_mgr) : lock_mgr_(lock_mgr), next_txn_id_(0) {}
    ~TransactionManager() = default;

    inline Transaction* Begin() {
        txn_id_t tid = next_txn_id_++;
        auto txn = std::make_unique<Transaction>(tid);
        Transaction* ptr = txn.get();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_txns_[tid] = std::move(txn);
        }
        return ptr;
    }

    inline void Commit(Transaction* txn) {
        if (txn == nullptr) return;
        txn->SetState(TransactionState::COMMITTED);
        lock_mgr_->ReleaseLocks(txn);
    }

    inline void Abort(Transaction* txn) {
        if (txn == nullptr) return;
        txn->SetState(TransactionState::ABORTED);
        lock_mgr_->ReleaseLocks(txn);
    }

private:
    LockManager* lock_mgr_;
    std::atomic<txn_id_t> next_txn_id_;
    std::mutex mutex_;
    std::unordered_map<txn_id_t, std::unique_ptr<Transaction>> active_txns_;
};

} // namespace minidb

#endif // TRANSACTION_MANAGER_H
