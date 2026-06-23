// lock_manager.cpp
#include "lock_manager.h"

namespace lab8 {

std::vector<TxnId> LockManager::conflict_holders(TxnId txn,
                                                 const std::string& item,
                                                 LockMode mode) const {
    std::vector<TxnId> out;
    auto it = locks_.find(item);
    if (it == locks_.end()) return out;
    const LockEntry& e = it->second;

    // An X lock held by another txn conflicts with any request.
    if (e.exclusive != 0 && e.exclusive != txn) out.push_back(e.exclusive);

    // S locks held by others conflict only with an X request (S/S is fine).
    if (mode == LockMode::X) {
        for (TxnId h : e.shared)
            if (h != txn) out.push_back(h);
    }
    return out;
}

bool LockManager::would_block(TxnId txn, const std::string& item,
                              LockMode mode) const {
    return !conflict_holders(txn, item, mode).empty();
}

bool LockManager::holds(TxnId txn, const std::string& item,
                        LockMode mode) const {
    auto it = locks_.find(item);
    if (it == locks_.end()) return false;
    const LockEntry& e = it->second;
    if (e.exclusive == txn) return true;          // X grants both S and X
    if (mode == LockMode::S) return e.shared.count(txn) > 0;
    return false;
}

LockReply LockManager::acquire(TxnId txn, const std::string& item,
                               LockMode mode) {
    LockEntry& e = locks_[item];

    // Already satisfied? (X covers S; S covers S.)
    if (holds(txn, item, mode)) return {LockResult::Granted, 0};

    auto blockers = conflict_holders(txn, item, mode);

    if (blockers.empty()) {
        // Grant. Handle the S->X upgrade case (txn had S, now wants X).
        if (mode == LockMode::X) {
            e.shared.erase(txn);   // drop our own S if upgrading
            e.exclusive = txn;
        } else {
            e.shared.insert(txn);
        }
        // Whatever we were waiting on is moot now.
        wait_for_.erase(txn);
        return {LockResult::Granted, 0};
    }

    // Cannot grant: we must wait. Record wait-for edges and look for a cycle.
    TxnId victim = detect_deadlock(txn, blockers);
    if (victim != 0) return {LockResult::Deadlock, victim};

    // Blocked but no cycle: a benign wait. In the single-threaded sim the
    // caller treats this as "blocked" (the op does not complete now).
    return {LockResult::Blocked, 0};
}

TxnId LockManager::detect_deadlock(TxnId requester,
                                   const std::vector<TxnId>& blockers) {
    // requester is waiting on each blocker.
    for (TxnId b : blockers) wait_for_[requester].insert(b);

    std::unordered_set<TxnId> visiting, done;
    if (dfs_cycle(requester, requester, visiting, done)) {
        // Victim policy: abort the YOUNGEST txn in the cycle (largest id),
        // which here is the requester that just closed the cycle. This is the
        // classic "wound-wait / abort the one that created the cycle" choice.
        return requester;
    }
    return 0;
}

bool LockManager::dfs_cycle(TxnId start, TxnId node,
                            std::unordered_set<TxnId>& visiting,
                            std::unordered_set<TxnId>& done) const {
    visiting.insert(node);
    auto it = wait_for_.find(node);
    if (it != wait_for_.end()) {
        for (TxnId next : it->second) {
            if (next == start) return true;          // back to origin: cycle
            if (visiting.count(next)) return true;    // cycle elsewhere
            if (done.count(next)) continue;
            if (dfs_cycle(start, next, visiting, done)) return true;
        }
    }
    visiting.erase(node);
    done.insert(node);
    return false;
}

void LockManager::release_all(TxnId txn) {
    for (auto& [item, e] : locks_) {
        e.shared.erase(txn);
        if (e.exclusive == txn) e.exclusive = 0;
    }
    // Remove this txn from the wait-for graph (as waiter and as a target).
    wait_for_.erase(txn);
    for (auto& [w, targets] : wait_for_) targets.erase(txn);
}

}  // namespace lab8
