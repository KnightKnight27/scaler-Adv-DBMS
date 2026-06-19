// txn_manager.hpp — ADBMS Lab 8 / 24BCS10193 Anushka Jain
//
// Header-only C++17 in-memory transaction manager.
//
// Concurrency control strategy:
//   * READS   — MVCC: each transaction reads from a consistent snapshot
//               captured at begin(). No locks acquired for reads.
//   * WRITES  — Strict 2PL: exclusive locks held until commit/abort.
//   * DEADLOCK— Cycle detection on the waits-for graph via iterative path
//               traversal. Youngest transaction (highest ID) is the victim.
//   * COMMIT  — First-updater-wins: validates that no written key was
//               committed by another transaction after our snapshot.
//   * GC      — Removes versions whose xmax falls below the oldest active
//               snapshot, freeing memory safely.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace adbms::txn {

// ── Type aliases ─────────────────────────────────────────────────────────────
using txn_id_t = std::uint64_t;
using ts_t     = std::uint64_t;

// ── Enumerations ─────────────────────────────────────────────────────────────
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
        case Result::Ok:                   return "OK";
        case Result::NotFound:             return "NOT_FOUND";
        case Result::LockWait:             return "LOCK_WAIT";
        case Result::Aborted:              return "ABORTED";
        case Result::SerializationFailure: return "SERIALIZATION_FAILURE";
    }
    return "?";
}

inline std::string to_string(State s) {
    switch (s) {
        case State::Active:    return "Active";
        case State::Committed: return "Committed";
        case State::Aborted:   return "Aborted";
    }
    return "?";
}

// ── Manager ───────────────────────────────────────────────────────────────────
class Manager {
public:

    // ── Public API ────────────────────────────────────────────────────────────

    // Allocate a new transaction and capture its read snapshot.
    txn_id_t begin() {
        txn_id_t id = ++next_txn_id_;
        TxnEntry& e = txn_table_[id];
        e.id       = id;
        e.snapshot = global_ts_;
        e.state    = State::Active;
        return id;
    }

    // Read key into `out`.  Checks the transaction's private buffer first,
    // then scans the version chain for the newest version visible at snapshot.
    Result read(txn_id_t tx, const std::string& key, std::string& out) {
        TxnEntry* e = find_txn(tx);
        if (!e || e->state != State::Active) return Result::Aborted;

        // Private write buffer takes precedence
        auto buf_it = e->write_buf.find(key);
        if (buf_it != e->write_buf.end()) {
            if (buf_it->second.deleted) return Result::NotFound;
            out = buf_it->second.val;
            return Result::Ok;
        }

        // MVCC version chain
        const VersionRecord* vr = find_visible(key, e->snapshot);
        if (!vr || vr->deleted) return Result::NotFound;
        out = vr->val;
        return Result::Ok;
    }

    // Write value to key within transaction tx.
    Result write(txn_id_t tx, const std::string& key, const std::string& val) {
        return do_write(tx, key, val, false);
    }

    // Logically delete key within transaction tx.
    Result remove(txn_id_t tx, const std::string& key) {
        return do_write(tx, key, {}, true);
    }

    // Commit transaction.  Runs first-updater-wins validation, then applies
    // buffered writes to the version store and releases all locks.
    Result commit(txn_id_t tx) {
        TxnEntry* e = find_txn(tx);
        if (!e || e->state != State::Active) return Result::Aborted;

        // ── Validation: first-updater-wins ───────────────────────────────────
        for (const auto& [key, _] : e->write_buf) {
            auto sit = version_store_.find(key);
            if (sit == version_store_.end() || sit->second.empty()) continue;
            // The latest committed version's xmin must not exceed our snapshot.
            if (sit->second.back().xmin > e->snapshot) {
                kill_txn(tx);
                return Result::SerializationFailure;
            }
        }

        // ── Write phase ───────────────────────────────────────────────────────
        ts_t commit_ts = ++global_ts_;

        for (auto& [key, pending] : e->write_buf) {
            auto& chain = version_store_[key];
            // Seal the previous head version
            if (!chain.empty() && chain.back().xmax == 0)
                chain.back().xmax = commit_ts;

            VersionRecord vr;
            vr.val     = pending.val;
            vr.deleted = pending.deleted;
            vr.xmin    = commit_ts;
            vr.xmax    = 0;
            vr.author  = tx;
            chain.push_back(std::move(vr));
        }

        free_locks(*e);
        e->state = State::Committed;
        clear_waits_for(tx);
        return Result::Ok;
    }

    // Abort and roll back a transaction.
    void abort(txn_id_t tx) { kill_txn(tx); }

    // Garbage-collect versions that are invisible to all active snapshots.
    std::size_t gc() {
        ts_t floor = oldest_active_snapshot();
        std::size_t removed = 0;
        for (auto& [key, chain] : version_store_) {
            std::vector<VersionRecord> kept;
            for (auto& vr : chain) {
                // A version is dead when it has been superseded (xmax != 0)
                // and even the oldest active snapshot can no longer see it.
                if (vr.xmax != 0 && vr.xmax <= floor) {
                    ++removed;
                    continue;
                }
                kept.push_back(std::move(vr));
            }
            chain.swap(kept);
        }
        return removed;
    }

    // ── Diagnostics ──────────────────────────────────────────────────────────

    State    state_of(txn_id_t tx) const {
        auto it = txn_table_.find(tx);
        return it == txn_table_.end() ? State::Aborted : it->second.state;
    }

    txn_id_t last_victim() const { return last_victim_id_; }

    std::size_t version_count() const {
        std::size_t n = 0;
        for (const auto& [_, chain] : version_store_) n += chain.size();
        return n;
    }

    std::size_t live_txn_count() const {
        std::size_t n = 0;
        for (const auto& [_, e] : txn_table_)
            if (e.state == State::Active) ++n;
        return n;
    }

    std::size_t lock_count() const { return xlock_map_.size(); }

    std::string dump_chain(const std::string& key) const {
        auto it = version_store_.find(key);
        if (it == version_store_.end()) return "(no versions)";
        std::ostringstream os;
        for (const auto& vr : it->second) {
            os << "[xmin=" << vr.xmin << " xmax=" << vr.xmax
               << " " << (vr.deleted ? "<del>" : vr.val)
               << " by T" << vr.author << "] ";
        }
        return os.str();
    }

    // Verify internal structural invariants.  Returns an empty string on
    // success, or an error message on violation.
    std::string check_invariants() const {
        for (const auto& [key, holder_id] : xlock_map_) {
            auto it = txn_table_.find(holder_id);
            if (it == txn_table_.end() || it->second.state != State::Active)
                return "lock held by non-active txn on key: " + key;
        }
        return {};
    }

private:

    // ── Internal data structures ──────────────────────────────────────────────

    // One committed (or in-progress) value for a key.
    struct VersionRecord {
        std::string val;
        bool        deleted = false;
        ts_t        xmin    = 0;   // commit ts that created this version
        ts_t        xmax    = 0;   // commit ts that superseded it (0 = live)
        txn_id_t    author  = 0;
    };

    // Pending (not yet committed) write in a transaction's local buffer.
    struct PendingWrite {
        std::string val;
        bool        deleted = false;
    };

    struct TxnEntry {
        txn_id_t    id       = 0;
        ts_t        snapshot = 0;
        State       state    = State::Active;
        std::unordered_map<std::string, PendingWrite>  write_buf;
        std::unordered_set<std::string>                held_locks;
    };

    // ── State ─────────────────────────────────────────────────────────────────
    txn_id_t next_txn_id_ = 0;
    ts_t     global_ts_   = 0;
    txn_id_t last_victim_id_ = 0;

    std::unordered_map<txn_id_t, TxnEntry>                   txn_table_;
    std::unordered_map<std::string, std::vector<VersionRecord>> version_store_;
    std::unordered_map<std::string, txn_id_t>                 xlock_map_;

    // waits_for_[A] = B  means transaction A is currently waiting for B's lock.
    std::unordered_map<txn_id_t, txn_id_t> waits_for_;

    // ── Helpers ───────────────────────────────────────────────────────────────

    TxnEntry* find_txn(txn_id_t id) {
        auto it = txn_table_.find(id);
        return it == txn_table_.end() ? nullptr : &it->second;
    }

    // Walk the version chain of `key` backwards to find the newest version
    // whose xmin <= snapshot and (xmax == 0 || xmax > snapshot).
    const VersionRecord* find_visible(const std::string& key, ts_t snap) const {
        auto sit = version_store_.find(key);
        if (sit == version_store_.end()) return nullptr;
        const auto& chain = sit->second;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (it->xmin <= snap && (it->xmax == 0 || it->xmax > snap))
                return &*it;
        }
        return nullptr;
    }

    // Core write path shared by write() and remove().
    Result do_write(txn_id_t tx, const std::string& key,
                    const std::string& val, bool del) {
        TxnEntry* e = find_txn(tx);
        if (!e || e->state != State::Active) return Result::Aborted;

        // Already own the lock → update the buffer directly.
        if (e->held_locks.count(key)) {
            e->write_buf[key] = {val, del};
            return Result::Ok;
        }

        // Someone else holds the lock.
        auto lk_it = xlock_map_.find(key);
        if (lk_it != xlock_map_.end() && lk_it->second != tx) {
            txn_id_t blocker = lk_it->second;
            waits_for_[tx] = blocker;

            txn_id_t victim = detect_cycle(tx);
            if (victim != 0) {
                last_victim_id_ = victim;
                kill_txn(victim);
                if (victim == tx) return Result::Aborted;
                // Victim was the other side; fall through — caller must retry.
            }
            return Result::LockWait;
        }

        // Lock is free — acquire it.
        xlock_map_[key] = tx;
        e->held_locks.insert(key);
        waits_for_.erase(tx);
        e->write_buf[key] = {val, del};
        return Result::Ok;
    }

    // Iterative cycle detection starting from `start`.
    // Returns the highest-ID transaction in the cycle (the victim), or 0.
    txn_id_t detect_cycle(txn_id_t start) {
        std::vector<txn_id_t> path;
        std::unordered_set<txn_id_t> on_path;
        txn_id_t cur = start;

        while (cur != 0) {
            if (on_path.count(cur)) {
                // Found the cycle entry point — collect IDs in the cycle.
                txn_id_t max_id = 0;
                bool in_cycle = false;
                for (txn_id_t node : path) {
                    if (node == cur) in_cycle = true;
                    if (in_cycle)   max_id = std::max(max_id, node);
                }
                return max_id;
            }
            on_path.insert(cur);
            path.push_back(cur);
            auto it = waits_for_.find(cur);
            cur = (it != waits_for_.end()) ? it->second : 0;
        }
        return 0;  // no cycle
    }

    // Release all exclusive locks held by a transaction.
    void free_locks(TxnEntry& e) {
        for (const auto& key : e.held_locks) xlock_map_.erase(key);
        e.held_locks.clear();
    }

    // Remove waits-for edges pointing to or from `tx`.
    void clear_waits_for(txn_id_t tx) {
        waits_for_.erase(tx);
        std::vector<txn_id_t> dependents;
        for (const auto& [waiter, holder] : waits_for_)
            if (holder == tx) dependents.push_back(waiter);
        for (txn_id_t w : dependents) waits_for_.erase(w);
    }

    // Abort a transaction, rolling back its buffer and releasing locks.
    void kill_txn(txn_id_t tx) {
        TxnEntry* e = find_txn(tx);
        if (!e || e->state != State::Active) return;
        e->write_buf.clear();
        free_locks(*e);
        e->state = State::Aborted;
        clear_waits_for(tx);
    }

    // The snapshot of the oldest currently-active transaction.
    // Used as the GC floor: any version with xmax <= this is invisible to all.
    ts_t oldest_active_snapshot() const {
        ts_t lo = global_ts_;
        for (const auto& [_, e] : txn_table_)
            if (e.state == State::Active) lo = std::min(lo, e.snapshot);
        return lo;
    }
};

}  // namespace adbms::txn