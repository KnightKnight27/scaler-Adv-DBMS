#include "txn/lock_manager.h"

namespace minidb {

bool LockManager::has_conflict(LockQueue &q, txn_id_t self, LockMode mode) {
    for (auto &r : q.requests) {
        if (!r.granted || r.txn == self) continue;
        // X conflicts with everything; S conflicts only with X.
        if (mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE) return true;
    }
    return false;
}

bool LockManager::detects_cycle(txn_id_t start) {
    // Iterative DFS with a recursion stack to find a back-edge (cycle).
    std::set<txn_id_t> visited;
    std::set<txn_id_t> on_stack;
    std::vector<std::pair<txn_id_t, std::set<txn_id_t>::iterator>> stack;

    auto push = [&](txn_id_t n) {
        visited.insert(n);
        on_stack.insert(n);
        stack.push_back({n, waits_for_[n].begin()});
    };
    push(start);
    while (!stack.empty()) {
        auto &[node, it] = stack.back();
        auto &edges = waits_for_[node];
        if (it == edges.end()) {
            on_stack.erase(node);
            stack.pop_back();
            continue;
        }
        txn_id_t next = *it;
        ++it;
        if (on_stack.count(next)) return true;        // back-edge => cycle
        if (!visited.count(next)) push(next);
    }
    return false;
}

void LockManager::acquire(Transaction *txn, const std::string &resource, LockMode mode) {
    std::unique_lock<std::mutex> lk(latch_);

    // Already holding this resource: good enough (we do not model S->X upgrade;
    // documented limitation — callers take X up-front when they intend to write).
    if (txn->locks().count(resource)) return;

    if (!table_.count(resource)) table_[resource] = std::make_unique<LockQueue>();
    LockQueue &q = *table_[resource];
    q.requests.push_back({txn->id(), mode, false});

    while (has_conflict(q, txn->id(), mode)) {
        // Record wait-for edges: this txn waits for every conflicting holder.
        for (auto &r : q.requests) {
            if (r.granted && r.txn != txn->id()) {
                if (mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE) {
                    waits_for_[txn->id()].insert(r.txn);
                }
            }
        }
        // If waiting would close a cycle, this txn becomes the deadlock victim.
        if (detects_cycle(txn->id())) {
            waits_for_.erase(txn->id());
            // Remove our pending (ungranted) request from the queue.
            for (auto it = q.requests.begin(); it != q.requests.end(); ++it) {
                if (it->txn == txn->id() && !it->granted) { q.requests.erase(it); break; }
            }
            throw TransactionAbortException("deadlock detected");
        }
        q.cv.wait(lk);
    }

    // Grant the lock.
    for (auto &r : q.requests) {
        if (r.txn == txn->id() && !r.granted) { r.granted = true; break; }
    }
    waits_for_.erase(txn->id());
    txn->locks().insert(resource);
}

void LockManager::release_all(Transaction *txn) {
    std::unique_lock<std::mutex> lk(latch_);
    for (const std::string &resource : txn->locks()) {
        auto qit = table_.find(resource);
        if (qit == table_.end()) continue;
        LockQueue &q = *qit->second;
        for (auto it = q.requests.begin(); it != q.requests.end(); ++it) {
            if (it->txn == txn->id()) { q.requests.erase(it); break; }
        }
        q.cv.notify_all(); // wake waiters who may now proceed
    }
    txn->locks().clear();
    // Drop any wait-for edges originating from this txn.
    waits_for_.erase(txn->id());
    for (auto &kv : waits_for_) kv.second.erase(txn->id());
}

} // namespace minidb
