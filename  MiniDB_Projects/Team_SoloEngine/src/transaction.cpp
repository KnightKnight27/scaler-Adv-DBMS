#include "transaction.h"

#include <stdexcept>

// ─── helpers ──────────────────────────────────────────────────────────────────

LockManager::LockRequestQueue &LockManager::GetOrCreateQueue(const RID &rid) {
    auto it = lock_table_.find(rid);
    if (it == lock_table_.end()) {
        it = lock_table_.emplace(rid, std::make_unique<LockRequestQueue>()).first;
    }
    return *it->second;
}

bool LockManager::CanGrantShared(const LockRequestQueue &q, txn_id_t id) const {
    // Shared is blocked only by a granted EXCLUSIVE held by another transaction.
    for (const auto &r : q.requests) {
        if (r.granted && r.mode == LockMode::EXCLUSIVE && r.txn_id != id)
            return false;
    }
    return true;
}

bool LockManager::CanGrantExclusive(const LockRequestQueue &q, txn_id_t id) const {
    // Exclusive is blocked by any granted lock held by another transaction.
    for (const auto &r : q.requests) {
        if (r.granted && r.txn_id != id)
            return false;
    }
    return true;
}

void LockManager::GrantPending(LockRequestQueue &q) {
    // Determine what is currently granted.
    bool any_granted       = false;
    bool exclusive_granted = false;
    for (const auto &r : q.requests) {
        if (r.granted) {
            any_granted = true;
            if (r.mode == LockMode::EXCLUSIVE) exclusive_granted = true;
        }
    }

    // Walk in FIFO order; grant compatible requests until we hit one we cannot grant.
    for (auto &r : q.requests) {
        if (r.granted) continue;

        if (r.mode == LockMode::SHARED && !exclusive_granted) {
            r.granted    = true;
            any_granted  = true;
            // Keep going: multiple shared locks can coexist.
        } else if (r.mode == LockMode::EXCLUSIVE && !any_granted) {
            r.granted          = true;
            any_granted        = true;
            exclusive_granted  = true;
            break;   // exclusive: stop — nothing else can be granted
        } else {
            break;   // FIFO: cannot skip a blocked request
        }
    }
}

// ─── AcquireLock (core) ───────────────────────────────────────────────────────

void LockManager::AcquireLock(Transaction *txn, RID rid, LockMode mode) {
    if (txn->GetState() != TxnState::GROWING)
        throw std::logic_error("2PL violation: lock acquisition outside GROWING phase");

    std::unique_lock<std::mutex> lock(latch_);

    LockRequestQueue &queue = GetOrCreateQueue(rid);

    // Append our request (not yet granted).
    queue.requests.push_back({txn->GetTxnId(), mode, false});
    // std::list gives stable addresses — this reference remains valid
    // even if other elements are added or removed.
    LockRequest &request = queue.requests.back();

    // Can we grant immediately?
    bool immediate = (mode == LockMode::SHARED)
                   ? CanGrantShared(queue, txn->GetTxnId())
                   : CanGrantExclusive(queue, txn->GetTxnId());
    if (immediate) {
        request.granted = true;
    } else {
        // Wait up to LOCK_TIMEOUT_MS ms.  The condition variable releases
        // latch_ atomically while sleeping, re-acquires on wake-up.
        bool granted = queue.cv.wait_for(
            lock,
            std::chrono::milliseconds(LOCK_TIMEOUT_MS),
            [&request]() { return request.granted; });

        if (!granted) {
            // Timeout — remove our (ungranted) request and abort.
            queue.requests.remove_if(
                [id = txn->GetTxnId()](const LockRequest &r) { return r.txn_id == id; });
            txn->SetState(TxnState::ABORTED);
            throw TransactionAbortException(txn->GetTxnId());
        }
    }

    // Lock granted — record in the transaction for later release.
    if (mode == LockMode::SHARED) txn->AddSharedLock(rid);
    else                          txn->AddExclusiveLock(rid);
}

void LockManager::AcquireSharedLock   (Transaction *txn, RID rid) { AcquireLock(txn, rid, LockMode::SHARED); }
void LockManager::AcquireExclusiveLock(Transaction *txn, RID rid) { AcquireLock(txn, rid, LockMode::EXCLUSIVE); }

// ─── ReleaseLocks ─────────────────────────────────────────────────────────────

void LockManager::ReleaseLocks(Transaction *txn) {
    std::unique_lock<std::mutex> lock(latch_);

    auto release_set = [&](const std::vector<RID> &rid_set) {
        for (const RID &rid : rid_set) {
            auto it = lock_table_.find(rid);
            if (it == lock_table_.end()) continue;

            LockRequestQueue &queue = *it->second;
            queue.requests.remove_if(
                [id = txn->GetTxnId()](const LockRequest &r) { return r.txn_id == id; });

            GrantPending(queue);
            queue.cv.notify_all();

            if (queue.requests.empty())
                lock_table_.erase(it);
        }
    };

    release_set(txn->GetSharedLockSet());
    release_set(txn->GetExclusiveLockSet());
}
