#include "transaction.h"
#include <chrono>

namespace minidb {

// ── LockManager ───────────────────────────────────────────────────────────────
// Strict 2PL: transactions acquire locks in the growing phase and release
// ALL locks together at commit/abort (the "strict" part prevents cascading aborts).

static constexpr int LOCK_TIMEOUT_MS = 150;   // wait this long before declaring deadlock

void LockManager::acquire(Txn* t, const std::string& key, bool exclusive) {
    std::unique_lock<std::mutex> lk(mu_);

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(LOCK_TIMEOUT_MS);

    // Wait until the lock is compatible or we time out.
    bool acquired = cv_.wait_until(lk, deadline, [&] {
        LockEntry& e = table_[key];
        if (exclusive) {
            // X lock: must have no other holders at all.
            bool no_shared  = e.shared_holders.empty() ||
                              (e.shared_holders.size() == 1 &&
                               e.shared_holders.count(t->id));
            bool no_excl    = e.excl_holder == -1 || e.excl_holder == t->id;
            return no_shared && no_excl;
        } else {
            // S lock: only blocked if someone else holds an X lock.
            return e.excl_holder == -1 || e.excl_holder == t->id;
        }
    });

    if (!acquired) {
        // Timed out waiting → treat as deadlock and abort this transaction.
        deadlock_count++;
        throw DeadlockError();
    }

    // Grant the lock.
    LockEntry& e = table_[key];
    if (exclusive) {
        e.excl_holder = t->id;
    } else {
        e.shared_holders.insert(t->id);
    }

    // Record that this transaction holds this key so release_all can find it.
    t->locks.push_back(key);
}

void LockManager::release_all(Txn* t) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (const std::string& key : t->locks) {
            auto it = table_.find(key);
            if (it == table_.end()) continue;
            LockEntry& e = it->second;
            e.shared_holders.erase(t->id);
            if (e.excl_holder == t->id) e.excl_holder = -1;
            // Clean up empty entries to avoid unbounded growth.
            if (e.shared_holders.empty() && e.excl_holder == -1)
                table_.erase(it);
        }
        t->locks.clear();
    }
    // Wake all waiters — some may now be able to acquire their lock.
    cv_.notify_all();
}

// ── MvccStore ─────────────────────────────────────────────────────────────────
// Snapshot isolation: each transaction sees the database as it was when it began.
// Readers never block; writers abort on write-write conflict (first-committer-wins).

MvccTxn MvccStore::begin_txn() {
    // Read timestamp = current logical clock.  All versions committed before
    // this timestamp are visible to this transaction.
    return MvccTxn(clock_.load());
}

bool MvccStore::get(MvccTxn& t, long key, Value& out) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = chains_.find(key);
    if (it == chains_.end()) return false;

    // Find the newest version that was committed before our snapshot.
    for (const Version& v : it->second) {
        if (v.commit_ts <= t.read_ts_) {
            if (v.deleted) return false;
            out = v.val;
            return true;
        }
    }
    return false;  // all versions are newer than our snapshot
}

void MvccStore::put(MvccTxn& t, long key, const Value& v) {
    t.writes_[key] = {false, v};
}

void MvccStore::erase(MvccTxn& t, long key) {
    t.writes_[key] = {true, Value::make_int(0)};
}

bool MvccStore::commit(MvccTxn& t) {
    std::lock_guard<std::mutex> lk(mu_);

    // Conflict check: for each key we want to write, is there a version
    // committed AFTER our snapshot?  If so, another transaction beat us
    // (first-committer-wins → we abort).
    for (auto& [key, _] : t.writes_) {
        auto it = chains_.find(key);
        if (it != chains_.end() && !it->second.empty()) {
            if (it->second.front().commit_ts > t.read_ts_) {
                conflict_count++;
                return false;   // conflict → caller must abort
            }
        }
    }

    // No conflict: assign a commit timestamp and install all versions.
    long commit_ts = clock_.fetch_add(1) + 1;
    for (auto& [key, dv] : t.writes_) {
        Version ver;
        ver.commit_ts = commit_ts;
        ver.deleted   = dv.first;
        ver.val       = dv.second;
        // Prepend so the chain is always newest-first.
        chains_[key].insert(chains_[key].begin(), ver);
    }
    return true;
}

} // namespace minidb
