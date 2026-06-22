#pragma once
#include <atomic>
#include <string>
#include "common/types.h"
#include "txn/lock_manager.h"

namespace minidb {

enum class TxnState { GROWING, SHRINKING, COMMITTED, ABORTED };

// Issues transaction ids and drives the 2PL lifecycle through the lock manager.
// Lower ids are "older"; the deadlock victim is always the youngest (largest
// id) so progress is guaranteed (older transactions never starve).
class TransactionManager {
 public:
  txn_id_t Begin() { return next_txn_.fetch_add(1); }

  // Acquire a row lock for `txn` (strict 2PL: held until commit/abort).
  void LockShared(txn_id_t txn, const std::string& key) {
    lock_mgr_.Acquire(txn, key, LockMode::SHARED);
  }
  void LockExclusive(txn_id_t txn, const std::string& key) {
    lock_mgr_.Acquire(txn, key, LockMode::EXCLUSIVE);
  }

  void Commit(txn_id_t txn) { lock_mgr_.Release(txn); }
  void Abort(txn_id_t txn) { lock_mgr_.Release(txn); }

  LockManager& Locks() { return lock_mgr_; }

 private:
  std::atomic<txn_id_t> next_txn_{1};
  LockManager lock_mgr_;
};

}  // namespace minidb
