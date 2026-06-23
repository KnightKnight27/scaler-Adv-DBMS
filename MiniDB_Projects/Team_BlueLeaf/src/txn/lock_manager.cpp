#include "txn/lock_manager.h"

#include <stack>

namespace minidb {

bool LockManager::compatible(const std::string& key, TxId tx, LockMode mode) {
    auto it = table_.find(key);
    if (it == table_.end()) return true;
    for (const Request& r : it->second) {
        if (!r.granted || r.tx == tx) continue;
        // S/S is fine; anything involving an X conflicts.
        if (mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE) return false;
    }
    return true;
}

bool LockManager::has_cycle(TxId start) {
    std::stack<TxId> st;
    std::unordered_set<TxId> seen;
    auto begin = waits_for_.find(start);
    if (begin != waits_for_.end())
        for (TxId n : begin->second) st.push(n);
    while (!st.empty()) {
        TxId u = st.top(); st.pop();
        if (u == start) return true;        // path returned to start -> cycle
        if (seen.count(u)) continue;
        seen.insert(u);
        auto it = waits_for_.find(u);
        if (it != waits_for_.end())
            for (TxId v : it->second) st.push(v);
    }
    return false;
}

void LockManager::acquire(TxId tx, const std::string& key, LockMode mode) {
    std::unique_lock<std::mutex> lk(mu_);
    auto& q = table_[key];

    // Already hold a lock on this key?
    for (Request& r : q) {
        if (r.tx == tx && r.granted) {
            if (r.mode == LockMode::EXCLUSIVE || mode == LockMode::SHARED) return;  // sufficient
            // Upgrade S -> X: wait until this tx is the only granted holder.
            for (;;) {
                std::unordered_set<TxId> blockers;
                for (const Request& o : q)
                    if (o.granted && o.tx != tx) blockers.insert(o.tx);
                if (blockers.empty()) { r.mode = LockMode::EXCLUSIVE; return; }
                waits_for_[tx] = blockers;
                if (has_cycle(tx)) { waits_for_.erase(tx); throw DeadlockException(tx); }
                cv_.wait(lk);
            }
        }
    }

    // New request.
    q.push_back(Request{tx, mode, false});
    for (;;) {
        if (compatible(key, tx, mode)) {
            for (Request& r : q) if (r.tx == tx && !r.granted) { r.mode = mode; r.granted = true; }
            held_[tx].insert(key);
            waits_for_.erase(tx);
            cv_.notify_all();
            return;
        }
        std::unordered_set<TxId> blockers;
        for (const Request& o : q)
            if (o.granted && o.tx != tx &&
                (mode == LockMode::EXCLUSIVE || o.mode == LockMode::EXCLUSIVE))
                blockers.insert(o.tx);
        waits_for_[tx] = blockers;
        if (has_cycle(tx)) {
            q.remove_if([tx](const Request& r) { return r.tx == tx && !r.granted; });
            waits_for_.erase(tx);
            cv_.notify_all();
            throw DeadlockException(tx);
        }
        cv_.wait(lk);
    }
}

void LockManager::release_all(TxId tx) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [key, q] : table_) q.remove_if([tx](const Request& r) { return r.tx == tx; });
    held_.erase(tx);
    waits_for_.erase(tx);
    for (auto& [t, s] : waits_for_) s.erase(tx);
    cv_.notify_all();
}

} // namespace minidb
