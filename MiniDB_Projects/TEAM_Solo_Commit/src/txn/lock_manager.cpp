#include "lock_manager.h"

#include <algorithm>

namespace minidb {

bool LockManager::CanGrant(int64_t rid_key, int txn, LockMode mode) {
    auto it = table_.find(rid_key);
    if (it == table_.end()) return true;
    for (const Request& r : it->second) {
        if (r.granted && r.txn != txn && Conflicts(mode, r.mode)) return false;
    }
    return true;
}

bool LockManager::DfsCycle(int u, std::unordered_set<int>& visited, std::unordered_set<int>& stack) {
    visited.insert(u);
    stack.insert(u);
    auto it = waits_for_.find(u);
    if (it != waits_for_.end()) {
        for (int v : it->second) {
            if (stack.count(v)) return true;                          // back-edge => cycle
            if (!visited.count(v) && DfsCycle(v, visited, stack)) return true;
        }
    }
    stack.erase(u);
    return false;
}

bool LockManager::HasDeadlock() {
    std::unordered_set<int> visited, stack;
    for (const auto& [u, _] : waits_for_) {
        if (!visited.count(u) && DfsCycle(u, visited, stack)) return true;
    }
    return false;
}

bool LockManager::Acquire(int txn, int64_t rid_key, LockMode mode) {
    std::unique_lock<std::mutex> lk(mtx_);

    while (!CanGrant(rid_key, txn, mode)) {
        // Record that `txn` waits for every conflicting holder, then look for a cycle.
        for (const Request& r : table_[rid_key]) {
            if (r.granted && r.txn != txn && Conflicts(mode, r.mode))
                waits_for_[txn].insert(r.txn);
        }
        if (HasDeadlock()) {
            waits_for_.erase(txn);  // back off; caller aborts to break the cycle
            return false;
        }
        cv_.wait(lk);
    }

    waits_for_.erase(txn);  // no longer waiting

    // Already hold a lock here? Upgrade S->X in place; otherwise grant a new one.
    for (Request& r : table_[rid_key]) {
        if (r.txn == txn) {
            if (mode == LockMode::EXCLUSIVE) r.mode = LockMode::EXCLUSIVE;
            return true;
        }
    }
    table_[rid_key].push_back({txn, mode, true});
    return true;
}

void LockManager::ReleaseAll(int txn) {
    std::unique_lock<std::mutex> lk(mtx_);
    for (auto& [key, reqs] : table_) {
        reqs.erase(std::remove_if(reqs.begin(), reqs.end(),
                                  [txn](const Request& r) { return r.txn == txn; }),
                   reqs.end());
    }
    waits_for_.erase(txn);
    for (auto& [u, set] : waits_for_) set.erase(txn);
    cv_.notify_all();
}

}  // namespace minidb
