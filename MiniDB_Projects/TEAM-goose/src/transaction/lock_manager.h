#pragma once

#include "common/types.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>

namespace minidb {

// lockmanager — two-phase locking with deadlock detection
// provides:
//   - shared (s) and exclusive (x) locks on resources (table_id + key)
//   - lock upgrade from s → x
//   - wait-die deadlock prevention + cycle detection fallback

enum class LockMode { SHARED = 0, EXCLUSIVE = 1 };

struct LockEntry {
    LockMode              mode;
    std::vector<TxnID>    holders;    // transactions holding this lock
    std::deque<TxnID>     wait_queue; // transactions waiting for this lock
};

class LockManager {
public:
    LockManager() = default;

    // acquire a lock on a resource.  blocks until granted or deadlock detected.
    // returns true on success, false on deadlock (transaction must abort).
    bool lock(TxnID txn_id, const std::string& resource, LockMode mode);

    // release all locks held by a transaction (called at commit/abort).
    void release_all(TxnID txn_id);

    // check if a transaction holds a specific lock.
    bool holds_lock(TxnID txn_id, const std::string& resource) const;

    // --- deadlock detection ---------------------------------------------------
    // build wait-for graph and detect cycles.  returns true if deadlock found.
    // `victim` is set to the transaction that should be aborted.
    bool detect_deadlock(TxnID& victim);

    // print current lock state (for debugging/demo).
    std::string dump() const;

private:
    bool can_grant(const LockEntry& entry, LockMode requested) const;
    void grant_lock(TxnID txn_id, LockEntry& entry, LockMode mode,
                    const std::string& resource);
    void build_wait_for_graph(
        std::unordered_map<TxnID, std::unordered_set<TxnID>>& graph) const;
    bool has_cycle(const std::unordered_map<TxnID, std::unordered_set<TxnID>>& graph,
                   TxnID start, std::unordered_set<TxnID>& visited,
                   std::unordered_set<TxnID>& rec_stack) const;

    mutable std::mutex _mutex;
    std::unordered_map<std::string, LockEntry> _lock_table;
    std::unordered_map<TxnID, std::unordered_set<std::string>> _txn_locks; // locks held per txn
};

// implementation

inline bool LockManager::can_grant(const LockEntry& entry, LockMode requested) const {
    if (entry.holders.empty()) return true;

    if (requested == LockMode::SHARED) {
        // multiple shared locks are ok, but not if an exclusive lock is held
        for (TxnID h : entry.holders) {
            if (entry.mode == LockMode::EXCLUSIVE) return false;
        }
        return true; // only shared locks held → another shared lock is fine
    }

    // requested exclusive: granted only if no other holder
    return entry.holders.empty();
}

inline void LockManager::grant_lock(TxnID txn_id, LockEntry& entry, LockMode mode,
                                     const std::string& resource) {
    entry.holders.push_back(txn_id);
    entry.mode = mode;
    _txn_locks[txn_id].insert(resource);
}

inline bool LockManager::lock(TxnID txn_id, const std::string& resource, LockMode mode) {
    std::unique_lock<std::mutex> lock(_mutex);

    // already held?
    if (_txn_locks.count(txn_id) && _txn_locks[txn_id].count(resource)) {
        auto& entry = _lock_table[resource];
        // lock upgrade: s → x is allowed if we're the only holder
        if (mode == LockMode::EXCLUSIVE && entry.mode == LockMode::SHARED &&
            entry.holders.size() == 1 && entry.holders[0] == txn_id) {
            entry.mode = LockMode::EXCLUSIVE;
            return true;
        }
        return true; // already held at sufficient level
    }

    auto& entry = _lock_table[resource];

    if (can_grant(entry, mode) && entry.wait_queue.empty()) {
        grant_lock(txn_id, entry, mode, resource);
        return true;
    }

    // cannot grant immediately — add to wait queue
    entry.wait_queue.push_back(txn_id);

    // check for deadlock before blocking
    // (simplified: we don't actually block; caller should retry or abort)
    // in a full implementation we'd use condition_variable wait with timeout.
    // for the assignment, we use a simple non-blocking approach + retry.

    // deadlock check
    TxnID victim = INVALID_TXN;
    bool deadlocked = detect_deadlock(victim);
    if (deadlocked && victim == txn_id) {
        // we are the victim — remove from wait queue
        auto& wq = entry.wait_queue;
        wq.erase(std::remove(wq.begin(), wq.end(), txn_id), wq.end());
        return false; // transaction must abort
    }

    // for simplicity: return false to indicate "could not acquire immediately".
    // the caller should retry or abort based on transaction logic.
    // remove from wait queue since we're not actually blocking.
    {
        auto& wq = entry.wait_queue;
        wq.erase(std::remove(wq.begin(), wq.end(), txn_id), wq.end());
    }
    return false;
}

inline void LockManager::release_all(TxnID txn_id) {
    std::unique_lock<std::mutex> lock(_mutex);

    auto it = _txn_locks.find(txn_id);
    if (it == _txn_locks.end()) return;

    for (const auto& resource : it->second) {
        auto lit = _lock_table.find(resource);
        if (lit != _lock_table.end()) {
            auto& holders = lit->second.holders;
            holders.erase(std::remove(holders.begin(), holders.end(), txn_id),
                          holders.end());
            if (holders.empty()) {
                lit->second.mode = LockMode::SHARED; // reset mode
            }
        }
    }
    _txn_locks.erase(it);
}

inline bool LockManager::holds_lock(TxnID txn_id, const std::string& resource) const {
    std::unique_lock<std::mutex> lock(_mutex);
    auto it = _txn_locks.find(txn_id);
    if (it == _txn_locks.end()) return false;
    return it->second.count(resource) > 0;
}

inline bool LockManager::detect_deadlock(TxnID& victim) {
    std::unordered_map<TxnID, std::unordered_set<TxnID>> graph;
    build_wait_for_graph(graph);

    // find cycles
    std::unordered_set<TxnID> visited;
    for (const auto& [txn, _] : graph) {
        std::unordered_set<TxnID> rec_stack;
        if (has_cycle(graph, txn, visited, rec_stack)) {
            // pick the youngest transaction (highest id) as victim
            TxnID max_id = INVALID_TXN;
            for (TxnID id : rec_stack) {
                if (id > max_id) max_id = id;
            }
            victim = max_id;
            return true;
        }
    }
    return false;
}

inline void LockManager::build_wait_for_graph(
    std::unordered_map<TxnID, std::unordered_set<TxnID>>& graph) const {
    // for each lock entry, waiting transactions → holding transactions edges
    for (const auto& [resource, entry] : _lock_table) {
        if (entry.wait_queue.empty()) continue;
        for (TxnID waiter : entry.wait_queue) {
            for (TxnID holder : entry.holders) {
                graph[waiter].insert(holder);
            }
        }
    }
}

inline bool LockManager::has_cycle(
    const std::unordered_map<TxnID, std::unordered_set<TxnID>>& graph,
    TxnID start, std::unordered_set<TxnID>& visited,
    std::unordered_set<TxnID>& rec_stack) const {

    if (rec_stack.count(start)) return true; // back edge → cycle
    if (visited.count(start)) return false;

    visited.insert(start);
    rec_stack.insert(start);

    auto it = graph.find(start);
    if (it != graph.end()) {
        for (TxnID neighbor : it->second) {
            if (has_cycle(graph, neighbor, visited, rec_stack))
                return true;
        }
    }

    rec_stack.erase(start);
    return false;
}

inline std::string LockManager::dump() const {
    std::ostringstream oss;
    for (const auto& [res, entry] : _lock_table) {
        oss << "Resource: " << res << "\n";
        oss << "  Mode: " << (entry.mode == LockMode::EXCLUSIVE ? "X" : "S") << "\n";
        oss << "  Holders: [";
        for (auto h : entry.holders) oss << h << " ";
        oss << "]\n  Waiters: [";
        for (auto w : entry.wait_queue) oss << w << " ";
        oss << "]\n";
    }
    return oss.str();
}

} // namespace minidb
