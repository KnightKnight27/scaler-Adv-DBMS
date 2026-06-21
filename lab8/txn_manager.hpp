// txn_manager.hpp — Lab 8 (Aman Yadav, 24BCS10183, Class B, 2nd Year)
//
// Header-only in-memory transaction manager:
//   * MVCC reads  — every txn reads from the snapshot it captured at begin()
//   * Strict 2PL  — writers take exclusive locks held until commit/abort
//   * Waits-for deadlock detection via DFS; youngest txn is the victim
//   * First-updater-wins serialization check at commit
//   * Vacuum (gc) prunes dead versions older than the oldest live snapshot

#pragma once

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace adbms::txn {

using txn_id_t = std::uint64_t;
using ts_t     = std::uint64_t;

enum class State  { Active, Committed, Aborted };
enum class Result { Ok, NotFound, LockWait, Aborted, SerializationFailure };

struct Version {
    std::string value;
    bool        tombstone{false};
    ts_t        xmin{0};
    ts_t        xmax{0};
    txn_id_t    creator{0};
};

struct TxnInfo {
    ts_t                        snapshot{0};
    State                       state{State::Active};
    std::unordered_map<std::string, Version> buffer;   // pending writes
    std::unordered_set<std::string>          locks;    // X-locks held
};

class Manager {
public:
    txn_id_t begin() {
        ++clock_;
        txn_id_t id = ++next_txn_;
        TxnInfo info;
        info.snapshot = clock_;
        txns_[id] = std::move(info);
        return id;
    }

    Result read(txn_id_t tx, const std::string& key, std::string& out) {
        auto it = txns_.find(tx);
        if (it == txns_.end() || it->second.state != State::Active) return Result::Aborted;

        // a txn sees its own pending writes first
        auto pend = it->second.buffer.find(key);
        if (pend != it->second.buffer.end()) {
            if (pend->second.tombstone) return Result::NotFound;
            out = pend->second.value;
            return Result::Ok;
        }

        ts_t snap = it->second.snapshot;
        auto chain = chains_.find(key);
        if (chain == chains_.end()) return Result::NotFound;
        for (auto rit = chain->second.rbegin(); rit != chain->second.rend(); ++rit) {
            const Version& v = *rit;
            if (v.xmin <= snap && (v.xmax == 0 || v.xmax > snap)) {
                if (v.tombstone) return Result::NotFound;
                out = v.value;
                return Result::Ok;
            }
        }
        return Result::NotFound;
    }

    Result write(txn_id_t tx, const std::string& key, const std::string& value) {
        return write_impl(tx, key, value, /*tombstone=*/false);
    }

    Result remove(txn_id_t tx, const std::string& key) {
        return write_impl(tx, key, /*value=*/"", /*tombstone=*/true);
    }

    Result commit(txn_id_t tx) {
        auto it = txns_.find(tx);
        if (it == txns_.end()) return Result::Aborted;
        TxnInfo& info = it->second;
        if (info.state != State::Active) return Result::Aborted;

        // First-updater-wins: bail if any touched key has a newer committed version.
        for (auto& [key, _] : info.buffer) {
            auto chain = chains_.find(key);
            if (chain == chains_.end()) continue;
            const Version& latest = chain->second.back();
            if (latest.xmin > info.snapshot) {
                abort_internal(tx);
                return Result::SerializationFailure;
            }
        }

        ++clock_;
        ts_t commit_ts = clock_;
        for (auto& [key, pending] : info.buffer) {
            auto& chain = chains_[key];
            if (!chain.empty() && chain.back().xmax == 0) chain.back().xmax = commit_ts;
            pending.xmin = commit_ts;
            pending.xmax = 0;
            pending.creator = tx;
            chain.push_back(pending);
        }
        release_locks(info);
        info.state = State::Committed;
        return Result::Ok;
    }

    void abort(txn_id_t tx) { abort_internal(tx); }

    void gc() {
        ts_t floor = oldest_snapshot();
        for (auto& [_, chain] : chains_) {
            std::vector<Version> kept;
            kept.reserve(chain.size());
            for (const Version& v : chain) {
                bool dead   = (v.xmax != 0 && v.xmax < floor);
                bool latest = (&v == &chain.back());
                if (!dead || latest) kept.push_back(v);
            }
            chain.swap(kept);
        }
    }

    // ---- introspection -----------------------------------------------------
    State    state_of(txn_id_t tx) const {
        auto it = txns_.find(tx);
        return it == txns_.end() ? State::Aborted : it->second.state;
    }
    std::optional<txn_id_t> last_victim() const { return last_victim_; }
    std::size_t live_txn_count() const {
        std::size_t n = 0;
        for (auto& [_, t] : txns_) if (t.state == State::Active) ++n;
        return n;
    }
    std::size_t lock_count() const { return xlock_.size(); }
    std::size_t version_count() const {
        std::size_t n = 0;
        for (auto& [_, c] : chains_) n += c.size();
        return n;
    }

    std::string check_invariants() const {
        for (auto& [key, holder] : xlock_) {
            auto it = txns_.find(holder);
            if (it == txns_.end() || it->second.state != State::Active)
                return "lock on '" + key + "' held by non-active txn";
        }
        for (auto& [key, chain] : chains_) {
            for (std::size_t i = 1; i < chain.size(); ++i) {
                if (chain[i].xmin <= chain[i - 1].xmin)
                    return "non-monotonic xmin in chain '" + key + "'";
            }
            for (std::size_t i = 0; i < chain.size(); ++i) {
                if (chain[i].xmax != 0 && chain[i].xmin >= chain[i].xmax)
                    return "xmax <= xmin in '" + key + "'";
                if (i + 1 < chain.size() && chain[i].xmax == 0)
                    return "non-tail version with xmax=0 in '" + key + "'";
            }
        }
        return "";
    }

private:
    Result write_impl(txn_id_t tx, const std::string& key,
                      const std::string& value, bool tombstone) {
        auto it = txns_.find(tx);
        if (it == txns_.end() || it->second.state != State::Active) return Result::Aborted;
        TxnInfo& info = it->second;

        // already holds the X-lock — just buffer the new value
        if (info.locks.count(key)) {
            Version& v = info.buffer[key];
            v.value = value;
            v.tombstone = tombstone;
            return Result::Ok;
        }

        auto lk = xlock_.find(key);
        if (lk != xlock_.end() && lk->second != tx) {
            waits_for_[tx] = lk->second;
            if (txn_id_t victim = detect_deadlock_victim(tx)) {
                abort_internal(victim);
                if (victim == tx) return Result::Aborted;
                // victim was elsewhere; current txn keeps waiting via LockWait
            }
            return Result::LockWait;
        }

        xlock_[key] = tx;
        info.locks.insert(key);
        Version v;
        v.value = value;
        v.tombstone = tombstone;
        info.buffer[key] = std::move(v);
        return Result::Ok;
    }

    void release_locks(TxnInfo& info) {
        for (const std::string& k : info.locks) xlock_.erase(k);
        info.locks.clear();
        // drop any wait edges that target now-released txn? we only key by waiter,
        // so a stale entry self-corrects on next deadlock check.
    }

    void abort_internal(txn_id_t tx) {
        auto it = txns_.find(tx);
        if (it == txns_.end()) return;
        if (it->second.state != State::Active) return;
        release_locks(it->second);
        it->second.buffer.clear();
        it->second.state = State::Aborted;
        waits_for_.erase(tx);
    }

    // DFS over waits_for_ starting from `start`. Returns the victim id
    // (youngest = highest id) within any cycle that includes `start`, else 0.
    txn_id_t detect_deadlock_victim(txn_id_t start) {
        std::unordered_set<txn_id_t> seen;
        std::vector<txn_id_t> stack;
        txn_id_t cur = start;
        while (true) {
            auto it = waits_for_.find(cur);
            if (it == waits_for_.end()) return 0;
            txn_id_t next = it->second;
            if (next == start || seen.count(next)) {
                stack.push_back(cur);
                txn_id_t victim = start;
                for (txn_id_t id : stack) if (id > victim) victim = id;
                if (next != start) {
                    // cycle exists but does not involve `start`; leave it for
                    // a future call to surface.
                    return 0;
                }
                last_victim_ = victim;
                return victim;
            }
            seen.insert(next);
            stack.push_back(cur);
            cur = next;
        }
    }

    ts_t oldest_snapshot() const {
        ts_t floor = clock_ + 1;
        bool any = false;
        for (auto& [_, t] : txns_) {
            if (t.state == State::Active) { floor = std::min(floor, t.snapshot); any = true; }
        }
        return any ? floor : clock_;
    }

    // ---- state -------------------------------------------------------------
    ts_t                                                  clock_{0};
    txn_id_t                                              next_txn_{0};
    std::unordered_map<std::string, std::vector<Version>> chains_;
    std::unordered_map<std::string, txn_id_t>             xlock_;
    std::unordered_map<txn_id_t, txn_id_t>                waits_for_;
    std::unordered_map<txn_id_t, TxnInfo>                 txns_;
    std::optional<txn_id_t>                               last_victim_;
};

}  // namespace adbms::txn
