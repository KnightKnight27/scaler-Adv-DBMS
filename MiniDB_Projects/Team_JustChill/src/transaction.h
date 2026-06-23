// transaction.h — Phase D: lightweight transactions (auto-commit + undo)
//
// A Transaction is an id plus an in-memory UNDO log: a list of closures that,
// replayed in reverse, reverse the statement's effects (insert -> tombstone,
// delete -> restore). There is no MVCC. Isolation comes from the LockManager
// (Strict 2PL); durability comes from the WAL. The TransactionManager hands out
// ids and tracks Active/Committed/Aborted state.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "lock_manager.h"  // TxnId

namespace minidb {

enum class TxnState { Active, Committed, Aborted };

class Transaction {
 public:
  explicit Transaction(TxnId id) : id_(id) {}

  TxnId id() const { return id_; }
  TxnState state() const { return state_; }

  // Operators push an undo action for every mutation they make.
  void addUndo(std::function<void()> fn) { undo_.push_back(std::move(fn)); }

  // Apply the undo log in reverse order (called on abort).
  void rollback() {
    for (auto it = undo_.rbegin(); it != undo_.rend(); ++it) (*it)();
    undo_.clear();
    state_ = TxnState::Aborted;
  }

  void markCommitted() {
    undo_.clear();  // nothing to undo once committed
    state_ = TxnState::Committed;
  }

 private:
  TxnId id_;
  TxnState state_ = TxnState::Active;
  std::vector<std::function<void()>> undo_;
};

class TransactionManager {
 public:
  Transaction* begin() {
    TxnId id = ++counter_;
    auto txn = std::make_unique<Transaction>(id);
    Transaction* raw = txn.get();
    txns_[id] = std::move(txn);
    return raw;
  }

  void commit(Transaction* t) {
    if (t) t->markCommitted();
  }
  void abort(Transaction* t) {
    if (t) t->rollback();
  }

  TxnState state(TxnId id) const {
    auto it = txns_.find(id);
    return it == txns_.end() ? TxnState::Aborted : it->second->state();
  }

 private:
  TxnId counter_ = 0;
  std::unordered_map<TxnId, std::unique_ptr<Transaction>> txns_;
};

}  // namespace minidb
