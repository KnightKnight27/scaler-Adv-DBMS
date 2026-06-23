#include "txn/lock_manager.h"
#include <algorithm>
#include <vector>

namespace minidb {

bool LockManager::Grantable(LockQueue &q, txn_id_t self, LockMode mode) {
  // Compatible if no *other* transaction currently holds a conflicting lock.
  for (const LockRequest &r : q.reqs) {
    if (!r.granted || r.txn == self) continue;
    if (LockConflict(mode, r.mode)) return false;
  }
  return true;
}

void LockManager::NotifyAll() {
  for (auto &kv : table_) kv.second.cv.notify_all();
}

bool LockManager::DetectDeadlock(txn_id_t requester, txn_id_t *victim) {
  // Build the waits-for graph: an edge w -> h whenever waiting txn w blocks on a
  // resource held (granted) by h with a conflicting mode.
  std::map<txn_id_t, std::vector<txn_id_t>> graph;
  for (auto &kv : waiting_rid_) {
    txn_id_t w = kv.first;
    const RowId &rid = kv.second;
    LockMode wm = waiting_mode_[w];
    auto it = table_.find(rid);
    if (it == table_.end()) continue;
    for (const LockRequest &r : it->second.reqs) {
      if (r.granted && r.txn != w && LockConflict(wm, r.mode)) {
        graph[w].push_back(r.txn);
      }
    }
  }

  // DFS from `requester` looking for a cycle; collect the cycle's members.
  std::vector<txn_id_t> stack;
  std::map<txn_id_t, int> on_stack;  // txn -> index in `stack`, if present
  std::map<txn_id_t, bool> visited;
  std::vector<txn_id_t> cycle;

  std::function<bool(txn_id_t)> dfs = [&](txn_id_t u) -> bool {
    visited[u] = true;
    on_stack[u] = static_cast<int>(stack.size());
    stack.push_back(u);
    for (txn_id_t v : graph[u]) {
      auto sit = on_stack.find(v);
      if (sit != on_stack.end()) {  // back-edge: cycle from sit->second to top
        for (size_t i = sit->second; i < stack.size(); ++i) cycle.push_back(stack[i]);
        return true;
      }
      if (!visited[v] && dfs(v)) return true;
    }
    stack.pop_back();
    on_stack.erase(u);
    return false;
  };

  if (!dfs(requester) || cycle.empty()) return false;
  *victim = *std::max_element(cycle.begin(), cycle.end());  // youngest = largest id
  return true;
}

bool LockManager::Acquire(Transaction *txn, const RowId &rid, LockMode mode) {
  std::unique_lock<std::mutex> lk(latch_);
  if (txn->state() == TxnState::ABORTED) throw TransactionAbortException(txn->id());
  if (txn->state() == TxnState::SHRINKING) {
    throw std::runtime_error("2PL violation: cannot acquire lock while shrinking");
  }

  // Already holds an equal-or-stronger lock?
  if (txn->HoldsExclusive(rid)) return true;
  if (mode == LockMode::SHARED && txn->HoldsShared(rid)) return true;

  LockQueue &q = table_[rid];

  // Upgrade S -> X: drop the existing shared request first.
  if (mode == LockMode::EXCLUSIVE && txn->HoldsShared(rid)) {
    q.reqs.remove_if([&](const LockRequest &r) { return r.txn == txn->id(); });
    txn->shared_locks().erase(rid);
  }

  q.reqs.push_back({txn->id(), mode, false});
  auto req_it = std::prev(q.reqs.end());
  txn_map_[txn->id()] = txn;

  while (!Grantable(q, txn->id(), mode)) {
    waiting_rid_[txn->id()] = rid;
    waiting_mode_[txn->id()] = mode;

    txn_id_t victim;
    if (DetectDeadlock(txn->id(), &victim)) {
      Transaction *v = txn_map_[victim];
      if (v) v->set_state(TxnState::ABORTED);
      if (victim == txn->id()) {
        q.reqs.erase(req_it);
        waiting_rid_.erase(txn->id());
        waiting_mode_.erase(txn->id());
        NotifyAll();
        throw TransactionAbortException(txn->id());
      }
      // Wake the victim so it can unwind itself.
      auto wit = waiting_rid_.find(victim);
      if (wit != waiting_rid_.end()) table_[wit->second].cv.notify_all();
    }

    q.cv.wait(lk);

    if (txn->state() == TxnState::ABORTED) {
      q.reqs.erase(req_it);
      waiting_rid_.erase(txn->id());
      waiting_mode_.erase(txn->id());
      NotifyAll();
      throw TransactionAbortException(txn->id());
    }
  }

  req_it->granted = true;
  waiting_rid_.erase(txn->id());
  waiting_mode_.erase(txn->id());
  if (mode == LockMode::SHARED) txn->shared_locks().insert(rid);
  else                          txn->exclusive_locks().insert(rid);
  return true;
}

bool LockManager::LockShared(Transaction *txn, const RowId &rid) {
  return Acquire(txn, rid, LockMode::SHARED);
}
bool LockManager::LockExclusive(Transaction *txn, const RowId &rid) {
  return Acquire(txn, rid, LockMode::EXCLUSIVE);
}

void LockManager::UnlockAll(Transaction *txn) {
  std::unique_lock<std::mutex> lk(latch_);
  auto release = [&](const RowId &rid) {
    auto it = table_.find(rid);
    if (it == table_.end()) return;
    it->second.reqs.remove_if([&](const LockRequest &r) { return r.txn == txn->id(); });
  };
  for (const RowId &r : txn->shared_locks()) release(r);
  for (const RowId &r : txn->exclusive_locks()) release(r);
  txn->shared_locks().clear();
  txn->exclusive_locks().clear();
  txn_map_.erase(txn->id());
  if (txn->state() != TxnState::ABORTED) txn->set_state(TxnState::SHRINKING);
  NotifyAll();  // released locks may unblock waiters
}

}  // namespace minidb
