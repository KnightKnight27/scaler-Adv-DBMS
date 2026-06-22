#include "transaction_manager.hpp"
#include <algorithm>

namespace minidb {

void TransactionManager::setInitial(int key, int64_t value) {
    history_[key].push_back({0, value});
}

Txn TransactionManager::begin() {
    std::lock_guard<std::mutex> lk(mtx_);
    Txn t;
    t.id = next_id_++;
    t.snapshot = commit_clock_; // see everything committed so far
    return t;
}

// The version visible to a transaction's snapshot: the newest version whose
// commit timestamp is <= the snapshot. Returns 0 if the key has no such version.
int64_t TransactionManager::visibleValue(const Txn& t, int key) const {
    auto it = history_.find(key);
    if (it == history_.end()) return 0;
    int64_t val = 0;
    for (const Version& v : it->second) {
        if (v.commit_ts <= t.snapshot) val = v.value;
        else break; // versions are stored in ascending timestamp order
    }
    return val;
}

bool TransactionManager::canGrant(TxnId txid, int key, LockMode m,
                                  std::set<TxnId>& blockers) const {
    blockers.clear();
    auto xit = xlock_.find(key);
    if (xit != xlock_.end() && xit->second != txid) blockers.insert(xit->second);

    if (m == LockMode::EXCLUSIVE) {
        auto sit = slock_.find(key);
        if (sit != slock_.end())
            for (TxnId h : sit->second)
                if (h != txid) blockers.insert(h);
    }
    return blockers.empty();
}

// Depth-first search of the waits-for graph looking for a path back to `start`.
bool TransactionManager::hasCycle(TxnId start) const {
    std::set<TxnId> visited;
    std::vector<TxnId> stack;
    auto it = waits_.find(start);
    if (it == waits_.end()) return false;
    for (TxnId n : it->second) stack.push_back(n);
    while (!stack.empty()) {
        TxnId cur = stack.back();
        stack.pop_back();
        if (cur == start) return true;
        if (!visited.insert(cur).second) continue;
        auto wit = waits_.find(cur);
        if (wit != waits_.end())
            for (TxnId n : wit->second) stack.push_back(n);
    }
    return false;
}

void TransactionManager::acquire(std::unique_lock<std::mutex>& lk, Txn& t, int key, LockMode m) {
    // Already holding a sufficient lock?
    if (held_x_[t.id].count(key)) return;
    if (m == LockMode::SHARED && held_s_[t.id].count(key)) return;

    std::set<TxnId> blockers;
    while (!canGrant(t.id, key, m, blockers)) {
        waits_[t.id] = blockers;
        if (hasCycle(t.id)) {
            waits_.erase(t.id);
            deadlocks_++;
            throw DeadlockAbort(t.id);
        }
        cv_.wait(lk);
    }
    waits_.erase(t.id);

    if (m == LockMode::EXCLUSIVE) {
        // Upgrade path: drop our own shared lock on this key, if any.
        slock_[key].erase(t.id);
        held_s_[t.id].erase(key);
        xlock_[key] = t.id;
        held_x_[t.id].insert(key);
    } else {
        slock_[key].insert(t.id);
        held_s_[t.id].insert(key);
    }
}

int64_t TransactionManager::read(Txn& t, int key) {
    std::unique_lock<std::mutex> lk(mtx_);
    // Read-your-own-writes in both modes.
    auto wb = writes_.find(t.id);
    if (wb != writes_.end()) {
        auto kv = wb->second.find(key);
        if (kv != wb->second.end()) return kv->second;
    }
    if (mode_ == Mode::TWO_PL) {
        acquire(lk, t, key, LockMode::SHARED);
        // Latest committed value (locks guarantee no concurrent writer).
        auto it = history_.find(key);
        return it == history_.end() || it->second.empty() ? 0 : it->second.back().value;
    }
    // MVCC: no lock, snapshot read.
    return visibleValue(t, key);
}

void TransactionManager::write(Txn& t, int key, int64_t value) {
    std::unique_lock<std::mutex> lk(mtx_);
    acquire(lk, t, key, LockMode::EXCLUSIVE); // exclusive in both modes
    writes_[t.id][key] = value;
}

void TransactionManager::releaseAll(TxnId txid) {
    for (int k : held_x_[txid])
        if (xlock_.count(k) && xlock_[k] == txid) xlock_.erase(k);
    for (int k : held_s_[txid]) slock_[k].erase(txid);
    held_x_.erase(txid);
    held_s_.erase(txid);
    waits_.erase(txid);
}

void TransactionManager::commit(Txn& t) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto wb = writes_.find(t.id);

    // First-committer-wins (MVCC): if any key we wrote was committed by another
    // transaction after our snapshot, committing would lose that update. Abort
    // instead. This is what makes MVCC mode prevent lost updates; 2PL prevents
    // them via locks, so this check never fires there.
    if (mode_ == Mode::MVCC && wb != writes_.end()) {
        for (const auto& kv : wb->second) {
            auto h = history_.find(kv.first);
            if (h != history_.end() && !h->second.empty() &&
                h->second.back().commit_ts > t.snapshot) {
                writes_.erase(wb);
                releaseAll(t.id);
                cv_.notify_all();
                throw TxnConflict(t.id);
            }
        }
    }

    int64_t ts = ++commit_clock_;
    if (wb != writes_.end()) {
        for (const auto& kv : wb->second)
            history_[kv.first].push_back({ts, kv.second});
        writes_.erase(wb);
    }
    releaseAll(t.id);
    cv_.notify_all();
}

void TransactionManager::abort(Txn& t) {
    std::lock_guard<std::mutex> lk(mtx_);
    writes_.erase(t.id);
    releaseAll(t.id);
    cv_.notify_all();
}

} // namespace minidb
