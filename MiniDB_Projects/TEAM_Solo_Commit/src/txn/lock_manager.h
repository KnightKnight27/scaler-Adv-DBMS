// MiniDB - LockManager: row-level Shared/Exclusive locks with waits-for-graph deadlock
// detection. This is the transaction manager I wrote in Lab 6, recast onto RID keys and an
// S/X lock matrix, and used here to enforce Strict 2PL (locks held until commit/abort).
//
// On a conflict the requester records "waits-for" edges to the holders; before blocking it
// runs a DFS cycle check, and if a cycle exists it backs off (returns false) so the caller can
// abort and break the deadlock. Releasing a transaction's locks wakes every waiter.
#pragma once

#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace minidb {

enum class LockMode { SHARED, EXCLUSIVE };

class LockManager {
public:
    // Acquire `mode` on `rid_key` for transaction `txn`. Blocks until granted; returns false
    // if granting would deadlock (the caller must then abort `txn`).
    bool Acquire(int txn, int64_t rid_key, LockMode mode);

    // Release every lock held by `txn` (Strict 2PL: called at commit/abort) and wake waiters.
    void ReleaseAll(int txn);

private:
    struct Request {
        int txn;
        LockMode mode;
        bool granted;
    };

    static bool Conflicts(LockMode a, LockMode b) {
        return a == LockMode::EXCLUSIVE || b == LockMode::EXCLUSIVE;
    }
    bool CanGrant(int64_t rid_key, int txn, LockMode mode);
    bool HasDeadlock();
    bool DfsCycle(int u, std::unordered_set<int>& visited, std::unordered_set<int>& stack);

    std::mutex mtx_;
    std::condition_variable cv_;
    std::unordered_map<int64_t, std::vector<Request>> table_;     // rid_key -> requests
    std::unordered_map<int, std::unordered_set<int>> waits_for_;  // txn -> txns it waits on
};

}  // namespace minidb
