#include "txn/lock_manager.h"

namespace minidb {

bool LockManager::IsCompatible(const ResourceQueue& rq, txn_id_t txn,
                               LockMode mode) const {
  for (const auto& e : rq.queue) {
    if (!e.granted) continue;
    if (e.txn == txn) continue;  // already held by us
    // Conflict unless both are SHARED.
    if (mode == LockMode::EXCLUSIVE || e.mode == LockMode::EXCLUSIVE) return false;
  }
  return true;
}

bool LockManager::FindCycle(txn_id_t u, std::unordered_set<txn_id_t>& visited,
                            std::unordered_set<txn_id_t>& on_stack,
                            txn_id_t* victim) {
  visited.insert(u);
  on_stack.insert(u);
  auto it = waits_for_.find(u);
  if (it != waits_for_.end()) {
    for (txn_id_t v : it->second) {
      if (on_stack.count(v)) { *victim = std::max(u, v); return true; }
      if (!visited.count(v) && FindCycle(v, visited, on_stack, victim)) {
        *victim = std::max(*victim, u);  // youngest (largest id) in the cycle
        return true;
      }
    }
  }
  on_stack.erase(u);
  return false;
}

bool LockManager::HasDeadlock(txn_id_t waiter) {
  std::unordered_set<txn_id_t> visited, on_stack;
  txn_id_t victim = waiter;
  if (FindCycle(waiter, visited, on_stack, &victim)) {
    aborted_.insert(victim);
    cv_.notify_all();  // wake the victim (and others) to re-check state
    return true;
  }
  return false;
}

void LockManager::Acquire(txn_id_t txn, const std::string& key, LockMode mode) {
  std::unique_lock<std::mutex> lk(latch_);
  ResourceQueue& rq = table_[key];

  // Already hold a compatible-or-stronger lock? Treat as granted (no upgrade
  // logic needed for our workload: writers take X up front).
  for (auto& e : rq.queue) {
    if (e.txn == txn && e.granted &&
        (e.mode == LockMode::EXCLUSIVE || mode == LockMode::SHARED)) {
      return;
    }
  }

  rq.queue.push_back({txn, mode, false});
  auto self = std::prev(rq.queue.end());

  while (!IsCompatible(rq, txn, mode)) {
    // Record waits-for edges: `txn` waits on every conflicting granted holder.
    waits_for_[txn].clear();
    for (auto& e : rq.queue) {
      if (e.granted && e.txn != txn &&
          (mode == LockMode::EXCLUSIVE || e.mode == LockMode::EXCLUSIVE)) {
        waits_for_[txn].insert(e.txn);
      }
    }
    if (HasDeadlock(txn) && aborted_.count(txn)) {
      rq.queue.erase(self);
      waits_for_.erase(txn);
      aborted_.erase(txn);
      throw TxnAborted("transaction " + std::to_string(txn) +
                       " aborted to break a deadlock");
    }
    cv_.wait(lk);
    // Woken: a victim may have been chosen, or a lock was released.
    if (aborted_.count(txn)) {
      rq.queue.erase(self);
      waits_for_.erase(txn);
      aborted_.erase(txn);
      throw TxnAborted("transaction " + std::to_string(txn) +
                       " aborted to break a deadlock");
    }
  }

  self->granted = true;
  waits_for_.erase(txn);
  held_[txn].insert(key);
}

void LockManager::Release(txn_id_t txn) {
  std::unique_lock<std::mutex> lk(latch_);
  auto it = held_.find(txn);
  if (it != held_.end()) {
    for (const auto& key : it->second) {
      ResourceQueue& rq = table_[key];
      for (auto e = rq.queue.begin(); e != rq.queue.end();) {
        if (e->txn == txn) e = rq.queue.erase(e);
        else ++e;
      }
    }
    held_.erase(it);
  }
  waits_for_.erase(txn);
  cv_.notify_all();  // let waiters re-test compatibility
}

}  // namespace minidb
