#pragma once

#include "common/types.h"
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <map>

// ============================================================
// LockManager — row-level locking with wait-for graph deadlock detection
//
// Supports SHARED (read) and EXCLUSIVE (write) locks.
// Strict 2PL: all locks held until txn commits/aborts.
// Deadlock detection: DFS on wait-for graph before blocking.
// ============================================================

enum class LockMode { SHARED, EXCLUSIVE };

struct LockRequest {
    int txn_id;
    LockMode mode;
    bool granted = false;
};

class LockManager {
public:
    // Acquire a shared lock. Returns true if granted, false if txn was
    // chosen as deadlock victim and should abort.
    bool LockShared(int txn_id, const RID& rid);

    // Acquire an exclusive lock. Returns true if granted, false if deadlock victim.
    bool LockExclusive(int txn_id, const RID& rid);

    // Release ALL locks held by this txn (called at commit/abort).
    void UnlockAll(int txn_id);

    // Get the lock mode held by txn on rid (for debugging/demo).
    // Returns "" if no lock held.
    std::string GetLockInfo(int txn_id, const RID& rid);

    // Get all lock holders for a RID
    std::vector<int> GetHolders(const RID& rid);

private:
    // Key for the lock table: combine page_id and slot_id into one uint64
    struct RIDHash {
        size_t operator()(const RID& r) const {
            return std::hash<uint64_t>()(((uint64_t)r.page_id << 16) | r.slot_id);
        }
    };
    struct RIDEqual {
        bool operator()(const RID& a, const RID& b) const { return a == b; }
    };

    struct LockEntry {
        std::vector<LockRequest> requests;   // granted + waiting
        std::condition_variable cv;          // waiters block here
    };

    std::mutex mu_;
    std::unordered_map<RID, LockEntry, RIDHash, RIDEqual> lock_table_;

    // Track which RIDs each txn holds locks on (for UnlockAll)
    std::unordered_map<int, std::vector<RID>> txn_locks_;

    // Wait-for graph: txn_id → set of txn_ids it's waiting for
    std::unordered_map<int, std::unordered_set<int>> wait_for_;

    // Check for deadlock cycle starting from txn_id using DFS
    bool HasCycle(int txn_id);
    bool DFS(int node, std::unordered_set<int>& visited, std::unordered_set<int>& in_stack);

    // Internal: try to grant a lock request. Returns true if granted immediately.
    bool TryGrant(LockEntry& entry, int txn_id, LockMode mode);
};
