#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "concurrency/lock_manager.h"
#include "concurrency/transaction.h"

namespace minidb {

// Hands out transactions and ends them. Transaction ids increase monotonically,
// so an id doubles as an age for wait-die. Under STRICT 2PL both commit and abort
// release all of a transaction's locks; data-level undo for abort is applied by
// the engine from txn->Writes() before Abort() is called.
class TransactionManager {
 public:
  explicit TransactionManager(LockManager *lock_manager) : lock_manager_(lock_manager) {}

  Transaction *Begin() {
    std::lock_guard<std::mutex> g(latch_);
    txn_id_t id = next_id_.fetch_add(1);
    auto txn = std::make_unique<Transaction>(id);
    Transaction *ptr = txn.get();
    txns_[id] = std::move(txn);
    return ptr;
  }

  void Commit(Transaction *txn) {
    txn->SetState(TxnState::kCommitted);
    lock_manager_->UnlockAll(txn);
  }

  void Abort(Transaction *txn) {
    txn->SetState(TxnState::kAborted);
    lock_manager_->UnlockAll(txn);
  }

 private:
  std::atomic<txn_id_t> next_id_{1};
  LockManager *lock_manager_;
  std::mutex latch_;
  std::unordered_map<txn_id_t, std::unique_ptr<Transaction>> txns_;
};

}  // namespace minidb
