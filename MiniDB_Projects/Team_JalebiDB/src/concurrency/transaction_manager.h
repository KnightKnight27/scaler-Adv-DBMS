#pragma once

#include "common/config.h"
#include "common/types.h"
#include "concurrency/transaction.h"
#include "concurrency/lock_manager.h"
#include "recovery/log_manager.h"
#include "storage/buffer_pool_manager.h"
#include <atomic>
#include <unordered_map>
#include <mutex>

namespace minidb {

class TransactionManager {
public:
    TransactionManager(LockManager *lock_mgr, LogManager *log_mgr, BufferPoolManager *bpm);
    ~TransactionManager();

    // Begin a new transaction
    Transaction *Begin();

    // Commit a transaction
    void Commit(Transaction *txn);

    // Abort a transaction and roll back its changes
    void Abort(Transaction *txn);

private:
    void RollbackTransaction(Transaction *txn);

    LockManager *lock_mgr_;
    LogManager *log_mgr_;
    BufferPoolManager *bpm_;
    std::atomic<txn_id_t> next_txn_id_{1};
    std::unordered_map<txn_id_t, Transaction *> txn_map_;
    std::mutex latch_;
};

} // namespace minidb
