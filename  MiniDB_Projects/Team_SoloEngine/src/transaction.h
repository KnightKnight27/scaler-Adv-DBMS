#pragma once

#include "table.h"   // RID

#include <chrono>
#include <condition_variable>
#include <exception>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

using txn_id_t = int32_t;

enum class TxnState { GROWING, SHRINKING, COMMITTED, ABORTED };
enum class LockMode  { SHARED, EXCLUSIVE };

// ─── Exception thrown when a transaction is aborted due to timeout ────────────

class TransactionAbortException : public std::exception {
public:
    explicit TransactionAbortException(txn_id_t id) : txn_id_(id) {}
    const char *what() const noexcept override { return "TransactionAbortException: lock timeout"; }
    txn_id_t GetTxnId() const { return txn_id_; }
private:
    txn_id_t txn_id_;
};

// ─── Transaction ─────────────────────────────────────────────────────────────

class Transaction {
public:
    explicit Transaction(txn_id_t id) : txn_id_(id) {}

    txn_id_t GetTxnId() const { return txn_id_; }
    TxnState GetState()  const { return state_; }
    void     SetState(TxnState s) { state_ = s; }

    void AddSharedLock   (RID rid) { shared_lock_set_.push_back(rid); }
    void AddExclusiveLock(RID rid) { exclusive_lock_set_.push_back(rid); }

    const std::vector<RID> &GetSharedLockSet()    const { return shared_lock_set_; }
    const std::vector<RID> &GetExclusiveLockSet() const { return exclusive_lock_set_; }

private:
    txn_id_t         txn_id_;
    TxnState         state_{TxnState::GROWING};
    std::vector<RID> shared_lock_set_;
    std::vector<RID> exclusive_lock_set_;
};

// ─── RID hashing (needed by LockManager's unordered_map) ─────────────────────

struct RIDHash {
    size_t operator()(const RID &r) const noexcept {
        return std::hash<int64_t>()(
            (static_cast<int64_t>(static_cast<uint32_t>(r.page_id)) << 32) |
             static_cast<int64_t>(static_cast<uint32_t>(r.slot_num)));
    }
};
struct RIDEqual {
    bool operator()(const RID &a, const RID &b) const noexcept {
        return a.page_id == b.page_id && a.slot_num == b.slot_num;
    }
};

// ─── LockManager (Strict 2PL + timeout-based abort) ──────────────────────────
//
// Pinning analogue for locks:
//   Every AcquireXxx call records the RID in the Transaction's lock set.
//   ReleaseLocks() removes ALL entries from the lock table for that transaction
//   and notifies waiting threads so they can retry granting.

class LockManager {
public:
    static constexpr int LOCK_TIMEOUT_MS = 50;

    // Strict 2PL: locks may only be acquired in GROWING state.
    // If the lock is contested and not granted within LOCK_TIMEOUT_MS ms,
    // the transaction is aborted and TransactionAbortException is thrown.
    void AcquireSharedLock   (Transaction *txn, RID rid);
    void AcquireExclusiveLock(Transaction *txn, RID rid);

    // Release all locks held by `txn`.  Must be called at commit or abort.
    void ReleaseLocks(Transaction *txn);

private:
    struct LockRequest {
        txn_id_t txn_id;
        LockMode mode;
        bool     granted{false};
    };

    // Per-RID queue.  Uses unique_ptr so the embedded condition_variable
    // (which is non-movable) stays at a stable address even as the map grows.
    struct LockRequestQueue {
        std::list<LockRequest>  requests;
        std::condition_variable cv;
    };

    std::mutex latch_;   // guards the entire lock_table_
    std::unordered_map<RID, std::unique_ptr<LockRequestQueue>, RIDHash, RIDEqual> lock_table_;

    LockRequestQueue &GetOrCreateQueue(const RID &rid);

    // Compatibility checks (caller holds latch_).
    // "Other" means any transaction whose txn_id != the given id.
    bool CanGrantShared   (const LockRequestQueue &q, txn_id_t id) const;
    bool CanGrantExclusive(const LockRequestQueue &q, txn_id_t id) const;

    // Try to advance pending (not-yet-granted) requests after a lock is released.
    void GrantPending(LockRequestQueue &q);

    // Common implementation shared by both Acquire methods.
    void AcquireLock(Transaction *txn, RID rid, LockMode mode);
};
