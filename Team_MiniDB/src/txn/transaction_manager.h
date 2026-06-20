#pragma once
#include <atomic>
#include <memory>

#include "lock_manager.h"
#include "transaction.h"

namespace minidb {

// Owns transaction id assignment and ties Commit/Abort to releasing every
// lock the transaction holds (Strict 2PL - locks are held for the whole
// transaction and only released at the very end, which is what gives
// Serializable isolation here). Transaction object lifetime is the
// caller's responsibility (typically a unique_ptr per client session).
class TransactionManager {
 public:
  explicit TransactionManager(LockManager *lock_mgr) : lock_mgr_(lock_mgr) {}

  std::unique_ptr<Transaction> Begin() { return std::make_unique<Transaction>(next_id_++); }

  void Commit(Transaction *txn) {
    txn->SetState(TxnState::SHRINKING);
    lock_mgr_->ReleaseAll(txn);
    txn->SetState(TxnState::COMMITTED);
  }

  void Abort(Transaction *txn) {
    txn->SetState(TxnState::SHRINKING);
    lock_mgr_->ReleaseAll(txn);
    txn->SetState(TxnState::ABORTED);
  }

 private:
  LockManager *lock_mgr_;
  std::atomic<txn_id_t> next_id_{1};
};

}  // namespace minidb
