#pragma once

#include <condition_variable>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "common/types.h"
#include "txn/transaction.h"

namespace minidb {

// A lock manager implementing two-phase locking with SHARED/EXCLUSIVE row locks.
// Locks are keyed by an opaque string (we use "table:key"). acquire() blocks
// until the lock is granted; if waiting would close a cycle in the waits-for
// graph it throws DeadlockException instead (deadlock detection, per lab_6).
//
// Strict 2PL: a transaction holds every lock until commit/abort, then
// release_all() frees them in one shot — so no other transaction can read
// uncommitted data and there are no cascading aborts.
class LockManager {
public:
    // Grant tx a lock on key in the given mode (upgrading S->X if needed).
    // Blocks until granted; throws DeadlockException if it would deadlock.
    void acquire(TxId tx, const std::string& key, LockMode mode);

    // Release every lock held by tx (end of transaction).
    void release_all(TxId tx);

private:
    struct Request {
        TxId     tx;
        LockMode mode;
        bool     granted;
    };

    // Is `mode` compatible with the locks currently granted to OTHER txns on key?
    bool compatible(const std::string& key, TxId tx, LockMode mode);
    // DFS the waits-for graph: does following edges from `start` return to start?
    bool has_cycle(TxId start);

    std::mutex              mu_;
    std::condition_variable cv_;
    std::unordered_map<std::string, std::list<Request>>      table_;      // key -> request queue
    std::unordered_map<TxId, std::unordered_set<TxId>>        waits_for_;  // tx -> txns it waits on
    std::unordered_map<TxId, std::unordered_set<std::string>> held_;       // tx -> keys it holds
};

} // namespace minidb
