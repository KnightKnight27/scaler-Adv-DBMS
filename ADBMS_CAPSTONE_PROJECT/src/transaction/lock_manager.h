#pragma once
#include "transaction/transaction.h"
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <list>
#include <memory>
#include <vector>

namespace minidb {

enum class LockMode { SHARED, EXCLUSIVE };

class LockRequest {
public:
    int txn_id_;
    LockMode lock_mode_;
    bool granted_;
    
    LockRequest(int txn_id, LockMode lock_mode)
        : txn_id_(txn_id), lock_mode_(lock_mode), granted_(false) {}
};

class LockRequestQueue {
public:
    std::list<std::shared_ptr<LockRequest>> request_queue_;
    std::condition_variable cv_;
    bool is_exclusive_locked_{false};
    int shared_lock_count_{0};
};

class LockManager {
public:
    LockManager() = default;

    // Acquire shared lock. Returns true if granted, false if deadlock timeout.
    bool LockShared(Transaction* txn, const RecordId& rid);

    // Acquire exclusive lock. Returns true if granted, false if deadlock timeout.
    bool LockExclusive(Transaction* txn, const RecordId& rid);

    // Release lock
    bool Unlock(Transaction* txn, const RecordId& rid);

    // Run DFS cycle detection on the Wait-For Graph. Returns true if cycle found.
    bool DetectDeadlock(int* aborted_txn_id);

private:
    std::mutex latch_;
    std::unordered_map<RecordId, LockRequestQueue, RecordIdHash> lock_table_;
    
    // Wait-For Graph: maps TxnId -> list of TxnIds it is waiting for
    std::unordered_map<int, std::list<int>> waits_for_;

    // Helpers for Wait-For Graph
    void AddEdge(int t1, int t2);
    void RemoveEdge(int t1, int t2);
    bool Dfs(int txn_id, std::unordered_map<int, int>& visited, std::vector<int>& stack, int* cycle_txn_id);
    
    // Constant for timeout-based deadlock detection (kept for fallback)
    const int DEADLOCK_TIMEOUT_MS = 2000; 
};

} // namespace minidb
