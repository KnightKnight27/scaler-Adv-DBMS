// ============================================================================
//  mvcc.hpp — Multi-Version Concurrency Control transaction manager.
//
//  This is the Lab 8 transaction manager, brought into the engine as the
//  Track B (Concurrency) extension. The split is the PostgreSQL one:
//
//    READS  — against the snapshot captured at begin(). A reader NEVER takes a
//             lock and NEVER blocks. A version is visible to snapshot S iff
//                  xmin <= S  AND  (xmax == 0 OR xmax > S).
//    WRITES — strict 2PL on the written key: an exclusive lock taken on first
//             write and held until commit/abort.
//    DEADLOCK — a blocked write runs DFS over the waits-for graph; a cycle
//             aborts the youngest (highest-id) transaction in it.
//    COMMIT — first-updater-wins: reject (SerializationFailure) if a key we
//             wrote got a newer committed version while we ran.
//    gc()   — prune dead versions older than the oldest live snapshot.
//
//  Keys are strings; the engine uses "table/pk" so every row is an MVCC item.
//  Values are serialized row bytes. Single-threaded model: a blocked write
//  returns LockWait and the caller retries — keeps the concurrency scenarios
//  deterministic for the demo, exactly as in Lab 8.
// ============================================================================
#pragma once

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace minidb {

enum class TxnResult { Ok, NotFound, LockWait, Aborted, SerializationFailure };
enum class TxnState  { Active, Committed, Aborted };

inline const char* to_string(TxnResult r) {
    switch (r) {
        case TxnResult::Ok: return "OK";
        case TxnResult::NotFound: return "NOT_FOUND";
        case TxnResult::LockWait: return "LOCK_WAIT";
        case TxnResult::Aborted: return "ABORTED";
        case TxnResult::SerializationFailure: return "SERIALIZATION_FAILURE";
    }
    return "?";
}

class MVCCManager {
public:
    using txid = uint64_t;
    using ts_t = uint64_t;

    // Start a transaction; its snapshot freezes everything committed so far.
    txid begin() {
        txid id = ++next_id_;
        Txn& t = txns_[id];
        t.id = id; t.snapshot = clock_; t.state = TxnState::Active;
        return id;
    }

    // MVCC read: own uncommitted write first, else the snapshot-visible version.
    TxnResult read(txid tx, const std::string& key, std::string& out) {
        Txn* t = lookup(tx);
        if (!t || t->state != TxnState::Active) return TxnResult::Aborted;
        if (auto it = t->buffer.find(key); it != t->buffer.end()) {
            if (it->second.tombstone) return TxnResult::NotFound;
            out = it->second.value; return TxnResult::Ok;
        }
        const Version* v = visible_to(key, t->snapshot);
        if (!v || v->tombstone) return TxnResult::NotFound;
        out = v->value; return TxnResult::Ok;
    }

    TxnResult write(txid tx, const std::string& key, const std::string& value) {
        return write_impl(tx, key, value, false);
    }
    TxnResult remove(txid tx, const std::string& key) {
        return write_impl(tx, key, "", true);
    }

    TxnResult commit(txid tx) {
        Txn* t = lookup(tx);
        if (!t || t->state != TxnState::Active) return TxnResult::Aborted;
        // first-updater-wins
        for (const auto& [key, _] : t->buffer) {
            auto it = store_.find(key);
            if (it == store_.end() || it->second.empty()) continue;
            if (it->second.back().xmin > t->snapshot) {
                abort_internal(t->id);
                return TxnResult::SerializationFailure;
            }
        }
        ts_t commit_ts = ++clock_;
        for (auto& [key, pending] : t->buffer) {
            auto& chain = store_[key];
            if (!chain.empty()) chain.back().xmax = commit_ts;   // supersede head
            chain.push_back({pending.value, pending.tombstone, commit_ts, 0, t->id});
        }
        release_all_locks(*t);
        t->state = TxnState::Committed;
        return TxnResult::Ok;
    }

    void abort(txid tx) { abort_internal(tx); }

    size_t gc() {
        ts_t low = oldest_snapshot();
        size_t pruned = 0;
        for (auto& [key, chain] : store_) {
            std::vector<Version> kept; kept.reserve(chain.size());
            for (Version& v : chain) {
                if (v.xmax != 0 && v.xmax <= low) { ++pruned; continue; }
                kept.push_back(std::move(v));
            }
            chain.swap(kept);
        }
        return pruned;
    }

    // --- introspection (used by tests / demo) -------------------------------
    TxnState state_of(txid tx) const {
        auto it = txns_.find(tx);
        return it == txns_.end() ? TxnState::Aborted : it->second.state;
    }
    txid   last_victim() const { return last_victim_; }
    size_t version_count() const {
        size_t n = 0; for (auto& [_, c] : store_) n += c.size(); return n;
    }
    std::string dump_chain(const std::string& key) const {
        auto it = store_.find(key);
        if (it == store_.end() || it->second.empty()) return "(no versions)";
        std::ostringstream os;
        for (const Version& v : it->second)
            os << "[xmin=" << v.xmin << " xmax=" << v.xmax << " "
               << (v.tombstone ? "<del>" : v.value) << "] ";
        return os.str();
    }

private:
    struct Version { std::string value; bool tombstone; ts_t xmin; ts_t xmax; txid creator; };
    struct Pending { std::string value; bool tombstone = false; };
    struct Txn {
        txid id = 0; ts_t snapshot = 0; TxnState state = TxnState::Active;
        std::unordered_map<std::string, Pending> buffer;
        std::unordered_set<std::string> locks;
    };

    txid next_id_ = 0, last_victim_ = 0;
    ts_t clock_ = 0;
    std::unordered_map<txid, Txn> txns_;
    std::unordered_map<std::string, std::vector<Version>> store_;
    std::unordered_map<std::string, txid> xlock_;        // key -> holder
    std::unordered_map<txid, txid> waits_for_;            // waiter -> holder

    Txn* lookup(txid tx) { auto it = txns_.find(tx); return it == txns_.end() ? nullptr : &it->second; }

    const Version* visible_to(const std::string& key, ts_t snap) const {
        auto it = store_.find(key);
        if (it == store_.end()) return nullptr;
        for (auto r = it->second.rbegin(); r != it->second.rend(); ++r)
            if (r->xmin <= snap && (r->xmax == 0 || r->xmax > snap)) return &*r;
        return nullptr;
    }

    TxnResult try_acquire(Txn& t, const std::string& key) {
        if (auto it = xlock_.find(key); it != xlock_.end()) {
            txid holder = it->second;
            if (holder == t.id) return TxnResult::Ok;            // reentrant
            waits_for_[t.id] = holder;
            std::vector<txid> cycle;
            if (detect_cycle(t.id, cycle)) {
                txid victim = *std::max_element(cycle.begin(), cycle.end());
                last_victim_ = victim;
                abort_internal(victim);
                if (victim == t.id) return TxnResult::Aborted;
                return try_acquire(t, key);
            }
            return TxnResult::LockWait;
        }
        xlock_[key] = t.id; t.locks.insert(key); waits_for_.erase(t.id);
        return TxnResult::Ok;
    }

    bool detect_cycle(txid start, std::vector<txid>& cycle) const {
        std::vector<txid> path = {start}; txid cur = start;
        while (true) {
            auto it = waits_for_.find(cur);
            if (it == waits_for_.end()) return false;
            txid nxt = it->second;
            if (nxt == start) { cycle = path; return true; }
            if (std::find(path.begin(), path.end(), nxt) != path.end()) return false;
            path.push_back(nxt); cur = nxt;
        }
    }

    TxnResult write_impl(txid tx, const std::string& key, const std::string& value, bool tomb) {
        Txn* t = lookup(tx);
        if (!t || t->state != TxnState::Active) return TxnResult::Aborted;
        TxnResult lk = try_acquire(*t, key);
        if (lk != TxnResult::Ok) return lk;
        Pending& p = t->buffer[key]; p.value = value; p.tombstone = tomb;
        return TxnResult::Ok;
    }

    void release_all_locks(Txn& t) {
        for (const std::string& k : t.locks) xlock_.erase(k);
        t.locks.clear();
        for (auto it = waits_for_.begin(); it != waits_for_.end(); )
            it = (it->second == t.id) ? waits_for_.erase(it) : std::next(it);
        waits_for_.erase(t.id);
    }

    void abort_internal(txid tx) {
        Txn* t = lookup(tx);
        if (!t || t->state != TxnState::Active) return;
        t->buffer.clear(); release_all_locks(*t); t->state = TxnState::Aborted;
    }

    ts_t oldest_snapshot() const {
        ts_t low = clock_; bool any = false;
        for (auto& [_, t] : txns_)
            if (t.state == TxnState::Active && (!any || t.snapshot < low)) { low = t.snapshot; any = true; }
        return low;
    }
};

}  // namespace minidb
