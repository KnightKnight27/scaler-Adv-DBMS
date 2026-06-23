#pragma once

#include "common/types.h"
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <string>
#include <functional>
#include <stdexcept>

// ─── Lock modes ───────────────────────────────────────────────────────────────

enum class LockMode {
    SHARED,     // multiple readers can hold simultaneously
    EXCLUSIVE   // only one writer; blocks all other readers and writers
};

// ─── DeadlockException ────────────────────────────────────────────────────────
// Thrown when the waits-for graph contains a cycle.

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid)
        : std::runtime_error("Deadlock detected — aborting transaction " + std::to_string(xid))
        , victim_xid(xid)
    {}
    TxID victim_xid;
};

// ─── LockManager ──────────────────────────────────────────────────────────────
//
// Implements Strict Two-Phase Locking (Strict 2PL):
//
//   Growing phase:  the transaction may acquire new locks (SHARED or EXCLUSIVE).
//   Shrinking phase: begins at commit/abort; ALL locks are released at once.
//                   No new locks can be acquired after shrinking starts.
//
// Strict 2PL guarantees serializability and avoids cascading aborts because
// no lock is released until the transaction has fully committed or aborted.
//
// Lock compatibility matrix:
//   Granted\Requested  SHARED  EXCLUSIVE
//   SHARED             ✓       ✗
//   EXCLUSIVE          ✗       ✗
//
// Deadlock detection: maintains a waits-for graph.
// When a cycle is detected, throws DeadlockException for the younger transaction.

class LockManager {
public:
    // Acquire a lock on resource_key for transaction xid.
    // Blocks until the lock is granted or a deadlock is detected.
    // Throws DeadlockException if a cycle is found.
    void acquireLock(TxID xid, const std::string& resource_key, LockMode mode);

    // Release ALL locks held by xid (called at commit/abort).
    void releaseAll(TxID xid);

    // Mark a transaction as entering the shrinking phase.
    // After this, acquireLock will throw for this xid.
    void beginShrinking(TxID xid);

    // Print the current lock table state (for debugging / demo).
    void printLockTable() const;

private:
    // A single lock request in the queue for a resource.
    struct LockRequest {
        TxID     xid;
        LockMode mode;
        bool     granted = false;
    };

    // One entry in the lock table for a resource_key.
    struct LockQueue {
        std::list<LockRequest>  requests;
        std::mutex              mu;
        std::condition_variable cv;
    };

    // Check if granting mode to xid conflicts with any currently granted lock.
    bool hasConflict(const LockQueue& lq, TxID xid, LockMode mode) const;

    // Waits-for graph cycle detection using DFS.
    bool hasCycle(TxID start,
                  const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) const;

    // Global lock table: resource_key → LockQueue
    // We use a pointer map so we can create queues on demand without invalidating refs.
    std::unordered_map<std::string, LockQueue*> lock_table_;
    std::mutex                                   lt_mutex_;    // protects lock_table_

    // Waits-for graph: waiter_xid → set of holders being waited on
    std::unordered_map<TxID, std::unordered_set<TxID>> waits_for_;
    std::mutex                                           wf_mutex_;

    // Set of transactions in their shrinking phase
    std::unordered_set<TxID> shrinking_;
    std::mutex               shrink_mutex_;

    // Get or create a LockQueue for a resource (thread-safe).
    LockQueue& getQueue(const std::string& key);
};
