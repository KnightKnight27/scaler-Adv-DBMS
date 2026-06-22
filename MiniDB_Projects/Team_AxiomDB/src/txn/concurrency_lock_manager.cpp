#include "txn/concurrency_lock_manager.h"

#include <vector>

namespace axiomdb {

namespace {
bool compatible(LockMode a, LockMode b) {
  return a == LockMode::Shared && b == LockMode::Shared;  // only S/S is compatible
}
}  // namespace

bool ConcurrencyLockManager::lock_shared(Transaction* txn, uint64_t resource) {
  return acquire(txn, resource, LockMode::Shared);
}

bool ConcurrencyLockManager::lock_exclusive(Transaction* txn, uint64_t resource) {
  return acquire(txn, resource, LockMode::Exclusive);
}

// A request can be granted iff every earlier request from a DIFFERENT
// transaction is compatible with it (FIFO order prevents writer starvation; a
// transaction's own earlier request is ignored so S->X upgrades aren't blocked
// by their own shared lock).
bool ConcurrencyLockManager::can_grant(const ResourceQueue& rq, txn_id_t txn, LockMode mode) const {
  for (const Request& e : rq.queue) {
    if (e.txn == txn && e.mode == mode) return true;  // reached our own request
    if (e.txn == txn) continue;
    if (!compatible(e.mode, mode)) return false;
  }
  return true;
}

bool ConcurrencyLockManager::has_cycle(txn_id_t start) const {
  // DFS from start's successors; if we can get back to start, there's a cycle.
  std::vector<txn_id_t> stack;
  std::unordered_set<txn_id_t> visited;
  auto it = waits_for_.find(start);
  if (it == waits_for_.end()) return false;
  for (txn_id_t n : it->second) stack.push_back(n);
  while (!stack.empty()) {
    txn_id_t u = stack.back();
    stack.pop_back();
    if (u == start) return true;
    if (!visited.insert(u).second) continue;
    auto f = waits_for_.find(u);
    if (f != waits_for_.end())
      for (txn_id_t n : f->second) stack.push_back(n);
  }
  return false;
}

bool ConcurrencyLockManager::acquire(Transaction* txn, uint64_t resource, LockMode mode) {
  std::unique_lock<std::mutex> lk(latch_);
  ResourceQueue& rq = table_[resource];

  // Re-entrant / upgrade fast paths.
  for (Request& r : rq.queue) {
    if (r.txn != txn->id() || !r.granted) continue;
    if (r.mode == LockMode::Exclusive) return true;  // already hold X
    if (mode == LockMode::Shared) return true;       // hold S, want S
    // Hold S, want X: upgrade in place if we are the only granted holder.
    bool other_granted = false;
    for (const Request& o : rq.queue)
      if (o.granted && o.txn != txn->id()) { other_granted = true; break; }
    if (!other_granted) {
      r.mode = LockMode::Exclusive;
      return true;
    }
    break;  // contended upgrade: fall through to enqueue an X and wait
  }

  rq.queue.push_back({txn->id(), mode, false});

  for (;;) {
    if (can_grant(rq, txn->id(), mode)) {
      for (Request& r : rq.queue)
        if (r.txn == txn->id() && r.mode == mode) { r.granted = true; break; }
      waits_for_.erase(txn->id());
      txn->locks().insert(resource);
      cv_.notify_all();  // our grant might let queued-behind compatible reqs in
      return true;
    }

    // Record what we are waiting for, then look for a deadlock cycle.
    std::unordered_set<txn_id_t> ws;
    for (const Request& e : rq.queue) {
      if (e.txn == txn->id() && e.mode == mode) break;  // our position
      if (e.txn == txn->id()) continue;
      if (!compatible(e.mode, mode)) ws.insert(e.txn);
    }
    waits_for_[txn->id()] = std::move(ws);

    if (has_cycle(txn->id())) {
      // Victim: remove our pending request and let the caller roll us back.
      for (auto it = rq.queue.begin(); it != rq.queue.end(); ++it) {
        if (it->txn == txn->id() && !it->granted && it->mode == mode) {
          rq.queue.erase(it);
          break;
        }
      }
      waits_for_.erase(txn->id());
      cv_.notify_all();
      return false;
    }

    cv_.wait(lk);
  }
}

void ConcurrencyLockManager::unlock_all(Transaction* txn) {
  std::unique_lock<std::mutex> lk(latch_);
  for (uint64_t res : txn->locks()) {
    auto qit = table_.find(res);
    if (qit == table_.end()) continue;
    qit->second.queue.remove_if([&](const Request& r) { return r.txn == txn->id(); });
    if (qit->second.queue.empty()) table_.erase(qit);
  }
  txn->locks().clear();

  // Drop this txn from the wait-for graph entirely (its node and any inbound
  // edges) so stale edges can't trigger a phantom cycle later.
  waits_for_.erase(txn->id());
  for (auto& [t, set] : waits_for_) set.erase(txn->id());

  cv_.notify_all();
}

}  // namespace axiomdb
