#include "txn/lock_manager.h"

#include <algorithm>
#include <functional>

namespace minidb {

bool LockManager::Acquire(TxnId txn, int64_t res, LockMode mode) {
  std::vector<Req>& q = table_[res];

  // Already hold something on this resource?
  for (Req& r : q) {
    if (r.txn == txn && r.granted) {
      if (r.mode == LockMode::X || mode == LockMode::S) return true;  // sufficient
      // Hold S, want X: upgrade only if no other transaction holds a lock.
      bool others = false;
      for (const Req& o : q)
        if (o.txn != txn && o.granted) { others = true; break; }
      if (!others) { r.mode = LockMode::X; return true; }
      q.push_back({txn, LockMode::X, false});  // wait to upgrade
      return false;
    }
  }

  // Fresh request: conflicts with any granted lock held by another txn?
  bool conflict = false;
  for (const Req& r : q) {
    if (r.granted && r.txn != txn && Conflicts(mode, r.mode)) { conflict = true; break; }
  }
  q.push_back({txn, mode, !conflict});
  return !conflict;
}

void LockManager::GrantWaiters(int64_t res, std::vector<TxnId>* woken) {
  std::vector<Req>& q = table_[res];
  // FIFO: grant waiting requests that no longer conflict with granted ones.
  for (size_t i = 0; i < q.size(); ++i) {
    if (q[i].granted) continue;
    bool conflict = false;
    for (size_t j = 0; j < q.size(); ++j) {
      if (j == i) continue;
      if (q[j].granted && q[j].txn != q[i].txn && Conflicts(q[i].mode, q[j].mode)) {
        conflict = true; break;
      }
    }
    if (!conflict) {
      q[i].granted = true;
      woken->push_back(q[i].txn);
    } else {
      // Stop at the first blocking request to preserve FIFO fairness for X.
      if (q[i].mode == LockMode::X) break;
    }
  }
}

std::vector<TxnId> LockManager::ReleaseAll(TxnId txn) {
  std::vector<TxnId> woken;
  for (auto& kv : table_) {
    std::vector<Req>& q = kv.second;
    size_t before = q.size();
    q.erase(std::remove_if(q.begin(), q.end(),
                           [txn](const Req& r) { return r.txn == txn; }),
            q.end());
    if (q.size() != before) GrantWaiters(kv.first, &woken);
  }
  // De-duplicate woken transactions.
  std::sort(woken.begin(), woken.end());
  woken.erase(std::unique(woken.begin(), woken.end()), woken.end());
  return woken;
}

bool LockManager::DetectDeadlock(TxnId* victim) {
  // Build wait-for edges: waiter -> holder for every conflicting (waiting,
  // granted) pair on the same resource.
  std::unordered_map<TxnId, std::vector<TxnId>> edges;
  std::vector<TxnId> nodes;
  for (const auto& kv : table_) {
    const std::vector<Req>& q = kv.second;
    for (const Req& w : q) {
      if (w.granted) continue;
      for (const Req& g : q) {
        if (g.granted && g.txn != w.txn && Conflicts(w.mode, g.mode)) {
          edges[w.txn].push_back(g.txn);
          nodes.push_back(w.txn);
          nodes.push_back(g.txn);
        }
      }
    }
  }
  if (nodes.empty()) return false;

  // DFS cycle detection; collect cycle members to choose a victim.
  std::unordered_map<TxnId, int> state;  // 0=unseen,1=in-stack,2=done
  std::vector<TxnId> stack;
  bool found = false;
  std::vector<TxnId> cycle;

  std::function<void(TxnId)> dfs = [&](TxnId u) {
    if (found) return;
    state[u] = 1;
    stack.push_back(u);
    for (TxnId v : edges[u]) {
      if (found) return;
      if (state[v] == 1) {
        // Found a back-edge: extract the cycle from the stack.
        found = true;
        size_t idx = stack.size();
        for (size_t k = 0; k < stack.size(); ++k)
          if (stack[k] == v) { idx = k; break; }
        for (size_t k = idx; k < stack.size(); ++k) cycle.push_back(stack[k]);
        return;
      }
      if (state[v] == 0) dfs(v);
    }
    stack.pop_back();
    state[u] = 2;
  };

  for (TxnId n : nodes) {
    if (state[n] == 0) dfs(n);
    if (found) break;
  }
  if (!found) return false;

  // Victim = youngest transaction (largest id) in the cycle.
  *victim = *std::max_element(cycle.begin(), cycle.end());
  return true;
}

std::string LockManager::Dump() const {
  std::string out;
  for (const auto& kv : table_) {
    if (kv.second.empty()) continue;
    out += "  res " + std::to_string(kv.first) + ": ";
    for (const Req& r : kv.second) {
      out += "T" + std::to_string(r.txn) +
             (r.mode == LockMode::X ? "(X" : "(S") +
             (r.granted ? ",granted) " : ",WAIT) ");
    }
    out += "\n";
  }
  return out;
}

}  // namespace minidb
