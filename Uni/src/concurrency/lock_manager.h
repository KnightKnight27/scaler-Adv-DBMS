#pragma once

#include "storage/page.h"
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <thread>
#include <atomic>

enum class TransactionState {
    ACTIVE,
    COMMITTED,
    ABORTED
};

struct Transaction {
    TxId_t txn_id;
    TransactionState state = TransactionState::ACTIVE;
    Lsn_t prev_lsn = 0;
    std::unordered_set<PageId_t> dirty_pages;
    std::vector<RID> held_locks;
};

enum class LockMode {
    SHARED,
    EXCLUSIVE
};

struct LockRequest {
    TxId_t txn_id;
    LockMode lock_mode;
    bool granted = false;
};

struct LockRequestQueue {
    std::list<LockRequest> request_queue;
    std::condition_variable cv;
};

// Hash function for RID
struct RIDHash {
    size_t operator()(const RID& rid) const {
        return (static_cast<size_t>(rid.page_id) << 16) ^ rid.slot_id;
    }
};

class LockManager {
public:
    LockManager();
    ~LockManager();

    bool AcquireShared(Transaction* txn, const RID& rid);
    bool AcquireExclusive(Transaction* txn, const RID& rid);
    bool Release(Transaction* txn, const RID& rid);
    void ReleaseAllLocks(Transaction* txn);

    void StartDeadlockDetector(const std::unordered_map<TxId_t, Transaction*>* txn_map);
    void StopDeadlockDetector();

private:
    void RunDeadlockDetection();
    bool FindCycle(TxId_t curr_txn, std::unordered_map<TxId_t, std::vector<TxId_t>>& adj,
                   std::unordered_set<TxId_t>& visited, std::unordered_set<TxId_t>& rec_stack,
                   std::vector<TxId_t>& cycle);

    std::mutex mutex_;
    std::unordered_map<RID, LockRequestQueue, RIDHash> lock_table_;
    
    std::thread detector_thread_;
    std::atomic<bool> run_detector_{false};
    const std::unordered_map<TxId_t, Transaction*>* txn_map_ = nullptr;
};
