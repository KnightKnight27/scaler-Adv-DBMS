#pragma once
/**
 * Lab 6 — Strict Two-Phase Locking (2PL) Lock Manager
 *
 * Implements shared (S) and exclusive (X) locks with strict 2PL:
 * - Growing phase: acquire locks as needed
 * - Locks are held until transaction commits or aborts (strict = no early release)
 * - Prevents dirty reads, non-repeatable reads, and phantoms
 */

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <string>
#include <algorithm>

using TxnId = uint64_t;
using RowId = uint64_t;

enum class LockMode { SHARED, EXCLUSIVE };

inline std::string lock_mode_str(LockMode mode) {
    return mode == LockMode::SHARED ? "S" : "X";
}

// ─────────────────────────────────────────────────
// LockRequest: A single lock request
// ─────────────────────────────────────────────────
struct LockRequest {
    TxnId    txn_id;
    LockMode mode;
    bool     granted;

    LockRequest(TxnId t, LockMode m) : txn_id(t), mode(m), granted(false) {}
};

// ─────────────────────────────────────────────────
// LockQueue: Manages lock requests for a single resource
// ─────────────────────────────────────────────────
struct LockQueue {
    std::vector<LockRequest> requests;
    LockMode                 current_mode = LockMode::SHARED;
    int                      granted_count = 0;

    bool has_exclusive_granted() const {
        for (const auto& req : requests) {
            if (req.granted && req.mode == LockMode::EXCLUSIVE) return true;
        }
        return false;
    }

    bool has_granted_by(TxnId txn_id) const {
        for (const auto& req : requests) {
            if (req.txn_id == txn_id && req.granted) return true;
        }
        return false;
    }

    LockMode get_granted_mode(TxnId txn_id) const {
        for (const auto& req : requests) {
            if (req.txn_id == txn_id && req.granted) return req.mode;
        }
        return LockMode::SHARED;
    }
};

// ─────────────────────────────────────────────────
// LockManager: Coordinates lock acquisition and release
// ─────────────────────────────────────────────────
class LockManager {
private:
    std::unordered_map<RowId, LockQueue>               lock_table_;
    std::unordered_map<TxnId, std::vector<RowId>>      txn_locks_;   // txn → locked resources
    std::mutex                                          mutex_;
    std::condition_variable                             cv_;

public:
    enum class LockResult { GRANTED, WAITING, DEADLOCK, UPGRADED };

    /**
     * Acquire a lock on a resource.
     * Returns immediately if lock can be granted;
     * returns WAITING if must wait (caller should handle deadlock check externally).
     */
    LockResult lock(TxnId txn_id, RowId row_id, LockMode mode) {
        std::lock_guard<std::mutex> guard(mutex_);

        auto& queue = lock_table_[row_id];

        // Check if we already hold a lock on this resource
        for (auto& req : queue.requests) {
            if (req.txn_id == txn_id && req.granted) {
                if (req.mode == LockMode::EXCLUSIVE) {
                    return LockResult::GRANTED;  // already have X lock
                }
                if (mode == LockMode::SHARED) {
                    return LockResult::GRANTED;  // already have S lock, requesting S
                }
                // Upgrade: S → X
                if (queue.granted_count == 1) {
                    req.mode = LockMode::EXCLUSIVE;
                    return LockResult::UPGRADED;
                }
                // Can't upgrade — other transactions hold shared locks
                // Add upgrade request to wait
                queue.requests.emplace_back(txn_id, LockMode::EXCLUSIVE);
                return LockResult::WAITING;
            }
        }

        // New lock request
        LockRequest new_req(txn_id, mode);

        // Can we grant immediately?
        if (queue.granted_count == 0) {
            // No one holds any lock → grant
            new_req.granted = true;
            queue.granted_count++;
            queue.requests.push_back(new_req);
            txn_locks_[txn_id].push_back(row_id);
            return LockResult::GRANTED;
        }

        if (mode == LockMode::SHARED && !queue.has_exclusive_granted()) {
            // Shared request and no exclusive lock held → grant
            new_req.granted = true;
            queue.granted_count++;
            queue.requests.push_back(new_req);
            txn_locks_[txn_id].push_back(row_id);
            return LockResult::GRANTED;
        }

        // Must wait
        queue.requests.push_back(new_req);
        txn_locks_[txn_id].push_back(row_id);
        return LockResult::WAITING;
    }

    /**
     * Release all locks held by a transaction (called on commit/abort)
     * Strict 2PL: locks are only released at end of transaction
     */
    void release_all(TxnId txn_id) {
        std::lock_guard<std::mutex> guard(mutex_);

        auto it = txn_locks_.find(txn_id);
        if (it == txn_locks_.end()) return;

        for (RowId row_id : it->second) {
            auto& queue = lock_table_[row_id];

            // Remove all requests from this transaction
            queue.requests.erase(
                std::remove_if(queue.requests.begin(), queue.requests.end(),
                    [txn_id](const LockRequest& r) { return r.txn_id == txn_id; }),
                queue.requests.end()
            );

            // Recount granted locks
            queue.granted_count = 0;
            for (const auto& req : queue.requests) {
                if (req.granted) queue.granted_count++;
            }

            // Try to grant waiting requests
            grant_waiting(queue);
        }

        txn_locks_.erase(it);
        cv_.notify_all();
    }

    /**
     * Get transactions that a given transaction is waiting on
     * Used for deadlock detection (building the wait-for graph)
     */
    std::vector<TxnId> get_blocking_txns(TxnId txn_id) const {
        std::vector<TxnId> blockers;

        for (const auto& [row_id, queue] : lock_table_) {
            bool is_waiting = false;
            for (const auto& req : queue.requests) {
                if (req.txn_id == txn_id && !req.granted) {
                    is_waiting = true;
                    break;
                }
            }

            if (is_waiting) {
                for (const auto& req : queue.requests) {
                    if (req.txn_id != txn_id && req.granted) {
                        blockers.push_back(req.txn_id);
                    }
                }
            }
        }

        return blockers;
    }

    void print_lock_table() const {
        std::cout << "\n  Lock Table:" << std::endl;
        for (const auto& [row_id, queue] : lock_table_) {
            if (queue.requests.empty()) continue;
            std::cout << "    Row " << row_id << ": ";
            for (const auto& req : queue.requests) {
                std::cout << "T" << req.txn_id << "("
                          << lock_mode_str(req.mode)
                          << (req.granted ? ",granted" : ",waiting")
                          << ") ";
            }
            std::cout << std::endl;
        }
    }

private:
    void grant_waiting(LockQueue& queue) {
        for (auto& req : queue.requests) {
            if (req.granted) continue;

            if (queue.granted_count == 0) {
                req.granted = true;
                queue.granted_count++;
                continue;
            }

            if (req.mode == LockMode::SHARED && !queue.has_exclusive_granted()) {
                req.granted = true;
                queue.granted_count++;
            }
            // Exclusive requests wait until all current holders release
        }
    }
};
