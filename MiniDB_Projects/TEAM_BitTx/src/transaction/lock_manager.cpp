#include "transaction/lock_manager.h"

namespace minidb {

using namespace std;

bool LockManager::LockShared(TxnId txn, int32_t rid) {
  lock_guard<mutex> lock(mu_);
  auto& e = locks_[rid];
  if (e.exclusive != INVALID_TXN_ID && e.exclusive != txn) {
    waiting_[txn][rid] = {e.exclusive, LockMode::EXCLUSIVE};
    return false;
  }
  e.shared.insert(txn);
  held_[txn].insert(rid);
  return true;
}

bool LockManager::LockExclusive(TxnId txn, int32_t rid) {
  lock_guard<mutex> lock(mu_);
  auto& e = locks_[rid];
  if (e.exclusive == txn)
    return true;
  if (e.exclusive != INVALID_TXN_ID) {
    waiting_[txn][rid] = {e.exclusive, LockMode::EXCLUSIVE};
    return false;
  }
  if (!e.shared.empty()) {
    TxnId holder = e.shared.size() == 1 ? *e.shared.begin() : INVALID_TXN_ID;
    if (holder != txn) {
      waiting_[txn][rid] = {holder, LockMode::SHARED};
      return false;
    }
  }
  e.exclusive = txn;
  e.shared.clear();
  held_[txn].insert(rid);
  waiting_[txn].erase(rid);
  return true;
}

bool LockManager::Unlock(TxnId txn, int32_t rid) {
  lock_guard<mutex> lock(mu_);
  auto it = locks_.find(rid);
  if (it == locks_.end())
    return false;
  auto& e = it->second;
  if (e.exclusive == txn)
    e.exclusive = INVALID_TXN_ID;
  e.shared.erase(txn);
  if (e.shared.empty() && e.exclusive == INVALID_TXN_ID)
    locks_.erase(it);
  held_[txn].erase(rid);
  waiting_[txn].erase(rid);
  return true;
}

bool LockManager::UnlockAll(TxnId txn) {
  lock_guard<mutex> lock(mu_);
  auto it = held_.find(txn);
  if (it == held_.end())
    return false;
  for (int32_t rid : it->second) {
    auto lit = locks_.find(rid);
    if (lit != locks_.end()) {
      auto& e = lit->second;
      if (e.exclusive == txn)
        e.exclusive = INVALID_TXN_ID;
      e.shared.erase(txn);
      if (e.shared.empty() && e.exclusive == INVALID_TXN_ID)
        locks_.erase(lit);
    }
  }
  held_.erase(it);
  waiting_.erase(txn);
  return true;
}

vector<LockManager::WaitForEntry> LockManager::BuildWaitFor(TxnId txn, int32_t rid,
                                                            LockMode mode) const {
  vector<WaitForEntry> out;
  auto lit = locks_.find(rid);
  if (lit == locks_.end())
    return out;
  auto& e = lit->second;
  if (mode == LockMode::SHARED) {
    if (e.exclusive != INVALID_TXN_ID && e.exclusive != txn) {
      out.push_back({e.exclusive, LockMode::EXCLUSIVE});
    }
  } else {
    if (e.exclusive != INVALID_TXN_ID && e.exclusive != txn) {
      out.push_back({e.exclusive, LockMode::EXCLUSIVE});
    }
    for (auto s : e.shared) {
      if (s != txn)
        out.push_back({s, LockMode::SHARED});
    }
  }
  return out;
}

bool LockManager::DetectCycleFrom(TxnId start, unordered_set<TxnId>& visited,
                                  unordered_set<TxnId>& stack) const {
  if (stack.count(start))
    return true;
  if (visited.count(start))
    return false;
  visited.insert(start);
  stack.insert(start);
  auto wit = waiting_.find(start);
  if (wit != waiting_.end()) {
    for (auto& [rid, wfe] : wit->second) {
      auto entries = BuildWaitFor(start, rid, wfe.mode);
      for (auto& w : entries) {
        if (w.holder != INVALID_TXN_ID && DetectCycleFrom(w.holder, visited, stack)) {
          return true;
        }
      }
    }
  }
  stack.erase(start);
  return false;
}

bool LockManager::HasCycle(TxnId txn) const {
  unordered_set<TxnId> visited, stack;
  return DetectCycleFrom(txn, visited, stack);
}

} // namespace minidb