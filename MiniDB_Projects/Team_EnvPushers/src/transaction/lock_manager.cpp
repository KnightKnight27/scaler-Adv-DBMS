#include "transaction/lock_manager.hpp"

namespace minidb {

bool LockManager::can_grant(LockEntry& e, const Request& req) {
    // Grantable iff every *other* request currently in the queue that conflicts
    // is not granted ahead of us. We use a simple rule: a request is grantable
    // when all already-granted requests are mode-compatible with it. This lets
    // shared locks share and serializes exclusive locks.
    for (auto& r : e.queue) {
        if (r.txn == req.txn) continue;          // ignore self (e.g. upgrade)
        if (r.granted && !compatible(r.mode, req.mode)) return false;
    }
    return true;
}

bool LockManager::dfs_cycle(TxnId start, TxnId at,
                            std::unordered_set<TxnId>& visited) {
    // Walk wait-for edges: `at` waits for every txn holding a conflicting,
    // granted lock on any resource where `at` has an ungranted request.
    for (auto& [id, entry] : table_) {
        bool at_waits_here = false;
        LockMode at_mode = LockMode::SHARED;
        for (auto& r : entry.queue)
            if (r.txn == at && !r.granted) { at_waits_here = true; at_mode = r.mode; }
        if (!at_waits_here) continue;
        for (auto& holder : entry.queue) {
            if (holder.granted && holder.txn != at &&
                !compatible(holder.mode, at_mode)) {
                if (holder.txn == start) return true;          // cycle back to start
                if (visited.insert(holder.txn).second &&
                    dfs_cycle(start, holder.txn, visited))
                    return true;
            }
        }
    }
    return false;
}

bool LockManager::has_deadlock(TxnId requester) {
    std::unordered_set<TxnId> visited;
    return dfs_cycle(requester, requester, visited);
}

void LockManager::acquire(TxnId txn, LockId id, LockMode mode) {
    std::unique_lock<std::mutex> lk(latch_);
    LockEntry& e = table_[id];

    // Already hold something on this resource?
    for (auto& r : e.queue) {
        if (r.txn == txn && r.granted) {
            if (r.mode == LockMode::EXCLUSIVE || r.mode == mode) return;  // already sufficient
            // Upgrade S -> X.
            r.mode = LockMode::EXCLUSIVE;
            // Must wait until no other shared holders.
            while (!can_grant(e, r)) {
                if (has_deadlock(txn)) {
                    r.mode = LockMode::SHARED;   // revert
                    throw DeadlockError(txn);
                }
                e.cv.wait(lk);
            }
            return;
        }
    }

    e.queue.push_back(Request{txn, mode, false});
    Request& req = e.queue.back();
    while (!can_grant(e, req)) {
        if (has_deadlock(txn)) {
            // Remove our request before bailing out.
            for (auto it = e.queue.begin(); it != e.queue.end(); ++it)
                if (it->txn == txn && !it->granted) { e.queue.erase(it); break; }
            throw DeadlockError(txn);
        }
        e.cv.wait(lk);
    }
    req.granted = true;
    held_[txn].insert(id);
}

void LockManager::release_all(TxnId txn) {
    std::unique_lock<std::mutex> lk(latch_);
    auto it = held_.find(txn);
    if (it == held_.end()) return;
    for (LockId id : it->second) {
        LockEntry& e = table_[id];
        for (auto rit = e.queue.begin(); rit != e.queue.end();) {
            if (rit->txn == txn) rit = e.queue.erase(rit);
            else ++rit;
        }
        e.cv.notify_all();  // wake waiters to re-check
    }
    held_.erase(it);
}

}  // namespace minidb
