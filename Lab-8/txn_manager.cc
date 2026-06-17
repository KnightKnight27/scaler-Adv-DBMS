// txn_manager.cc — ADBMS Lab 8, 24BCS10115 Gauri Shukla
//
// Implementation of the MVCC + strict-2PL transaction manager.

#include "txn_manager.h"

#include <algorithm>

namespace mvcc {

const char* to_string(Status s) {
    switch (s) {
        case Status::Ok:                   return "OK";
        case Status::NotFound:             return "NOT_FOUND";
        case Status::LockWait:             return "LOCK_WAIT";
        case Status::Aborted:              return "ABORTED";
        case Status::SerializationFailure: return "SERIALIZATION_FAILURE";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TxId TxnManager::begin() {
    TxId id = next_id_++;
    Txn t;
    t.id       = id;
    t.snapshot = clock_;        // see every commit whose ts <= clock_ at this moment
    t.st       = TxState::Active;
    txns_.emplace(id, std::move(t));
    return id;
}

// ---------------------------------------------------------------------------
// MVCC visibility + read
// ---------------------------------------------------------------------------

const TxnManager::Version* TxnManager::visible(const std::string& key, TxId snapshot) const {
    auto it = store_.find(key);
    if (it == store_.end()) return nullptr;
    // Under snapshot isolation the [begin_ts, end_ts) intervals are disjoint, so
    // at most one version satisfies the predicate for a given snapshot.
    for (const Version& v : it->second) {
        bool born   = v.begin_ts <= snapshot;
        bool living = (v.end_ts == 0) || (v.end_ts > snapshot);
        if (born && living) return &v;
    }
    return nullptr;
}

Status TxnManager::read(TxId tx, const std::string& key, std::string& out) {
    Txn& t = txns_.at(tx);
    if (t.st != TxState::Active) return Status::Aborted;

    // Own uncommitted writes take priority (read-your-writes).
    auto w = t.writes.find(key);
    if (w != t.writes.end()) {
        if (w->second.deleted) return Status::NotFound;
        out = w->second.value;
        return Status::Ok;
    }

    const Version* v = visible(key, t.snapshot);
    if (!v || v->deleted) return Status::NotFound;
    out = v->value;
    return Status::Ok;
}

// ---------------------------------------------------------------------------
// Lock manager + deadlock detection
// ---------------------------------------------------------------------------

Status TxnManager::acquire_x(Txn& t, const std::string& key) {
    if (t.locks.count(key)) return Status::Ok;        // already ours

    auto held = xlock_.find(key);
    if (held == xlock_.end()) {                        // free → grant
        xlock_[key] = t.id;
        t.locks.insert(key);
        return Status::Ok;
    }

    TxId holder = held->second;
    if (holder == t.id) { t.locks.insert(key); return Status::Ok; }

    // Record the wait edge and walk the (functional) waits-for graph: each txn
    // waits for exactly one holder, so a cycle is just the chain looping back.
    waits_[t.id] = holder;

    std::vector<TxId> chain;
    std::unordered_set<TxId> seen;
    TxId cur = holder;
    bool cycle = false;
    while (true) {
        chain.push_back(cur);
        if (cur == t.id) { cycle = true; break; }
        if (seen.count(cur)) break;          // a cycle not involving t → still a wait
        seen.insert(cur);
        auto nxt = waits_.find(cur);
        if (nxt == waits_.end()) break;
        cur = nxt->second;
    }

    if (!cycle) return Status::LockWait;

    // Cycle through t: abort the youngest (largest id) participant.
    TxId victim = 0;
    for (TxId m : chain) victim = std::max(victim, m);
    last_victim_ = victim;
    abort_internal(victim);

    if (victim == t.id) return Status::Aborted;

    // The victim released its locks; try to grab the key now.
    waits_.erase(t.id);
    auto again = xlock_.find(key);
    if (again == xlock_.end()) {
        xlock_[key] = t.id;
        t.locks.insert(key);
        return Status::Ok;
    }
    waits_[t.id] = again->second;            // still held by a survivor → keep waiting
    return Status::LockWait;
}

void TxnManager::release_locks(Txn& t) {
    for (const std::string& k : t.locks) {
        auto x = xlock_.find(k);
        if (x != xlock_.end() && x->second == t.id) xlock_.erase(x);
    }
    t.locks.clear();
}

void TxnManager::drop_wait_edges(TxId id) {
    for (auto it = waits_.begin(); it != waits_.end(); ) {
        if (it->first == id || it->second == id) it = waits_.erase(it);
        else ++it;
    }
}

// ---------------------------------------------------------------------------
// Writes
// ---------------------------------------------------------------------------

Status TxnManager::write(TxId tx, const std::string& key, const std::string& value) {
    Txn& t = txns_.at(tx);
    if (t.st != TxState::Active) return Status::Aborted;

    Status ls = acquire_x(t, key);
    if (ls != Status::Ok) return ls;          // LockWait or Aborted (we were victim)

    t.writes[key] = Pending{value, false};
    return Status::Ok;
}

Status TxnManager::remove(TxId tx, const std::string& key) {
    Txn& t = txns_.at(tx);
    if (t.st != TxState::Active) return Status::Aborted;

    Status ls = acquire_x(t, key);
    if (ls != Status::Ok) return ls;

    // Only a tombstone over something currently visible counts as a delete.
    auto w = t.writes.find(key);
    bool exists = (w != t.writes.end()) ? !w->second.deleted
                                        : (visible(key, t.snapshot) != nullptr);
    if (!exists) return Status::NotFound;

    t.writes[key] = Pending{std::string(), true};
    return Status::Ok;
}

// ---------------------------------------------------------------------------
// Commit / abort
// ---------------------------------------------------------------------------

Status TxnManager::commit(TxId tx) {
    Txn& t = txns_.at(tx);
    if (t.st != TxState::Active) return Status::Aborted;

    // First-updater-wins: reject if any written key gained a version committed
    // after our snapshot (a concurrent writer beat us to it).
    for (const auto& kv : t.writes) {
        auto s = store_.find(kv.first);
        if (s == store_.end()) continue;
        for (const Version& v : s->second)
            if (v.begin_ts > t.snapshot) {
                abort_internal(tx);
                return Status::SerializationFailure;
            }
    }

    TxId cts = ++clock_;
    for (const auto& kv : t.writes) {
        std::vector<Version>& chain = store_[kv.first];
        for (Version& v : chain)
            if (v.end_ts == 0) v.end_ts = cts;       // close the previous live version
        Version nv;
        nv.value    = kv.second.value;
        nv.deleted  = kv.second.deleted;
        nv.begin_ts = cts;
        nv.end_ts   = 0;
        nv.creator  = tx;
        chain.push_back(std::move(nv));
    }

    release_locks(t);
    drop_wait_edges(tx);
    t.st = TxState::Committed;
    return Status::Ok;
}

void TxnManager::abort_internal(TxId id) {
    auto it = txns_.find(id);
    if (it == txns_.end()) return;
    Txn& t = it->second;
    if (t.st != TxState::Active) return;
    release_locks(t);
    drop_wait_edges(id);
    t.writes.clear();
    t.st = TxState::Aborted;
}

void TxnManager::abort(TxId tx) { abort_internal(tx); }

// ---------------------------------------------------------------------------
// Garbage collection (vacuum)
// ---------------------------------------------------------------------------

std::size_t TxnManager::gc() {
    // The oldest snapshot any active txn could read from; nothing that died
    // before it can ever be visible again.
    TxId oldest = clock_;
    for (const auto& kv : txns_)
        if (kv.second.st == TxState::Active) oldest = std::min(oldest, kv.second.snapshot);

    std::size_t pruned = 0;
    for (auto& kv : store_) {
        std::vector<Version>& chain = kv.second;
        auto dead = std::remove_if(chain.begin(), chain.end(), [&](const Version& v) {
            return v.end_ts != 0 && v.end_ts <= oldest;
        });
        pruned += static_cast<std::size_t>(std::distance(dead, chain.end()));
        chain.erase(dead, chain.end());
    }
    return pruned;
}

// ---------------------------------------------------------------------------
// Introspection
// ---------------------------------------------------------------------------

TxState TxnManager::state(TxId tx) const {
    auto it = txns_.find(tx);
    return it == txns_.end() ? TxState::Aborted : it->second.st;
}

std::size_t TxnManager::version_count() const {
    std::size_t n = 0;
    for (const auto& kv : store_) n += kv.second.size();
    return n;
}

}  // namespace mvcc
