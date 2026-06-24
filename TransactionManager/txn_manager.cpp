#include "txn_manager.h"

int TxnManager::begin() {
    Ts ts = ++clock_;
    int id = static_cast<int>(ts);     // id == snapshot timestamp
    txns_[id] = Txn{id, ts, true, {}};
    return id;
}

TxnManager::Txn& TxnManager::get(int id) { return txns_.at(id); }

std::optional<long long> TxnManager::read(int txn, const std::string& key) {
    Txn& t = get(txn);
    const Version* v = store_.visible(key, txn, t.begin_ts);
    if (!v || v->tombstone) return std::nullopt;   // no version, or it's a delete
    return v->value;
}

LockResult TxnManager::do_write(int txn, const std::string& key,
                                long long value, bool tombstone) {
    LockResult r = locks_.acquire(txn, key, LockMode::X);
    if (r == LockResult::Granted) {
        Version* v = store_.prepend(key, value, tombstone, txn);
        get(txn).writes.emplace_back(key, v);
    } else if (r == LockResult::Deadlock) {
        abort(txn);                                 // requester is the victim
    }
    // Blocked: nothing created; the caller retries after the blocker finishes.
    return r;
}

LockResult TxnManager::write(int txn, const std::string& key, long long value) {
    return do_write(txn, key, value, /*tombstone=*/false);
}

LockResult TxnManager::remove(int txn, const std::string& key) {
    return do_write(txn, key, /*value=*/0, /*tombstone=*/true);
}

void TxnManager::commit(int txn) {
    Txn& t = get(txn);
    Ts commit_ts = ++clock_;
    for (auto& [key, v] : t.writes) {
        v->begin_ts = commit_ts;                    // version becomes visible
        if (v->prev) v->prev->end_ts = commit_ts;   // older version is superseded
    }
    locks_.release_all(txn);
    t.active = false;
}

void TxnManager::abort(int txn) {
    Txn& t = get(txn);
    // Pop created versions newest-first; each is still the head of its chain
    // because we hold its X lock.
    for (auto it = t.writes.rbegin(); it != t.writes.rend(); ++it)
        store_.pop_head(it->first, it->second);
    t.writes.clear();
    locks_.release_all(txn);
    t.active = false;
}
