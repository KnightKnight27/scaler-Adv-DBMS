#include "minidb/transaction/transaction_manager.h"

namespace minidb {

bool LockManager::LockShared(Transaction& txn, std::string resource) {
  EnsureGrowing(txn);
  if (Holds(txn, resource, LockMode::Exclusive) || Holds(txn, resource, LockMode::Shared)) {
    return true;
  }

  auto& state = locks_[resource];
  auto blockers = SharedBlockers(txn, state);
  if (!blockers.empty()) {
    WaitFor(txn.id, blockers);
    return false;
  }

  state.shared_holders.insert(txn.id);
  txn.shared_locks.insert(std::move(resource));
  ClearWaitsFor(txn.id);
  return true;
}

bool LockManager::LockExclusive(Transaction& txn, std::string resource) {
  EnsureGrowing(txn);
  if (Holds(txn, resource, LockMode::Exclusive)) {
    return true;
  }

  auto& state = locks_[resource];
  auto blockers = ExclusiveBlockers(txn, state);
  if (!blockers.empty()) {
    WaitFor(txn.id, blockers);
    return false;
  }

  state.shared_holders.erase(txn.id);
  txn.shared_locks.erase(resource);
  state.exclusive_holder = txn.id;
  txn.exclusive_locks.insert(std::move(resource));
  ClearWaitsFor(txn.id);
  return true;
}

void LockManager::ReleaseAll(Transaction& txn) {
  for (const auto& resource : txn.shared_locks) {
    auto it = locks_.find(resource);
    if (it != locks_.end()) {
      it->second.shared_holders.erase(txn.id);
    }
  }
  for (const auto& resource : txn.exclusive_locks) {
    auto it = locks_.find(resource);
    if (it != locks_.end() && it->second.exclusive_holder == txn.id) {
      it->second.exclusive_holder.reset();
    }
  }
  txn.shared_locks.clear();
  txn.exclusive_locks.clear();
  ClearWaitsFor(txn.id);
}

bool LockManager::Holds(Transaction& txn, std::string_view resource, LockMode mode) const {
  std::string key(resource);
  if (mode == LockMode::Shared) {
    return txn.shared_locks.find(key) != txn.shared_locks.end();
  }
  return txn.exclusive_locks.find(key) != txn.exclusive_locks.end();
}

bool LockManager::HasDeadlock() const {
  for (const auto& [txn_id, blockers] : waits_for_) {
    std::unordered_set<TxnId> visited;
    if (HasCycleFrom(txn_id, txn_id, visited)) {
      return true;
    }
  }
  return false;
}

void LockManager::EnsureGrowing(const Transaction& txn) const {
  if (txn.state != TransactionState::Growing) {
    throw MiniDbError("transaction is not active");
  }
}

std::unordered_set<TxnId> LockManager::SharedBlockers(const Transaction& txn, const LockState& state) const {
  std::unordered_set<TxnId> blockers;
  if (state.exclusive_holder.has_value() && *state.exclusive_holder != txn.id) {
    blockers.insert(*state.exclusive_holder);
  }
  return blockers;
}

std::unordered_set<TxnId> LockManager::ExclusiveBlockers(const Transaction& txn, const LockState& state) const {
  std::unordered_set<TxnId> blockers;
  if (state.exclusive_holder.has_value() && *state.exclusive_holder != txn.id) {
    blockers.insert(*state.exclusive_holder);
  }
  for (TxnId holder : state.shared_holders) {
    if (holder != txn.id) {
      blockers.insert(holder);
    }
  }
  return blockers;
}

void LockManager::WaitFor(TxnId waiter, const std::unordered_set<TxnId>& blockers) {
  waits_for_[waiter] = blockers;
  if (HasDeadlock()) {
    throw MiniDbError("deadlock detected for transaction " + std::to_string(waiter));
  }
}

void LockManager::ClearWaitsFor(TxnId txn_id) {
  waits_for_.erase(txn_id);
  for (auto& [waiter, blockers] : waits_for_) {
    blockers.erase(txn_id);
  }
}

bool LockManager::HasCycleFrom(TxnId start, TxnId current, std::unordered_set<TxnId>& visited) const {
  auto it = waits_for_.find(current);
  if (it == waits_for_.end()) {
    return false;
  }
  for (TxnId next : it->second) {
    if (next == start) {
      return true;
    }
    if (visited.insert(next).second && HasCycleFrom(start, next, visited)) {
      return true;
    }
  }
  return false;
}

Transaction& TransactionManager::Begin() {
  TxnId txn_id = next_txn_id_++;
  auto [it, inserted] = transactions_.emplace(txn_id, Transaction{txn_id});
  (void)inserted;
  return it->second;
}

bool TransactionManager::LockShared(TxnId txn_id, std::string resource) {
  try {
    return locks_.LockShared(Get(txn_id), std::move(resource));
  } catch (const MiniDbError& error) {
    if (std::string(error.what()).rfind("deadlock detected", 0) == 0) {
      Abort(txn_id);
    }
    throw;
  }
}

bool TransactionManager::LockExclusive(TxnId txn_id, std::string resource) {
  try {
    return locks_.LockExclusive(Get(txn_id), std::move(resource));
  } catch (const MiniDbError& error) {
    if (std::string(error.what()).rfind("deadlock detected", 0) == 0) {
      Abort(txn_id);
    }
    throw;
  }
}

void TransactionManager::Commit(TxnId txn_id) {
  Transaction& txn = Get(txn_id);
  locks_.ReleaseAll(txn);
  txn.state = TransactionState::Committed;
}

void TransactionManager::Abort(TxnId txn_id) {
  Transaction& txn = Get(txn_id);
  locks_.ReleaseAll(txn);
  txn.state = TransactionState::Aborted;
}

Transaction& TransactionManager::Get(TxnId txn_id) {
  auto it = transactions_.find(txn_id);
  if (it == transactions_.end()) {
    throw MiniDbError("unknown transaction: " + std::to_string(txn_id));
  }
  return it->second;
}

const Transaction& TransactionManager::Get(TxnId txn_id) const {
  auto it = transactions_.find(txn_id);
  if (it == transactions_.end()) {
    throw MiniDbError("unknown transaction: " + std::to_string(txn_id));
  }
  return it->second;
}

}  // namespace minidb
