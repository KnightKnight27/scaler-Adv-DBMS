// txn_manager.hpp — ADBMS Lab 8 / 24BCS10199 Ayush Singh
//
// An in-memory transaction manager that splits concurrency control the same
// way PostgreSQL does:
//
//   * READS  — MVCC against the snapshot taken at begin(). Readers never
//              block and never take locks.
//   * WRITES — Strict two-phase locking.
//   * DEADLOCK — waits-for cycle detection.
//   * COMMIT — First-updater-wins.
//   * gc()  — Prunes dead versions.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace adbms::txn {

using txn_id_t = std::uint64_t;
using ts_t     = std::uint64_t;

enum class Result {
    Ok,
    NotFound,
    LockWait,
    Aborted,
    SerializationFailure,
};

enum class State { Active, Committed, Aborted };

inline std::string to_string(Result r) {
    switch (r) {
        case Result::Ok: return "OK";
        case Result::NotFound: return "NOT_FOUND";
        case Result::LockWait: return "LOCK_WAIT";
        case Result::Aborted: return "ABORTED";
        case Result::SerializationFailure: return "SERIALIZATION_FAILURE";
    }
    return "?";
}

inline std::string to_string(State s) {
    switch (s) {
        case State::Active: return "Active";
        case State::Committed: return "Committed";
        case State::Aborted: return "Aborted";
    }
    return "?";
}

class Manager {
public:
    txn_id_t begin() {
        txn_id_t id = ++next_id_;
        Txn& t = txns_[id];
        t.id = id;
        t.snapshot = clock_;
        t.state = State::Active;
        return id;
    }

    Result read(txn_id_t tx, const std::string& key, std::string& out) {
        Txn* t = lookup(tx);
        if (!t || t->state != State::Active) return Result::Aborted;

        if (auto it = t->buffer.find(key); it != t->buffer.end()) {
            if (it->second.tombstone) return Result::NotFound;
            out = it->second.value;
            return Result::Ok;
        }

        const Version* v = visible_to(key, t->snapshot);
        if (!v || v->tombstone) return Result::NotFound;

        out = v->value;
        return Result::Ok;
    }

    Result write(txn_id_t tx, const std::string& key, const std::string& value) {
        return write_impl(tx, key, value, false);
    }

    Result remove(txn_id_t tx, const std::string& key) {
        return write_impl(tx, key, "", true);
    }

    Result commit(txn_id_t tx) {
        Txn* t = lookup(tx);
        if (!t || t->state != State::Active) return Result::Aborted;

        // First-updater-wins check
        for (const auto& [key, _] : t->buffer) {
            const auto it = store_.find(key);
            if (it == store_.end()) continue;

            const auto& chain = it->second;
            if (chain.empty()) continue;

            const Version& head = chain.back();
            // If the key has a committed version with xmin > t->snapshot, it means
            // another transaction updated and committed it since we started.
            if (head.xmin > t->snapshot) {
                abort_internal(t->id);
                return Result::SerializationFailure;
            }
        }

        ts_t commit_ts = ++clock_;

        for (auto& [key, pending] : t->buffer) {
            auto& chain = store_[key];
            if (!chain.empty()) chain.back().xmax = commit_ts;

            Version v;
            v.value = pending.value;
            v.tombstone = pending.tombstone;
            v.xmin = commit_ts;
            v.xmax = 0;
            v.creator = t->id;
            chain.push_back(std::move(v));
        }

        release_all_locks(*t);
        t->state = State::Committed;
        waits_for_.erase(t->id);
        
        // Remove anyone waiting for us
        std::vector<txn_id_t> waiters;
        for (auto& [waiter, holder] : waits_for_) {
            if (holder == t->id) {
                waiters.push_back(waiter);
            }
        }
        for (auto w : waiters) {
            waits_for_.erase(w);
        }

        return Result::Ok;
    }

    void abort(txn_id_t tx) { abort_internal(tx); }

    std::size_t gc() {
        ts_t low = oldest_snapshot();
        std::size_t pruned = 0;

        for (auto& [key, chain] : store_) {
            std::vector<Version> kept;
            for (auto& v : chain) {
                if (v.xmax != 0 && v.xmax <= low) {
                    ++pruned;
                    continue;
                }
                kept.push_back(std::move(v));
            }
            chain.swap(kept);
        }

        return pruned;
    }

    State state_of(txn_id_t tx) const {
        auto it = txns_.find(tx);
        return it == txns_.end() ? State::Aborted : it->second.state;
    }

    txn_id_t last_victim() const { return last_victim_; }

    std::size_t version_count() const {
        std::size_t n = 0;
        for (auto& [_, c] : store_) n += c.size();
        return n;
    }

    std::size_t live_txn_count() const {
        std::size_t n = 0;
        for (auto& [_, t] : txns_)
            if (t.state == State::Active) ++n;
        return n;
    }

    std::size_t lock_count() const { return xlock_.size(); }

    std::string dump_chain(const std::string& key) const {
        auto it = store_.find(key);
        if (it == store_.end()) return "(no versions)";

        std::ostringstream os;
        for (auto& v : it->second) {
            os << "[xmin=" << v.xmin << " xmax=" << v.xmax
               << " " << (v.tombstone ? "<del>" : v.value)
               << " by T" << v.creator << "] ";
        }
        return os.str();
    }

    std::string check_invariants() const {
        for (auto& [_, holder] : xlock_) {
            auto it = txns_.find(holder);
            if (it == txns_.end() || it->second.state != State::Active)
                return "x-lock held by non-active txn";
        }
        return "";
    }

private:
    struct Version {
        std::string value;
        bool tombstone = false;
        ts_t xmin = 0;
        ts_t xmax = 0;
        txn_id_t creator = 0;
    };

    struct Pending {
        std::string value;
        bool tombstone = false;
    };

    struct Txn {
        txn_id_t id = 0;
        ts_t snapshot = 0;
        State state = State::Active;
        std::unordered_map<std::string, Pending> buffer;
        std::unordered_set<std::string> locks;
    };

    txn_id_t next_id_ = 0;
    ts_t clock_ = 0;
    txn_id_t last_victim_ = 0;

    std::unordered_map<txn_id_t, Txn> txns_;
    std::unordered_map<std::string, std::vector<Version>> store_;
    std::unordered_map<std::string, txn_id_t> xlock_;
    std::unordered_map<txn_id_t, txn_id_t> waits_for_;

    Txn* lookup(txn_id_t tx) {
        auto it = txns_.find(tx);
        return it == txns_.end() ? nullptr : &it->second;
    }

    const Version* visible_to(const std::string& key, ts_t snapshot) const {
        auto it = store_.find(key);
        if (it == store_.end()) return nullptr;

        for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
            if (rit->xmin <= snapshot &&
                (rit->xmax == 0 || rit->xmax > snapshot))
                return &*rit;
        }
        return nullptr;
    }

    txn_id_t detect_deadlock(txn_id_t start_tx) {
        std::vector<txn_id_t> path;
        std::unordered_set<txn_id_t> visited;
        txn_id_t curr = start_tx;

        while (curr != 0) {
            if (visited.count(curr)) {
                auto it = std::find(path.begin(), path.end(), curr);
                if (it != path.end()) {
                    txn_id_t victim = 0;
                    for (auto mit = it; mit != path.end(); ++mit) {
                        victim = std::max(victim, *mit);
                    }
                    return victim;
                }
                break;
            }
            visited.insert(curr);
            path.push_back(curr);

            auto it = waits_for_.find(curr);
            if (it != waits_for_.end()) {
                curr = it->second;
            } else {
                curr = 0;
            }
        }
        return 0;
    }

    Result write_impl(txn_id_t tx, const std::string& key,
                      const std::string& value, bool tombstone) {
        Txn* t = lookup(tx);
        if (!t || t->state != State::Active) return Result::Aborted;

        // 1. If we already hold the lock, we can write immediately
        if (t->locks.count(key)) {
            Pending& p = t->buffer[key];
            p.value = value;
            p.tombstone = tombstone;
            return Result::Ok;
        }

        // 2. Check if anyone else holds the lock
        auto lock_it = xlock_.find(key);
        if (lock_it != xlock_.end() && lock_it->second != tx) {
            txn_id_t holder = lock_it->second;
            waits_for_[tx] = holder;

            // Deadlock detection
            txn_id_t victim = detect_deadlock(tx);
            if (victim != 0) {
                last_victim_ = victim;
                abort_internal(victim);
                if (victim == tx) {
                    return Result::Aborted;
                }
            }
            return Result::LockWait;
        }

        // 3. Lock is free; acquire lock and proceed
        xlock_[key] = tx;
        t->locks.insert(key);
        waits_for_.erase(tx); // We are no longer waiting

        Pending& p = t->buffer[key];
        p.value = value;
        p.tombstone = tombstone;
        return Result::Ok;
    }

    void release_all_locks(Txn& t) {
        for (auto& k : t.locks) xlock_.erase(k);
        t.locks.clear();
    }

    void abort_internal(txn_id_t tx) {
        Txn* t = lookup(tx);
        if (!t || t->state != State::Active) return;
        t->buffer.clear();
        release_all_locks(*t);
        t->state = State::Aborted;
        
        waits_for_.erase(tx);
        
        // Remove anyone waiting for us
        std::vector<txn_id_t> waiters;
        for (auto& [waiter, holder] : waits_for_) {
            if (holder == tx) {
                waiters.push_back(waiter);
            }
        }
        for (auto w : waiters) {
            waits_for_.erase(w);
        }
    }

    ts_t oldest_snapshot() const {
        ts_t low = clock_;
        for (auto& [_, t] : txns_) {
            if (t.state == State::Active)
                low = std::min(low, t.snapshot);
        }
        return low;
    }
};

} // namespace adbms::txn
