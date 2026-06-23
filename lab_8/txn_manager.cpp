// txn_manager.cpp
#include "txn_manager.h"

#include <stdexcept>

namespace lab8 {

TxnId TxnManager::begin() {
    TxnId id = ++clock_;          // consume one tick as the txn id
    Txn t;
    t.id       = id;
    t.begin_ts = clock_;          // snapshot watermark = current clock value
    t.snapshot = t.begin_ts;
    t.state    = TxnState::Active;
    txns_[id]  = std::move(t);
    return id;
}

// Visibility rule (snapshot isolation):
//   Walk the chain newest-first. A version V is VISIBLE to txn t iff:
//     (a) V was created by t itself (t sees its own pending writes), OR
//     (b) V is committed (begin_ts != INF) AND committed at-or-before t's
//         snapshot (V.begin_ts <= t.begin_ts) AND not yet superseded as of the
//         snapshot (V.end_ts > t.begin_ts).
//   The first version satisfying this is the one t reads. A tombstone that is
//   visible means the key is logically absent for t.
VersionPtr TxnManager::visible_version(const Txn& t,
                                       const std::string& key) const {
    for (VersionPtr v = store_.head(key); v; v = v->prev) {
        // (a) own pending write is always visible to its creator.
        if (v->creator == t.id) return v;

        // skip versions created by OTHER, still-uncommitted txns.
        if (v->begin_ts == INF) continue;

        // (b) committed & within snapshot window.
        if (v->begin_ts <= t.begin_ts && v->end_ts > t.begin_ts) return v;
    }
    return nullptr;
}

ReadResult TxnManager::read(TxnId txn, const std::string& key) {
    Txn& t = txns_.at(txn);
    if (t.state != TxnState::Active)
        return {OpStatus::Aborted, std::nullopt, false};

    VersionPtr v = visible_version(t, key);
    if (!v || v->tombstone)
        return {OpStatus::Ok, std::nullopt, false};   // key absent for snapshot
    return {OpStatus::Ok, v->value, true};
}

OpStatus TxnManager::do_write(TxnId txn, const std::string& key, Value v,
                              bool tombstone) {
    Txn& t = txns_.at(txn);
    if (t.state != TxnState::Active) return OpStatus::Aborted;

    // Acquire exclusive lock under Strict 2PL (held until commit/abort).
    LockReply rep = lm_.acquire(txn, key, LockMode::X);
    if (rep.result == LockResult::Blocked)
        return OpStatus::Blocked;                       // benign wait
    if (rep.result == LockResult::Deadlock) {
        // We were chosen as the victim -> roll ourselves back.
        abort(txn);
        return OpStatus::Aborted;
    }

    // Lock granted. If we already created a version for this key in THIS txn,
    // update it in place (still one logical version for our pending write).
    auto it = t.my_versions.find(key);
    if (it != t.my_versions.end()) {
        it->second->value     = v;
        it->second->tombstone = tombstone;
        return OpStatus::Ok;
    }

    VersionPtr node   = store_.prepend(key, v, tombstone, txn);
    t.my_versions[key] = node;
    t.written_keys.push_back(key);
    return OpStatus::Ok;
}

OpStatus TxnManager::write(TxnId txn, const std::string& key, Value v) {
    return do_write(txn, key, v, /*tombstone=*/false);
}

OpStatus TxnManager::remove(TxnId txn, const std::string& key) {
    return do_write(txn, key, Value{}, /*tombstone=*/true);
}

void TxnManager::commit(TxnId txn) {
    Txn& t = txns_.at(txn);
    if (t.state != TxnState::Active) return;

    Ts cts = ++clock_;            // commit timestamp
    t.commit_ts = cts;

    // Stamp each of our pending versions as committed and close the version it
    // superseded (set the older live version's end_ts to our commit-ts).
    for (const auto& key : t.written_keys) {
        VersionPtr node = t.my_versions[key];
        node->begin_ts  = cts;
        if (node->prev && node->prev->end_ts == INF)
            node->prev->end_ts = cts;
    }

    t.state = TxnState::Committed;
    lm_.release_all(txn);          // Strict 2PL: release ALL locks at commit
}

void TxnManager::abort(TxnId txn) {
    Txn& t = txns_.at(txn);
    if (t.state != TxnState::Active) return;

    // Roll back: discard every uncommitted version we prepended. Because writes
    // prepend to the head and a txn holds the X lock on each key it wrote, our
    // version is guaranteed to still be the head of that key's chain.
    for (auto it = t.written_keys.rbegin(); it != t.written_keys.rend(); ++it)
        store_.pop_head(*it);

    t.my_versions.clear();
    t.written_keys.clear();
    t.state = TxnState::Aborted;
    lm_.release_all(txn);          // release locks, wake waiters, fix graph
}

TxnState TxnManager::state(TxnId txn) const {
    auto it = txns_.find(txn);
    if (it == txns_.end()) throw std::out_of_range("unknown txn");
    return it->second.state;
}

}  // namespace lab8
