#include "LockManager.h"

#include <stdexcept>

// ── Lock acquisition ────────────────────────────────────────────────────

bool LockManager::lock(Transaction* txn, int64_t rid, LockMode mode) {
    std::lock_guard<std::mutex> guard(mutex_);

    if (txn->getState() != TxnState::GROWING) {
        throw std::runtime_error(
            "Cannot acquire lock: Transaction " +
            std::to_string(txn->getTxnId()) +
            " is in state " + txn->stateToString());
    }

    auto it = lockTable_.find(rid);

    // ── Case 1: No existing lock on this record ─────────────────────
    if (it == lockTable_.end()) {
        LockEntry entry;
        entry.mode = mode;
        entry.holders.insert(txn->getTxnId());
        lockTable_[rid] = entry;

        if (mode == LockMode::SHARED) {
            txn->addSharedLock(rid);
        } else {
            txn->addExclusiveLock(rid);
        }
        return true;
    }

    LockEntry& entry = it->second;

    // ── Case 2: This transaction already holds the lock ─────────────
    if (entry.holders.count(txn->getTxnId())) {
        if (mode == LockMode::SHARED) {
            // Already have it (either S or X) — no-op
            return true;
        }

        // Requesting X — check if we need to upgrade from S to X
        if (entry.mode == LockMode::EXCLUSIVE) {
            // Already hold X — no-op
            return true;
        }

        // Currently S, want X — lock upgrade
        if (entry.holders.size() == 1) {
            // We're the only holder — safe to upgrade
            entry.mode = LockMode::EXCLUSIVE;
            txn->addExclusiveLock(rid);
            return true;
        }

        // Other S holders exist — check for deadlock
        for (int holderId : entry.holders) {
            if (holderId != txn->getTxnId()) {
                if (deadlockDetector_.wouldCauseCycle(txn->getTxnId(), holderId)) {
                    return false;  // deadlock detected — caller should abort
                }
            }
        }

        // In a real system, we'd wait here. For simplicity, we use
        // a no-wait policy: if we can't upgrade immediately, report deadlock.
        return false;
    }

    // ── Case 3: Other transaction(s) hold the lock ──────────────────
    if (mode == LockMode::SHARED && entry.mode == LockMode::SHARED) {
        // Compatible: multiple S locks can coexist
        entry.holders.insert(txn->getTxnId());
        txn->addSharedLock(rid);
        return true;
    }

    // Incompatible lock request (S vs X, or X vs anything)
    // Check for deadlock before deciding to wait/abort
    for (int holderId : entry.holders) {
        if (deadlockDetector_.wouldCauseCycle(txn->getTxnId(), holderId)) {
            // Deadlock detected — requesting transaction should abort
            return false;
        }
    }

    // In our simplified implementation, we use no-wait: if the lock
    // can't be granted immediately, we return false (caller aborts).
    // A full implementation would block the thread and use condition
    // variables to wake up when the lock becomes available.
    //
    // We add wait-for edges for diagnostic purposes even though
    // we don't actually block.
    for (int holderId : entry.holders) {
        deadlockDetector_.addEdge(txn->getTxnId(), holderId);
    }

    // No-wait policy: return false to signal conflict
    return false;
}

// ── Lock release ────────────────────────────────────────────────────────

void LockManager::unlock(Transaction* txn, int64_t rid) {
    std::lock_guard<std::mutex> guard(mutex_);

    auto it = lockTable_.find(rid);
    if (it == lockTable_.end()) return;

    LockEntry& entry = it->second;
    entry.holders.erase(txn->getTxnId());

    // Clean up the deadlock detector edges
    deadlockDetector_.removeTxn(txn->getTxnId());

    if (entry.holders.empty()) {
        lockTable_.erase(it);
    }
}

void LockManager::unlockAll(Transaction* txn) {
    std::lock_guard<std::mutex> guard(mutex_);

    // Release all shared locks
    for (int64_t rid : txn->getSharedLocks()) {
        auto it = lockTable_.find(rid);
        if (it != lockTable_.end()) {
            it->second.holders.erase(txn->getTxnId());
            if (it->second.holders.empty()) {
                lockTable_.erase(it);
            }
        }
    }

    // Release all exclusive locks
    for (int64_t rid : txn->getExclusiveLocks()) {
        auto it = lockTable_.find(rid);
        if (it != lockTable_.end()) {
            it->second.holders.erase(txn->getTxnId());
            if (it->second.holders.empty()) {
                lockTable_.erase(it);
            }
        }
    }

    // Clean up deadlock detector
    deadlockDetector_.removeTxn(txn->getTxnId());
}
