#include "txn/lock_manager.h"

namespace minidb {

bool LockManager::can_grant(const Entry& entry, TxnId txn, LockMode mode) const {
  for (const Request& r : entry.queue) {
    if (r.granted && r.txn != txn && !compatible(r.mode, mode)) return false;
  }
  return true;
}

// Depth-first search over the waits-for graph: can `from` reach `target`?
bool LockManager::reaches(TxnId from, TxnId target, std::unordered_set<TxnId>& seen) const {
  auto it = waits_for_.find(from);
  if (it == waits_for_.end()) return false;
  for (TxnId next : it->second) {
    if (next == target) return true;
    if (seen.insert(next).second && reaches(next, target, seen)) return true;
  }
  return false;
}

void LockManager::acquire(TxnId txn, const RowKey& key, LockMode mode) {
  std::unique_lock<std::mutex> lk(mu_);
  Entry& entry = table_[key];

  // Build the set of granted holders that block this request.
  auto blockers = [&]() {
    std::unordered_set<TxnId> b;
    for (const Request& r : entry.queue)
      if (r.granted && r.txn != txn && !compatible(r.mode, mode)) b.insert(r.txn);
    return b;
  };

  // Already hold a lock on this key? Possibly an upgrade.
  for (Request& r : entry.queue) {
    if (r.txn == txn && r.granted) {
      if (mode == LockMode::Shared || r.mode == LockMode::Exclusive) return;
      r.mode = LockMode::Exclusive;  // S -> X upgrade
      r.granted = false;
      while (!can_grant(entry, txn, LockMode::Exclusive)) {
        waits_for_[txn] = blockers();
        std::unordered_set<TxnId> seen;
        if (reaches(txn, txn, seen)) { waits_for_.erase(txn); throw TxnAbortException(txn); }
        cv_.wait(lk);
      }
      r.granted = true;
      waits_for_.erase(txn);
      return;
    }
  }

  // Fresh request: append it and wait until it can be granted.
  entry.queue.push_back({txn, mode, false});
  auto my = std::prev(entry.queue.end());
  while (!can_grant(entry, txn, mode)) {
    waits_for_[txn] = blockers();
    std::unordered_set<TxnId> seen;
    if (reaches(txn, txn, seen)) {
      entry.queue.erase(my);
      waits_for_.erase(txn);
      throw TxnAbortException(txn);
    }
    cv_.wait(lk);
  }
  my->granted = true;
  held_[txn].insert(key);
  waits_for_.erase(txn);
}

void LockManager::release_all(TxnId txn) {
  std::unique_lock<std::mutex> lk(mu_);
  auto it = held_.find(txn);
  if (it != held_.end()) {
    for (const RowKey& key : it->second) {
      Entry& entry = table_[key];
      entry.queue.remove_if([&](const Request& r) { return r.txn == txn; });
    }
    held_.erase(it);
  }
  waits_for_.erase(txn);
  cv_.notify_all();  // wake every waiter to re-check whether it can now proceed
}

}  // namespace minidb
