// Lock manager for strict two-phase locking (2PL).
//
// Resources are identified by an opaque LockId (MiniDB locks at table
// granularity, so LockId == table_id). Two modes are supported: SHARED (read)
// and EXCLUSIVE (write). Locks are held until commit/abort (the "strict" in
// strict 2PL), which yields serializable isolation.
//
// Blocking is implemented with a per-resource condition variable. Before a
// request waits, the manager builds the global wait-for graph and runs cycle
// detection; if granting the wait would close a cycle, the requesting
// transaction is chosen as the deadlock victim and a DeadlockError is thrown.
#pragma once

#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "common/types.hpp"

namespace minidb {

using LockId = int64_t;

enum class LockMode { SHARED, EXCLUSIVE };

struct DeadlockError : std::runtime_error {
    explicit DeadlockError(TxnId t)
        : std::runtime_error("deadlock detected, victim txn " + std::to_string(t)),
          victim(t) {}
    TxnId victim;
};

class LockManager {
public:
    // Acquire `mode` on `id` for `txn`; blocks until granted. Throws
    // DeadlockError if waiting would deadlock (txn must then abort).
    void acquire(TxnId txn, LockId id, LockMode mode);

    // Release every lock held by txn (called at commit/abort).
    void release_all(TxnId txn);

private:
    struct Request {
        TxnId    txn;
        LockMode mode;
        bool     granted;
    };
    struct LockEntry {
        std::list<Request> queue;
        std::condition_variable cv;
    };

    static bool compatible(LockMode a, LockMode b) {
        return a == LockMode::SHARED && b == LockMode::SHARED;
    }
    bool can_grant(LockEntry& e, const Request& req);
    bool has_deadlock(TxnId requester);
    bool dfs_cycle(TxnId start, TxnId at, std::unordered_set<TxnId>& visited);

    std::mutex latch_;
    std::map<LockId, LockEntry> table_;
    // txn -> set of lock ids it currently holds (for release_all).
    std::unordered_map<TxnId, std::unordered_set<LockId>> held_;
};

}  // namespace minidb
