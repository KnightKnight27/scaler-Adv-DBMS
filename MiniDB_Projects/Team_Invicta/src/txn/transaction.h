#pragma once
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "common/config.h"

namespace minidb {

// Lock granularity is a single row, identified by (table, primary key).
struct RowId {
  std::string table;
  int64_t     key;
  bool operator<(const RowId &o) const {
    return table < o.table || (table == o.table && key < o.key);
  }
  bool operator==(const RowId &o) const { return table == o.table && key == o.key; }
};

enum class LockMode { SHARED, EXCLUSIVE };

// Two exclusive, or one exclusive and one shared, lock conflict; two shared
// locks are compatible.
inline bool LockConflict(LockMode a, LockMode b) {
  return a == LockMode::EXCLUSIVE || b == LockMode::EXCLUSIVE;
}

enum class TxnState { GROWING, SHRINKING, COMMITTED, ABORTED };

// A transaction tracks its lock set (for release at end) and an undo log of
// inverse operations (for ROLLBACK / victim abort). Strict 2PL: locks are held
// until COMMIT/ABORT, never released early.
class Transaction {
 public:
  explicit Transaction(txn_id_t id) : id_(id) {}

  txn_id_t id() const { return id_; }
  TxnState state() const { return state_; }
  void set_state(TxnState s) { state_ = s; }

  std::set<RowId> &shared_locks() { return shared_; }
  std::set<RowId> &exclusive_locks() { return exclusive_; }

  bool HoldsShared(const RowId &r) const { return shared_.count(r) > 0; }
  bool HoldsExclusive(const RowId &r) const { return exclusive_.count(r) > 0; }

  // Inverse operations, applied in reverse on rollback.
  void PushUndo(std::function<void()> fn) { undo_.push_back(std::move(fn)); }
  std::vector<std::function<void()>> &undo() { return undo_; }

 private:
  txn_id_t                            id_;
  TxnState                            state_{TxnState::GROWING};
  std::set<RowId>                     shared_;
  std::set<RowId>                     exclusive_;
  std::vector<std::function<void()>>  undo_;
};

// Thrown when a transaction is aborted (e.g. chosen as a deadlock victim).
struct TransactionAbortException : std::runtime_error {
  explicit TransactionAbortException(txn_id_t id)
      : std::runtime_error("transaction " + std::to_string(id) + " aborted (deadlock victim)"),
        txn_id(id) {}
  txn_id_t txn_id;
};

}  // namespace minidb
