#include "txn/lock_manager.h"

namespace minidb {

void LockManager::lockShared(Transaction *txn, const string &table) {
  acquire(txn, table, LockMode::kShared);
}
void LockManager::lockExclusive(Transaction *txn, const string &table) {
  acquire(txn, table, LockMode::kExclusive);
}

bool LockManager::compatible(const LockQueue &q, const Request &r) const {
  // A request is grantable iff it does not conflict with any *granted* lock
  // held by a *different* transaction.
  for (const Request &o : q.queue) {
    if (o.txn == r.txn || !o.granted) continue;
    if (r.mode == LockMode::kExclusive) return false;  // X conflicts with all
    if (o.mode == LockMode::kExclusive) return false;  // our S conflicts with X
  }
  return true;
}

void LockManager::acquire(Transaction *txn, const string &table, LockMode mode) {
  unique_lock<mutex> lk(latch_);
  LockQueue &q = tables_[table];

  // Already hold a lock on this table?  Handle re-request / upgrade.
  for (Request &r : q.queue) {
    if (r.txn != txn->id()) continue;
    if (r.mode == LockMode::kExclusive) return;        // already strongest
    if (mode == LockMode::kShared) return;             // have S, want S
    r.mode = LockMode::kExclusive;                     // upgrade S -> X
    r.granted = false;
    while (!compatible(q, r)) {
      if (hasCycle(txn->id())) {
        q.cv.notify_all();
        throw TxnError("deadlock detected on upgrade; transaction aborted");
      }
      q.cv.wait(lk);
    }
    r.granted = true;
    txn->locks()[table] = LockMode::kExclusive;
    return;
  }

  // Brand-new request: append it, then wait until it is grantable.
  q.queue.push_back({txn->id(), mode, false});
  Request &self = q.queue.back();
  while (!compatible(q, self)) {
    if (hasCycle(txn->id())) {
      // remove our pending request so the graph is clean, then abort.
      q.queue.remove_if([&](const Request &x) { return x.txn == txn->id(); });
      q.cv.notify_all();
      throw TxnError("deadlock detected; transaction aborted");
    }
    q.cv.wait(lk);
  }
  self.granted = true;
  txn->locks()[table] = mode;
}

void LockManager::releaseAll(Transaction *txn) {
  lock_guard<mutex> lk(latch_);
  for (auto &[table, mode] : txn->locks()) {
    (void)mode;
    LockQueue &q = tables_[table];
    q.queue.remove_if([&](const Request &r) { return r.txn == txn->id(); });
    q.cv.notify_all();   // wake waiters: a lock may now be grantable
  }
  txn->locks().clear();
}

unordered_map<txn_id_t, vector<txn_id_t>> LockManager::buildWaitForGraph() {
  // Edge w -> g means "transaction w is blocked waiting for a lock that g holds".
  unordered_map<txn_id_t, vector<txn_id_t>> graph;
  for (auto &[table, q] : tables_) {
    for (const Request &w : q.queue) {
      if (w.granted) continue;             // only waiters create edges
      for (const Request &g : q.queue) {
        if (!g.granted || g.txn == w.txn) continue;
        // w waits for g if their modes conflict (X waits for anyone; S for X).
        if (w.mode == LockMode::kExclusive || g.mode == LockMode::kExclusive)
          graph[w.txn].push_back(g.txn);
      }
    }
  }
  return graph;
}

bool LockManager::hasCycle(txn_id_t start) {
  auto graph = buildWaitForGraph();
  unordered_set<txn_id_t> visited;
  // DFS from `start`; if we can get back to `start`, the wait-for graph has a
  // cycle that includes this transaction.
  function<bool(txn_id_t)> dfs = [&](txn_id_t u) {
    for (txn_id_t v : graph[u]) {
      if (v == start) return true;
      if (!visited.count(v)) { visited.insert(v); if (dfs(v)) return true; }
    }
    return false;
  };
  return dfs(start);
}

}  // namespace minidb
