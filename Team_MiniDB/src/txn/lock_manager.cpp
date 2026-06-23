#include "lock_manager.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <vector>

namespace minidb {

LockManager::LockManager() {
  detector_thread_ = std::thread([this] { DeadlockDetectionLoop(); });
}

LockManager::~LockManager() {
  stop_ = true;
  if (detector_thread_.joinable()) detector_thread_.join();
}

std::shared_ptr<LockManager::LockQueue> LockManager::GetQueue(const std::string &resource) {
  std::lock_guard<std::mutex> g(table_mtx_);
  auto it = table_.find(resource);
  if (it != table_.end()) return it->second;
  auto q = std::make_shared<LockQueue>();
  table_[resource] = q;
  return q;
}

bool LockManager::IsCompatible(LockQueue &q, const std::list<LockRequest>::iterator &me) {
  for (auto it = q.requests.begin(); it != q.requests.end(); ++it) {
    if (it == me || !it->granted) continue;
    if (it->txn_id == me->txn_id) continue;  // a txn never conflicts with its own locks
    if (me->exclusive || it->exclusive) return false;
  }
  return true;
}

bool LockManager::IsAborted(txn_id_t id) {
  std::lock_guard<std::mutex> g(aborted_mtx_);
  return aborted_txns_.count(id) > 0;
}

void LockManager::LockShared(Transaction *txn, const std::string &resource) {
  if (txn->SharedLocks().count(resource) || txn->ExclusiveLocks().count(resource)) return;
  auto q = GetQueue(resource);
  std::unique_lock<std::mutex> lk(q->mtx);
  q->requests.push_back({txn->Id(), false, false});
  auto me = std::prev(q->requests.end());
  while (!IsCompatible(*q, me)) {
    if (q->cv.wait_for(lk, std::chrono::milliseconds(50), [&] { return IsCompatible(*q, me) || IsAborted(txn->Id()); })) {
      // predicate satisfied
    }
    if (IsAborted(txn->Id())) {
      q->requests.erase(me);
      q->cv.notify_all();
      throw TransactionAbortedException(txn->Id());
    }
  }
  me->granted = true;
  txn->RecordSharedLock(resource);
}

void LockManager::LockExclusive(Transaction *txn, const std::string &resource) {
  if (txn->ExclusiveLocks().count(resource)) return;
  auto q = GetQueue(resource);
  std::unique_lock<std::mutex> lk(q->mtx);
  q->requests.push_back({txn->Id(), true, false});
  auto me = std::prev(q->requests.end());
  while (!IsCompatible(*q, me)) {
    q->cv.wait_for(lk, std::chrono::milliseconds(50), [&] { return IsCompatible(*q, me) || IsAborted(txn->Id()); });
    if (IsAborted(txn->Id())) {
      q->requests.erase(me);
      q->cv.notify_all();
      throw TransactionAbortedException(txn->Id());
    }
  }
  me->granted = true;
  txn->RecordExclusiveLock(resource);
}

void LockManager::ReleaseAll(Transaction *txn) {
  std::lock_guard<std::mutex> g(table_mtx_);
  for (auto &[resource, q] : table_) {
    std::lock_guard<std::mutex> ql(q->mtx);
    bool changed = false;
    for (auto it = q->requests.begin(); it != q->requests.end();) {
      if (it->txn_id == txn->Id()) { it = q->requests.erase(it); changed = true; }
      else ++it;
    }
    if (changed) q->cv.notify_all();
  }
  std::lock_guard<std::mutex> g2(aborted_mtx_);
  aborted_txns_.erase(txn->Id());
}

void LockManager::DeadlockDetectionLoop() {
  while (!stop_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Build wait-for graph: edge waiter -> holder for every blocked request.
    std::unordered_map<txn_id_t, std::unordered_set<txn_id_t>> graph;
    {
      std::lock_guard<std::mutex> g(table_mtx_);
      for (auto &[resource, q] : table_) {
        std::lock_guard<std::mutex> ql(q->mtx);
        for (auto &waiter : q->requests) {
          if (waiter.granted) continue;
          for (auto &holder : q->requests) {
            if (!holder.granted || holder.txn_id == waiter.txn_id) continue;
            if (waiter.exclusive || holder.exclusive) graph[waiter.txn_id].insert(holder.txn_id);
          }
        }
      }
    }

    // DFS cycle detection; on a cycle, abort the youngest (max id) member.
    std::unordered_set<txn_id_t> visited, on_stack;
    std::vector<txn_id_t> victim_candidates;
    std::function<bool(txn_id_t, std::vector<txn_id_t> &)> dfs = [&](txn_id_t node, std::vector<txn_id_t> &path) {
      visited.insert(node);
      on_stack.insert(node);
      path.push_back(node);
      for (txn_id_t next : graph[node]) {
        if (on_stack.count(next)) {
          // Cycle found: victim = max id among the cycle on the path.
          txn_id_t victim = next;
          bool in_cycle = false;
          for (auto it = path.rbegin(); it != path.rend(); ++it) {
            if (*it == next) in_cycle = true;
            if (in_cycle) victim = std::max(victim, *it);
          }
          victim_candidates.push_back(victim);
          return true;
        }
        if (!visited.count(next) && dfs(next, path)) return true;
      }
      path.pop_back();
      on_stack.erase(node);
      return false;
    };
    for (auto &[node, _] : graph) {
      if (!visited.count(node)) {
        std::vector<txn_id_t> path;
        dfs(node, path);
      }
    }

    if (!victim_candidates.empty()) {
      std::lock_guard<std::mutex> g(aborted_mtx_);
      for (auto v : victim_candidates) aborted_txns_.insert(v);
    }
    // Wake everyone so aborted waiters notice and throw.
    std::lock_guard<std::mutex> g(table_mtx_);
    for (auto &[resource, q] : table_) q->cv.notify_all();
  }
}

}  // namespace minidb
