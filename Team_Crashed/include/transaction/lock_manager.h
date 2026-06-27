// =============================================================================
// include/transaction/lock_manager.h
// -----------------------------------------------------------------------------
// Strict 2PL with deadlock detection via a wait-for graph. See
// include/transaction/README.md.
// =============================================================================
#pragma once

#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/status.h"
#include "common/types.h"
#include "transaction/transaction.h"   // for LockMode

namespace minidb::transaction {

class LockManager {
public:
    LockManager();
    ~LockManager();

    LockManager(const LockManager&)            = delete;
    LockManager& operator=(const LockManager&) = delete;

    // Acquire a lock, BLOCKING the calling thread if the lock is currently
    // held by another transaction in a conflicting mode. Before blocking, the
    // request records its wait edge in the wait-for graph and runs a cycle
    // check: if granting would form a cycle, the request is refused with
    // Status::DEADLOCK (the caller is expected to abort the transaction,
    // breaking the cycle). Otherwise the thread parks on cv_ until a release
    // wakes it, then re-checks. This is real blocking + real deadlock
    // detection, replacing the v1 "report DEADLOCK on any conflict" stub.
    Status acquireShared   (TransactionId txn, RecordId rid);
    Status acquireExclusive(TransactionId txn, RecordId rid);
    void   releaseAll      (TransactionId txn);   // 2PL: only at commit/abort

    // True iff the wait-for graph currently has a cycle. Exposed for
    // tests and for the demo.
    bool   hasCycle();

    // True iff `txn` currently holds any lock (S or X) on `rid`. Exposed so
    // the executor-level 2PL test can assert that a TWO_PL write actually
    // acquired (and, after commit, released) its lock — deterministically,
    // without spawning threads.
    bool   holdsLock(TransactionId txn, RecordId rid) const;

private:
    // rid -> set of (txn, mode)
    std::unordered_map<RecordId,
        std::unordered_map<TransactionId, LockMode>> holders_;
    // rid -> list of (txn) blocked on it
    std::unordered_map<RecordId, std::vector<TransactionId>> waiters_;
    // txn -> txn (the txn it's waiting on)
    std::unordered_map<TransactionId, TransactionId> waitsFor_;
    mutable std::mutex     mu_;    // mutable so the const test inspector holdsLock() can lock it
    std::condition_variable cv_;   // all waiters park here; releaseAll wakes
};

} // namespace minidb::transaction