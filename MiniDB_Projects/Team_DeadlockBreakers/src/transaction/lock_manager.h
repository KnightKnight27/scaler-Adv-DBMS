#pragma once

#include "common/types.h"
#include "transaction/transaction.h"

#include <condition_variable>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace minidb {

/*
 * LockManager: implements Strict Two-Phase Locking (2PL).
 *
 * Lock granularity: table-level (SHARED / EXCLUSIVE).
 * Strict 2PL: locks are only released on commit or abort.
 * Deadlock detection: timeout-based (wait up to 1 second, then abort).
 */
class LockManager {
public:
    LockManager() = default;

    // Acquire a shared lock on a resource. Returns false on deadlock/timeout.
    bool LockShared(Transaction* txn, const std::string& resource);

    // Acquire an exclusive lock on a resource. Returns false on deadlock/timeout.
    bool LockExclusive(Transaction* txn, const std::string& resource);

    // Release all locks held by this transaction
    void UnlockAll(Transaction* txn);

private:
    struct LockRequest {
        txn_id_t  txn_id;
        LockMode  mode;
        bool      granted = false;
    };

    struct LockQueue {
        std::list<LockRequest>  requests;
        std::condition_variable cv;
    };

    std::mutex                                    mutex_;
    std::unordered_map<std::string, LockQueue>    lock_table_;

    bool TryGrant(LockQueue& queue, const LockRequest& req);
    void GrantWaiters(LockQueue& queue);
};

} // namespace minidb
