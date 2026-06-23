#include "minidb/transaction/lock_manager.h"

#include <algorithm>
#include <functional>
#include <stdexcept>

#include "minidb/common/trace.h"

namespace minidb {

std::unique_ptr<Transaction> LockManager::Begin() {
  std::scoped_lock lock(mutex_);
  return std::make_unique<Transaction>(next_txn_id_++);
}

bool LockManager::CanGrant(const Queue &queue, std::size_t position) const {
  const auto &request = queue.requests[position];
  for (std::size_t i = 0; i < queue.requests.size(); ++i) {
    if (i == position) continue;
    const auto &other = queue.requests[i];
    if (!other.granted && i > position) continue;
    if (other.txn_id == request.txn_id) continue;
    if (request.mode == LockMode::Exclusive ||
        other.mode == LockMode::Exclusive) {
      return false;
    }
  }
  return true;
}

void LockManager::Acquire(Transaction &txn, const std::string &table,
                          LockMode mode) {
  std::unique_lock lock(mutex_);
  if (txn.state_ != TransactionState::Active) {
    throw std::runtime_error("transaction is not active");
  }
  auto held = txn.locks_.find(table);
  if (held != txn.locks_.end()) {
    if (held->second == LockMode::Exclusive || held->second == mode) return;
    throw std::runtime_error("lock upgrade is not supported in this demo");
  }
  auto &queue = queues_[table];
  queue.requests.push_back({txn.id_, mode, false});
  auto find_position = [&]() {
    return static_cast<std::size_t>(
        std::find_if(queue.requests.begin(), queue.requests.end(),
                     [&](const Request &r) { return r.txn_id == txn.id_; }) -
        queue.requests.begin());
  };
  while (true) {
    const auto position = find_position();
    if (position >= queue.requests.size()) {
      throw std::runtime_error("lock request was cancelled");
    }
    if (CanGrant(queue, position)) {
      queue.requests[position].granted = true;
      txn.locks_[table] = mode;
      Trace::Log("LOCK", "T" + std::to_string(txn.id_) + " acquired " +
                             (mode == LockMode::Shared ? "S" : "X") +
                             " lock on " + table);
      return;
    }
    if (auto victim = DetectDeadlockUnlocked(); victim && *victim == txn.id_) {
      queue.requests.erase(queue.requests.begin() +
                           static_cast<std::ptrdiff_t>(position));
      txn.state_ = TransactionState::Aborted;
      throw std::runtime_error("deadlock detected; youngest transaction aborted");
    }
    queue.condition.wait(lock);
  }
}

void LockManager::LockShared(Transaction &txn, const std::string &table) {
  Acquire(txn, table, LockMode::Shared);
}

void LockManager::LockExclusive(Transaction &txn, const std::string &table) {
  Acquire(txn, table, LockMode::Exclusive);
}

void LockManager::ReleaseAll(Transaction &txn) {
  for (const auto &[table, mode] : txn.locks_) {
    (void)mode;
    auto queue_it = queues_.find(table);
    if (queue_it == queues_.end()) continue;
    auto &requests = queue_it->second.requests;
    std::erase_if(requests,
                  [&](const Request &r) { return r.txn_id == txn.id_; });
    queue_it->second.condition.notify_all();
  }
  txn.locks_.clear();
}

void LockManager::Commit(Transaction &txn) {
  std::scoped_lock lock(mutex_);
  txn.state_ = TransactionState::Committed;
  ReleaseAll(txn);
}

void LockManager::Abort(Transaction &txn) {
  std::scoped_lock lock(mutex_);
  txn.state_ = TransactionState::Aborted;
  ReleaseAll(txn);
}

std::unordered_map<TxnId, std::unordered_set<TxnId>>
LockManager::WaitsFor() const {
  std::unordered_map<TxnId, std::unordered_set<TxnId>> graph;
  for (const auto &[table, queue] : queues_) {
    (void)table;
    for (const auto &waiter : queue.requests) {
      if (waiter.granted) continue;
      for (const auto &holder : queue.requests) {
        if (!holder.granted || holder.txn_id == waiter.txn_id) continue;
        if (waiter.mode == LockMode::Exclusive ||
            holder.mode == LockMode::Exclusive) {
          graph[waiter.txn_id].insert(holder.txn_id);
        }
      }
    }
  }
  return graph;
}

std::optional<TxnId> LockManager::DetectDeadlockUnlocked() const {
  const auto graph = WaitsFor();
  std::unordered_set<TxnId> visited;
  std::unordered_set<TxnId> active;
  std::optional<TxnId> victim;
  std::function<void(TxnId)> visit = [&](TxnId node) {
    if (active.contains(node)) {
      victim = victim ? std::max(*victim, node) : node;
      return;
    }
    if (visited.contains(node)) return;
    visited.insert(node);
    active.insert(node);
    if (auto it = graph.find(node); it != graph.end()) {
      for (TxnId dependency : it->second) {
        visit(dependency);
        if (victim) *victim = std::max(*victim, dependency);
      }
    }
    active.erase(node);
  };
  for (const auto &[node, edges] : graph) {
    (void)edges;
    visit(node);
  }
  return victim;
}

std::optional<TxnId> LockManager::DetectDeadlock() const {
  std::scoped_lock lock(mutex_);
  return DetectDeadlockUnlocked();
}

}  // namespace minidb
