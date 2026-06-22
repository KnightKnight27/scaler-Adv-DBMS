// ============================================================================
//  two_pl.hpp — Strict Two-Phase Locking manager (the BASELINE for Track B).
//
//  This is the concurrency control MVCC replaces. The contrast that makes the
//  extension worth doing lives in ONE rule difference:
//
//      Under 2PL a READ takes a SHARED lock.
//      A shared lock is incompatible with another txn's EXCLUSIVE lock.
//      => a reader BLOCKS while any writer holds the row.   (MVCC readers don't.)
//
//  Lock compatibility:
//        held \ want |  S      X
//        ------------+----------------
//             S      |  ok     wait
//             X      |  wait   wait
//
//  Strict 2PL: all locks are held until commit/abort (the "shrinking phase" is
//  a single point at end-of-transaction). That gives serializability and
//  recoverability. Deadlocks are caught with the same waits-for DFS as the MVCC
//  side. Single-threaded model: an un-grantable lock returns LockWait so the
//  benchmark can COUNT how often a reader was forced to wait.
// ============================================================================
#pragma once

#include "mvcc.hpp"   // reuse TxnResult / TxnState

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace minidb {

class TwoPLManager {
public:
    using txid = uint64_t;
    enum class Mode { S, X };

    txid begin() {
        txid id = ++next_id_;
        txns_[id] = Txn{id, TxnState::Active, {}, {}};
        return id;
    }

    // A read must hold a shared lock for its whole life (strict 2PL).
    TxnResult read(txid tx, const std::string& key, std::string& out) {
        Txn* t = lookup(tx);
        if (!t || t->state != TxnState::Active) return TxnResult::Aborted;
        TxnResult lk = acquire(*t, key, Mode::S);
        if (lk != TxnResult::Ok) return lk;          // blocked by a writer
        auto it = store_.find(key);
        if (it == store_.end()) return TxnResult::NotFound;
        out = it->second; return TxnResult::Ok;
    }

    TxnResult write(txid tx, const std::string& key, const std::string& value) {
        Txn* t = lookup(tx);
        if (!t || t->state != TxnState::Active) return TxnResult::Aborted;
        TxnResult lk = acquire(*t, key, Mode::X);
        if (lk != TxnResult::Ok) return lk;
        t->writes[key] = value;                      // buffered until commit
        return TxnResult::Ok;
    }

    TxnResult commit(txid tx) {
        Txn* t = lookup(tx);
        if (!t || t->state != TxnState::Active) return TxnResult::Aborted;
        for (auto& [k, v] : t->writes) store_[k] = v;  // install, then release
        release_all(*t);
        t->state = TxnState::Committed;
        return TxnResult::Ok;
    }

    void abort(txid tx) {
        Txn* t = lookup(tx);
        if (!t || t->state != TxnState::Active) return;
        release_all(*t); t->state = TxnState::Aborted;
    }

    TxnState state_of(txid tx) const {
        auto it = txns_.find(tx);
        return it == txns_.end() ? TxnState::Aborted : it->second.state;
    }

private:
    struct LockEntry { Mode mode; std::unordered_set<txid> holders; };
    struct Txn {
        txid id; TxnState state;
        std::unordered_map<std::string, Mode> held;   // key -> lock mode held
        std::unordered_map<std::string, std::string> writes;
    };

    txid next_id_ = 0;
    std::unordered_map<txid, Txn> txns_;
    std::unordered_map<std::string, std::string> store_;
    std::unordered_map<std::string, LockEntry> locks_;
    std::unordered_map<txid, txid> waits_for_;

    Txn* lookup(txid tx) { auto it = txns_.find(tx); return it == txns_.end() ? nullptr : &it->second; }

    TxnResult acquire(Txn& t, const std::string& key, Mode want) {
        auto it = locks_.find(key);
        if (it == locks_.end()) {                       // nobody holds it
            locks_[key] = {want, {t.id}}; t.held[key] = want; waits_for_.erase(t.id);
            return TxnResult::Ok;
        }
        LockEntry& e = it->second;
        bool only_me = e.holders.size() == 1 && e.holders.count(t.id);
        if (only_me) {                                   // upgrade / re-take
            if (want == Mode::X) e.mode = Mode::X;
            t.held[key] = e.mode; return TxnResult::Ok;
        }
        // compatible iff both shared and we're only adding another reader
        if (want == Mode::S && e.mode == Mode::S) {
            e.holders.insert(t.id); t.held[key] = Mode::S; waits_for_.erase(t.id);
            return TxnResult::Ok;
        }
        // otherwise we must wait on one of the current holders
        txid holder = *e.holders.begin();
        waits_for_[t.id] = holder;
        std::vector<txid> cycle;
        if (detect_cycle(t.id, cycle)) {                 // deadlock: youngest dies
            txid victim = *std::max_element(cycle.begin(), cycle.end());
            abort(victim);
            if (victim == t.id) return TxnResult::Aborted;
            return acquire(t, key, want);
        }
        return TxnResult::LockWait;
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

    void release_all(Txn& t) {
        for (auto& [key, _] : t.held) {
            auto it = locks_.find(key);
            if (it == locks_.end()) continue;
            it->second.holders.erase(t.id);
            if (it->second.holders.empty()) locks_.erase(it);
        }
        t.held.clear();
        for (auto it = waits_for_.begin(); it != waits_for_.end(); )
            it = (it->second == t.id) ? waits_for_.erase(it) : std::next(it);
        waits_for_.erase(t.id);
    }
};

}  // namespace minidb
