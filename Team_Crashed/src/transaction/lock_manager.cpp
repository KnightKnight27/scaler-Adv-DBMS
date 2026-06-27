// =============================================================================
// src/transaction/lock_manager.cpp
// -----------------------------------------------------------------------------
// Strict 2PL with deadlock detection via a wait-for graph.
//
// Concurrency model
// -----------------
//   * mu_ guards every map. The lock manager is a coarse-grained in-memory
//     structure.
//   * When a request cannot be granted because another transaction holds a
//     conflicting lock, the requesting transaction BLOCKS: it records its
//     wait edge in waitsFor_, runs a cycle check, and — if no cycle would
//     form — parks on cv_ until a release wakes it. On wake it re-checks
//     and either grabs the lock or parks again.
//   * If recording the wait edge WOULD form a cycle in waitsFor_, the
//     request is refused with Status::DEADLOCK. The caller aborts the
//     transaction, which releases its locks and wakes the waiters it was
//     blocking — resolving the deadlock. We abort the REQUESTER (the txn
//     that would close the cycle) as the victim.
//
// Visibility rules for SHARED vs EXCLUSIVE
//   * Many txns may hold S on the same rid.
//   * Only one txn may hold X; while X is held no S is granted either.
//   * Upgrading S -> X is treated as a fresh request; the own S lock is
//     ignored when checking for conflicting holders.
// =============================================================================
#include "transaction/lock_manager.h"

#include <algorithm>

namespace minidb::transaction {

LockManager::LockManager()  = default;
LockManager::~LockManager() = default;

namespace {

// DFS over the waitsFor_ graph. A back-edge means a cycle. Operates on a
// const reference; no locking needed if caller already holds mu_.
bool dfsHasCycle(TransactionId start,
                 const std::unordered_map<TransactionId, TransactionId>& edges,
                 std::unordered_set<TransactionId>& onStack,
                 std::unordered_set<TransactionId>& visited) {
    if (onStack.count(start))   return true;
    if (visited.count(start))   return false;
    visited.insert(start);
    onStack.insert(start);

    auto it = edges.find(start);
    if (it != edges.end()) {
        if (dfsHasCycle(it->second, edges, onStack, visited)) return true;
    }

    onStack.erase(start);
    return false;
}

} // anonymous namespace

// Check if some other txn holds an exclusive lock in this holder map.
// Caller must already hold mu_.
static bool holderMapHasOtherExclusive(TransactionId self,
    const std::unordered_map<TransactionId, LockMode>& h) {
    for (const auto& [other, mode] : h) {
        if (other != self && mode == LockMode::EXCLUSIVE) return true;
    }
    return false;
}

// Check for a cycle in waitsFor_, assuming the caller already holds mu_.
static bool hasCycleLocked(
    const std::unordered_map<TransactionId, TransactionId>& waitsFor) {
    std::unordered_set<TransactionId> visited;
    std::unordered_set<TransactionId> onStack;
    for (const auto& [t, _] : waitsFor) {
        if (visited.count(t)) continue;
        if (dfsHasCycle(t, waitsFor, onStack, visited)) return true;
    }
    return false;
}

// Remove `txn` from rid's waiter list and drop its wait edge. Caller holds mu_.
static void clearWait(LockManager&, // unused; kept for symmetry with old code
                      std::unordered_map<TransactionId, TransactionId>& waitsFor,
                      std::unordered_map<RecordId, std::vector<TransactionId>>& waiters,
                      TransactionId txn, RecordId rid) {
    waitsFor.erase(txn);
    auto& q = waiters[rid];
    q.erase(std::remove(q.begin(), q.end(), txn), q.end());
}

// Acquire a SHARED lock on rid for txn, blocking if an exclusive lock is held
// by another transaction. Returns DEADLOCK if the wait would close a cycle.
Status LockManager::acquireShared(TransactionId txn, RecordId rid) {
    std::unique_lock<std::mutex> lk(mu_);
    while (true) {
        auto& h = holders_[rid];
        if (!holderMapHasOtherExclusive(txn, h)) {
            h[txn] = LockMode::SHARED;
            clearWait(*this, waitsFor_, waiters_, txn, rid);
            return Status::OK;
        }
        // Park: record the wait edge against the (single) X-holder and check
        // for a cycle before blocking.
        TransactionId blocker = INVALID_TXN_ID;
        for (const auto& [other, mode] : h) {
            if (mode == LockMode::EXCLUSIVE) { blocker = other; break; }
        }
        waitsFor_[txn] = blocker;
        if (hasCycleLocked(waitsFor_)) {
            clearWait(*this, waitsFor_, waiters_, txn, rid);
            return Status::DEADLOCK;
        }
        waiters_[rid].push_back(txn);
        cv_.wait(lk);   // release mu_, park; reacquire on wake and recheck
    }
}

// Acquire an EXCLUSIVE lock on rid for txn, blocking if any other transaction
// holds any lock on rid. Returns DEADLOCK if the wait would close a cycle.
Status LockManager::acquireExclusive(TransactionId txn, RecordId rid) {
    std::unique_lock<std::mutex> lk(mu_);
    while (true) {
        auto& h = holders_[rid];
        TransactionId blocker = INVALID_TXN_ID;
        for (const auto& [other, mode] : h) {
            (void)mode;
            if (other == txn) continue;   // own S lock: upgrade is fine
            blocker = other;
            break;
        }
        if (blocker == INVALID_TXN_ID) {
            h[txn] = LockMode::EXCLUSIVE;
            clearWait(*this, waitsFor_, waiters_, txn, rid);
            return Status::OK;
        }
        waitsFor_[txn] = blocker;
        if (hasCycleLocked(waitsFor_)) {
            clearWait(*this, waitsFor_, waiters_, txn, rid);
            return Status::DEADLOCK;
        }
        waiters_[rid].push_back(txn);
        cv_.wait(lk);
    }
}

// Release every lock held by txn. Called at commit / abort. Wakes all
// blocked waiters so they can re-evaluate their requests.
void LockManager::releaseAll(TransactionId txn) {
    std::lock_guard<std::mutex> lk(mu_);

    for (auto& [rid, h] : holders_) {
        h.erase(txn);
    }
    // Drop empty holder entries to bound memory growth.
    for (auto it = holders_.begin(); it != holders_.end(); ) {
        if (it->second.empty()) it = holders_.erase(it);
        else                    ++it;
    }

    waitsFor_.erase(txn);
    for (auto& [rid, q] : waiters_) {
        q.erase(std::remove(q.begin(), q.end(), txn), q.end());
    }
    cv_.notify_all();
}

// Walk the wait-for graph. Public entry point (acquires mu_).
// Exposed for tests and the demo.
bool LockManager::hasCycle() {
    std::lock_guard<std::mutex> lk(mu_);
    return hasCycleLocked(waitsFor_);
}

// True iff `txn` holds any lock on `rid`. Public test inspector.
bool LockManager::holdsLock(TransactionId txn, RecordId rid) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = holders_.find(rid);
    if (it == holders_.end()) return false;
    return it->second.count(txn) != 0;
}

} // namespace minidb::transaction