#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include "txn/lock_manager.h"
#include "txn/transaction.h"

namespace minidb {

// Hands out transaction ids and drives begin/commit/abort. Commit releases all
// locks (strict 2PL); abort first replays the undo log (in reverse) to roll the
// data back, then releases locks.
class TransactionManager {
 public:
  explicit TransactionManager(LockManager *lm) : lm_(lm) {}

  Transaction *Begin() {
    std::lock_guard<std::mutex> g(latch_);
    txn_id_t id = next_id_++;
    auto txn = std::make_unique<Transaction>(id);
    Transaction *raw = txn.get();
    txns_[id] = std::move(txn);
    return raw;
  }

  void Commit(Transaction *txn) {
    txn->undo().clear();  // changes are durable; nothing to undo
    lm_->UnlockAll(txn);
    txn->set_state(TxnState::COMMITTED);
  }

  void Abort(Transaction *txn) {
    // Apply inverse operations in reverse order, then release locks.
    auto &u = txn->undo();
    for (auto it = u.rbegin(); it != u.rend(); ++it) (*it)();
    u.clear();
    txn->set_state(TxnState::ABORTED);
    lm_->UnlockAll(txn);
  }

 private:
  LockManager                                         *lm_;
  std::atomic<txn_id_t>                                next_id_{1};
  std::mutex                                           latch_;
  std::map<txn_id_t, std::unique_ptr<Transaction>>     txns_;
};

}  // namespace minidb
