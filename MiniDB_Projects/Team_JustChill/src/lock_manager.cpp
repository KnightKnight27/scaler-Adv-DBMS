// lock_manager.cpp — Track 3 (Query & Concurrency)
#include "lock_manager.h"

#include <stdexcept>

namespace minidb {

bool LockManager::canGrant(const TableLock& tl, TxnId txn, LockMode mode) const {
  if (mode == LockMode::Shared) {
    // A reader can join if nobody holds an exclusive lock, or if this very
    // txn already holds the exclusive lock (X implies S).
    return tl.exclusive_holder == 0 || tl.exclusive_holder == txn;
  }
  // Exclusive: need it free of any other exclusive holder, and no shared
  // holders except possibly this txn itself (the Shared -> Exclusive upgrade).
  if (tl.exclusive_holder != 0 && tl.exclusive_holder != txn) return false;
  for (TxnId h : tl.shared_holders) {
    if (h != txn) return false;
  }
  return true;
}

void LockManager::acquire(TxnId txn, const std::string& table, LockMode mode) {
  std::unique_lock<std::mutex> lk(mutex_);

  // table_locks_[table] default-constructs an empty TableLock on first use.
  TableLock& tl = table_locks_[table];

  if (!cv_.wait_for(lk, kLockTimeout,
                    [&] { return canGrant(tl, txn, mode); })) {
    throw std::runtime_error(
        "LockManager: timed out acquiring " +
        std::string(mode == LockMode::Shared ? "S" : "X") +
        " lock on table '" + table + "' for txn " + std::to_string(txn) +
        " (possible deadlock)");
  }

  // Grant it.
  if (mode == LockMode::Exclusive) {
    tl.shared_holders.erase(txn);  // in case this is an upgrade
    tl.exclusive_holder = txn;
  } else {
    // Don't downgrade an existing exclusive hold to shared.
    if (tl.exclusive_holder != txn) tl.shared_holders.insert(txn);
  }
  txn_tables_[txn].insert(table);
}

void LockManager::release_all(TxnId txn) {
  std::unique_lock<std::mutex> lk(mutex_);

  auto it = txn_tables_.find(txn);
  if (it == txn_tables_.end()) return;

  for (const std::string& table : it->second) {
    auto lit = table_locks_.find(table);
    if (lit == table_locks_.end()) continue;
    TableLock& tl = lit->second;
    tl.shared_holders.erase(txn);
    if (tl.exclusive_holder == txn) tl.exclusive_holder = 0;
  }
  txn_tables_.erase(it);

  // Wake everyone waiting; predicates re-check who can now proceed.
  cv_.notify_all();
}

}  // namespace minidb
