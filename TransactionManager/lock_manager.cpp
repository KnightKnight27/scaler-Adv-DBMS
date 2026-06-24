#include "lock_manager.h"
#include <algorithm>

// DFS over the wait-for graph: can we get from `node` back to `target`?
bool LockManager::reaches(int node, int target, std::set<int>& seen) const {
    auto it = wait_for_.find(node);
    if (it == wait_for_.end()) return false;
    for (int next : it->second) {
        if (next == target) return true;
        if (seen.insert(next).second && reaches(next, target, seen))
            return true;
    }
    return false;
}

bool LockManager::has_cycle(int start) {
    std::set<int> seen;
    return reaches(start, start, seen);
}

LockResult LockManager::acquire(int txn, const std::string& key, LockMode req) {
    // A fresh request supersedes any prior wait state for this txn.
    wait_for_.erase(txn);

    auto& holders = locks_[key];

    // What do we already hold on this key?
    bool have_s = false, have_x = false;
    for (const auto& e : holders)
        if (e.txn == txn) { (e.mode == LockMode::X ? have_x : have_s) = true; }

    if (have_x)                               return LockResult::Granted; // strongest already
    if (req == LockMode::S && have_s)         return LockResult::Granted;

    // Collect other transactions whose held lock conflicts with our request.
    // S conflicts only with X; X conflicts with both S and X.
    std::set<int> conflicts;
    for (const auto& e : holders) {
        if (e.txn == txn) continue;
        bool incompatible = (req == LockMode::X) || (e.mode == LockMode::X);
        if (incompatible) conflicts.insert(e.txn);
    }

    if (conflicts.empty()) {
        if (req == LockMode::X && have_s) {              // upgrade S -> X: drop our S
            holders.erase(std::remove_if(holders.begin(), holders.end(),
                          [&](const Entry& e) { return e.txn == txn; }),
                          holders.end());
        }
        holders.push_back({txn, req});
        return LockResult::Granted;
    }

    // Must wait: record edges requester -> each holder, then look for a cycle.
    for (int holder : conflicts) wait_for_[txn].insert(holder);
    return has_cycle(txn) ? LockResult::Deadlock : LockResult::Blocked;
}

void LockManager::release_all(int txn) {
    for (auto& [key, holders] : locks_) {
        holders.erase(std::remove_if(holders.begin(), holders.end(),
                      [&](const Entry& e) { return e.txn == txn; }),
                      holders.end());
    }
    wait_for_.erase(txn);                       // outgoing edges
    for (auto& [waiter, targets] : wait_for_)   // incoming edges
        targets.erase(txn);
}
