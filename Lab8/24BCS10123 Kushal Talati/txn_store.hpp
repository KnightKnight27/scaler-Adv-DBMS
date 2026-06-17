// Lab 8 — in-memory transaction store: MVCC reads + strict 2PL writes
// 24BCS10123  Kushal Talati
//
// kt::TxnStore is a header-only key/value store that puts together the
// concurrency-control core of a relational engine:
//
//   * MVCC snapshot reads  — every transaction reads against the snapshot it
//     captured at start(); reads take no locks and never block.
//   * Strict two-phase locking for writes — a writer takes a per-key exclusive
//     lock and holds all of them until commit/rollback.
//   * Deadlock detection  — a waits-for graph is walked on every blocked write;
//     a cycle aborts the youngest transaction in it.
//   * First-committer-wins — at commit a writer is rejected if a concurrent
//     transaction already committed a newer version of a key it wrote.
//   * vacuum()  — drops dead revisions no live or future snapshot can reach.
//
// This is the PostgreSQL split: reads go through MVCC visibility, writes go
// through row locks. It is a single-threaded, deterministic simulator — a
// blocked write returns Outcome::Blocked and the caller retries once the holder
// releases, which keeps the deadlock schedules reproducible. Header-only, the
// same shape as my Lab-6 / Lab-7 submissions.

#ifndef KT_LAB8_TXN_STORE_HPP
#define KT_LAB8_TXN_STORE_HPP

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kt {

using TxnId = unsigned long long;

enum class Outcome { Ok, Missing, Blocked, RolledBack, Conflict };

inline const char* name_of(Outcome o) {
    switch (o) {
        case Outcome::Ok:         return "OK";
        case Outcome::Missing:    return "MISSING";
        case Outcome::Blocked:    return "BLOCKED";
        case Outcome::RolledBack: return "ROLLED_BACK";
        case Outcome::Conflict:   return "CONFLICT";
    }
    return "?";
}

enum class Phase { Active, Committed, Aborted };

class TxnStore {
public:
    TxnStore() = default;

    // Open a transaction; it captures the current commit clock as its snapshot.
    TxnId start();

    // MVCC read against this txn's snapshot (its own buffered writes win first).
    Outcome get(TxnId tx, const std::string& key, std::string& out);

    // 2PL writes — they acquire the key's exclusive lock before buffering.
    //   Ok          buffered into the write-set,
    //   Blocked     key locked by another live txn, no cycle (retry later),
    //   RolledBack  this txn was picked as the deadlock victim.
    Outcome put(TxnId tx, const std::string& key, const std::string& value);
    Outcome erase(TxnId tx, const std::string& key);

    // Ok, Conflict (first-committer-wins), or RolledBack (already dead).
    Outcome commit(TxnId tx);
    void    rollback(TxnId tx);

    std::size_t vacuum();   // drop dead revisions; returns how many were dropped

    // ---- introspection for the driver ---------------------------------
    Phase  phase(TxnId tx) const;
    TxnId  last_victim() const { return victim_; }
    std::size_t revision_count() const;

private:
    // One historical value of a key. Committed revisions of a key occupy
    // disjoint [born, retired) clock intervals.
    struct Revision {
        std::string value;
        bool        tombstone = false;
        TxnId       born      = 0;   // commit clock of the creator
        TxnId       retired   = 0;   // commit clock that superseded it (0 = live)
        TxnId       author    = 0;
    };

    struct Buffered { std::string value; bool tombstone; };

    struct Txn {
        TxnId snapshot = 0;
        Phase phase    = Phase::Active;
        std::unordered_map<std::string, Buffered> writes;
        std::unordered_set<std::string>           held;   // locks this txn holds
    };

    TxnId clock_    = 0;   // global commit clock; each commit does ++clock_
    TxnId next_     = 1;   // next transaction id to hand out
    TxnId victim_   = 0;   // last deadlock victim, for the driver

    std::unordered_map<TxnId, Txn>                        txns_;
    std::unordered_map<std::string, std::vector<Revision>> chains_;  // key -> history
    std::unordered_map<std::string, TxnId>                owner_;    // key -> lock holder
    std::unordered_map<TxnId, TxnId>                      waiting_;  // waiter -> holder

    const Revision* visible_to(const std::string& key, TxnId snapshot) const;
    Outcome lock_key(Txn& t, TxnId id, const std::string& key);
    void    free_locks(Txn& t, TxnId id);
    void    forget_waits(TxnId id);
    void    kill(TxnId id);
};

// ===========================================================================
// Lifecycle
// ===========================================================================

inline TxnId TxnStore::start() {
    const TxnId id = next_++;
    Txn t;
    t.snapshot = clock_;        // visible: every commit with clock <= clock_ now
    t.phase    = Phase::Active;
    txns_.emplace(id, std::move(t));
    return id;
}

// ===========================================================================
// MVCC visibility + read
// ===========================================================================

inline const TxnStore::Revision*
TxnStore::visible_to(const std::string& key, TxnId snapshot) const {
    const auto it = chains_.find(key);
    if (it == chains_.end()) return nullptr;
    for (const Revision& r : it->second) {
        const bool already_born = r.born <= snapshot;
        const bool not_retired   = (r.retired == 0) || (r.retired > snapshot);
        if (already_born && not_retired) return &r;   // at most one qualifies
    }
    return nullptr;
}

inline Outcome TxnStore::get(TxnId tx, const std::string& key, std::string& out) {
    Txn& t = txns_.at(tx);
    if (t.phase != Phase::Active) return Outcome::RolledBack;

    // Read-your-own-writes: the buffered write-set takes precedence.
    const auto w = t.writes.find(key);
    if (w != t.writes.end()) {
        if (w->second.tombstone) return Outcome::Missing;
        out = w->second.value;
        return Outcome::Ok;
    }

    const Revision* r = visible_to(key, t.snapshot);
    if (!r || r->tombstone) return Outcome::Missing;
    out = r->value;
    return Outcome::Ok;
}

// ===========================================================================
// Lock manager + deadlock detection
// ===========================================================================

inline Outcome TxnStore::lock_key(Txn& t, TxnId id, const std::string& key) {
    if (t.held.count(key)) return Outcome::Ok;          // re-entrant: already ours

    const auto held = owner_.find(key);
    if (held == owner_.end()) {                          // unlocked -> grant
        owner_[key] = id;
        t.held.insert(key);
        return Outcome::Ok;
    }
    if (held->second == id) { t.held.insert(key); return Outcome::Ok; }

    // Record the wait and follow the waits-for chain. Each waiter waits on
    // exactly one holder, so the graph is functional: a cycle is the chain
    // looping back to the requester.
    const TxnId holder = held->second;
    waiting_[id] = holder;

    std::vector<TxnId> chain;
    std::unordered_set<TxnId> seen;
    TxnId cur = holder;
    bool cyclic = false;
    for (;;) {
        chain.push_back(cur);
        if (cur == id) { cyclic = true; break; }
        if (seen.count(cur)) break;        // a cycle that excludes us -> plain wait
        seen.insert(cur);
        const auto nxt = waiting_.find(cur);
        if (nxt == waiting_.end()) break;
        cur = nxt->second;
    }

    if (!cyclic) return Outcome::Blocked;

    // Break the cycle by aborting its youngest member (largest id).
    TxnId loser = 0;
    for (TxnId m : chain) loser = std::max(loser, m);
    victim_ = loser;
    kill(loser);

    if (loser == id) return Outcome::RolledBack;

    // The victim freed its locks; try to take the key right away.
    waiting_.erase(id);
    const auto again = owner_.find(key);
    if (again == owner_.end()) {
        owner_[key] = id;
        t.held.insert(key);
        return Outcome::Ok;
    }
    waiting_[id] = again->second;          // still held by a survivor -> keep waiting
    return Outcome::Blocked;
}

inline void TxnStore::free_locks(Txn& t, TxnId id) {
    for (const std::string& k : t.held) {
        const auto o = owner_.find(k);
        if (o != owner_.end() && o->second == id) owner_.erase(o);
    }
    t.held.clear();
}

inline void TxnStore::forget_waits(TxnId id) {
    for (auto it = waiting_.begin(); it != waiting_.end(); ) {
        if (it->first == id || it->second == id) it = waiting_.erase(it);
        else ++it;
    }
}

// ===========================================================================
// Writes
// ===========================================================================

inline Outcome TxnStore::put(TxnId tx, const std::string& key, const std::string& value) {
    Txn& t = txns_.at(tx);
    if (t.phase != Phase::Active) return Outcome::RolledBack;

    const Outcome lk = lock_key(t, tx, key);
    if (lk != Outcome::Ok) return lk;          // Blocked or RolledBack (we lost)

    t.writes[key] = Buffered{value, false};
    return Outcome::Ok;
}

inline Outcome TxnStore::erase(TxnId tx, const std::string& key) {
    Txn& t = txns_.at(tx);
    if (t.phase != Phase::Active) return Outcome::RolledBack;

    const Outcome lk = lock_key(t, tx, key);
    if (lk != Outcome::Ok) return lk;

    // A tombstone only counts if something is currently visible to delete.
    const auto w = t.writes.find(key);
    const bool present = (w != t.writes.end()) ? !w->second.tombstone
                                               : (visible_to(key, t.snapshot) != nullptr);
    if (!present) return Outcome::Missing;

    t.writes[key] = Buffered{std::string(), true};
    return Outcome::Ok;
}

// ===========================================================================
// Commit / rollback
// ===========================================================================

inline Outcome TxnStore::commit(TxnId tx) {
    Txn& t = txns_.at(tx);
    if (t.phase != Phase::Active) return Outcome::RolledBack;

    // First-committer-wins: bail if any written key already gained a revision
    // committed after our snapshot (a concurrent writer got there first).
    for (const auto& kv : t.writes) {
        const auto c = chains_.find(kv.first);
        if (c == chains_.end()) continue;
        for (const Revision& r : c->second)
            if (r.born > t.snapshot) {
                kill(tx);
                return Outcome::Conflict;
            }
    }

    const TxnId stamp = ++clock_;
    for (const auto& kv : t.writes) {
        std::vector<Revision>& chain = chains_[kv.first];
        for (Revision& r : chain)
            if (r.retired == 0) r.retired = stamp;   // close the prior live revision
        Revision nr;
        nr.value     = kv.second.value;
        nr.tombstone = kv.second.tombstone;
        nr.born      = stamp;
        nr.retired   = 0;
        nr.author    = tx;
        chain.push_back(std::move(nr));
    }

    free_locks(t, tx);
    forget_waits(tx);
    t.phase = Phase::Committed;
    return Outcome::Ok;
}

inline void TxnStore::kill(TxnId id) {
    const auto it = txns_.find(id);
    if (it == txns_.end()) return;
    Txn& t = it->second;
    if (t.phase != Phase::Active) return;
    free_locks(t, id);
    forget_waits(id);
    t.writes.clear();
    t.phase = Phase::Aborted;
}

inline void TxnStore::rollback(TxnId tx) { kill(tx); }

// ===========================================================================
// Vacuum
// ===========================================================================

inline std::size_t TxnStore::vacuum() {
    // The oldest snapshot any active txn can still read against; nothing retired
    // at or before it can ever be seen again.
    TxnId floor = clock_;
    for (const auto& kv : txns_)
        if (kv.second.phase == Phase::Active) floor = std::min(floor, kv.second.snapshot);

    std::size_t dropped = 0;
    for (auto& kv : chains_) {
        std::vector<Revision>& chain = kv.second;
        const auto dead = std::remove_if(chain.begin(), chain.end(), [&](const Revision& r) {
            return r.retired != 0 && r.retired <= floor;
        });
        dropped += static_cast<std::size_t>(std::distance(dead, chain.end()));
        chain.erase(dead, chain.end());
    }
    return dropped;
}

// ===========================================================================
// Introspection
// ===========================================================================

inline Phase TxnStore::phase(TxnId tx) const {
    const auto it = txns_.find(tx);
    return it == txns_.end() ? Phase::Aborted : it->second.phase;
}

inline std::size_t TxnStore::revision_count() const {
    std::size_t n = 0;
    for (const auto& kv : chains_) n += kv.second.size();
    return n;
}

}  // namespace kt

#endif  // KT_LAB8_TXN_STORE_HPP
