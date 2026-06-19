#pragma once

#include "common/config.h"
#include "common/types.h"
#include "concurrency/transaction.h"
#include <unordered_map>
#include <list>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <unordered_set>

namespace minidb {

class LockManager {
public:
    LockManager();
    ~LockManager();

    // Acquire a Shared lock on RID
    bool AcquireShared(Transaction *txn, const RID &rid);

    // Acquire an Exclusive lock on RID
    bool AcquireExclusive(Transaction *txn, const RID &rid);

    // Release lock on RID
    bool Release(Transaction *txn, const RID &rid);

    // Release all locks held by a transaction (Strict 2PL)
    void ReleaseAllLocks(Transaction *txn);

    // Enable/disable deadlock detection thread
    void StartDeadlockDetection();
    void StopDeadlockDetection();

private:
    struct LockRequest {
        Transaction *txn;
        txn_id_t txn_id;
        LockMode lock_mode;
        bool is_granted{false};
    };

    struct LockHead {
        std::list<LockRequest> request_queue;
        std::condition_variable cv;
    };

    // Helper for cycle detection
    bool DFS(txn_id_t curr, std::unordered_map<txn_id_t, std::vector<txn_id_t>> &adj,
             std::unordered_set<txn_id_t> &visited, std::unordered_set<txn_id_t> &rec_stack,
             txn_id_t &victim);

    void RunCycleDetection();

    std::unordered_map<RID, LockHead> lock_table_;
    std::mutex latch_;
    
    std::thread detection_thread_;
    bool run_detection_{false};
};

} // namespace minidb
