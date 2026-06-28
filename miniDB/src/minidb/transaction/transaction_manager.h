#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "minidb/common/types.h"

namespace minidb {

enum class LockMode { Shared, Exclusive };
enum class TransactionState { Growing, Committed, Aborted };

struct Transaction {
  TxnId id{0};
  TransactionState state{TransactionState::Growing};
  std::unordered_set<std::string> shared_locks;
  std::unordered_set<std::string> exclusive_locks;
};

class LockManager {
 public:
  bool LockShared(Transaction& txn, std::string resource);
  bool LockExclusive(Transaction& txn, std::string resource);
  void ReleaseAll(Transaction& txn);
  bool Holds(Transaction& txn, std::string_view resource, LockMode mode) const;
  bool HasDeadlock() const;

 private:
  struct LockState {
    std::unordered_set<TxnId> shared_holders;
    std::optional<TxnId> exclusive_holder;
  };

  void EnsureGrowing(const Transaction& txn) const;
  std::unordered_set<TxnId> SharedBlockers(const Transaction& txn, const LockState& state) const;
  std::unordered_set<TxnId> ExclusiveBlockers(const Transaction& txn, const LockState& state) const;
  void WaitFor(TxnId waiter, const std::unordered_set<TxnId>& blockers);
  void ClearWaitsFor(TxnId txn_id);
  bool HasCycleFrom(TxnId start, TxnId current, std::unordered_set<TxnId>& visited) const;

  std::unordered_map<std::string, LockState> locks_;
  std::unordered_map<TxnId, std::unordered_set<TxnId>> waits_for_;
};

class TransactionManager {
 public:
  Transaction& Begin();
  bool LockShared(TxnId txn_id, std::string resource);
  bool LockExclusive(TxnId txn_id, std::string resource);
  void Commit(TxnId txn_id);
  void Abort(TxnId txn_id);
  Transaction& Get(TxnId txn_id);
  const Transaction& Get(TxnId txn_id) const;
  LockManager& lock_manager() { return locks_; }

 private:
  TxnId next_txn_id_{1};
  std::unordered_map<TxnId, Transaction> transactions_;
  LockManager locks_;
};

}  // namespace minidb
