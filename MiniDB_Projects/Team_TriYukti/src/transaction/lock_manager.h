#pragma once
#include "transaction/transaction.h"
#include <mutex>
#include <unordered_map>
#include <vector>
#include <condition_variable>
#include <string>
#include <thread>
#include <atomic>

namespace minidb {

enum class LockMode { SHARED, EXCLUSIVE };

class LockManager {
public:
    LockManager();
    ~LockManager();

    bool LockShared(Transaction *txn, const std::string &resource_id);
    bool LockExclusive(Transaction *txn, const std::string &resource_id);
    bool Unlock(Transaction *txn, const std::string &resource_id);

    // Call periodically to detect deadlocks
    void RunCycleDetection();
    
    void AddTransaction(Transaction *txn);
    void RemoveTransaction(txn_id_t txn_id);
    
    
    std::vector<txn_id_t> GetActiveTransactions() const {
        std::vector<txn_id_t> active;
        for (const auto &pair : waits_for_) {
            active.push_back(pair.first);
        }
        return active;
    }

private:
    std::unordered_map<txn_id_t, Transaction*> txn_map_;
    struct LockRequest {
        txn_id_t txn_id;
        LockMode lock_mode;
        bool granted;
    };

    struct LockRequestQueue {
        std::vector<LockRequest> requests;
        std::condition_variable cv;
    };

    std::mutex latch_;
    std::unordered_map<std::string, LockRequestQueue> lock_table_;
    
    std::thread *deadlock_detection_thread_;
    std::atomic<bool> enable_cycle_detection_;
    std::unordered_map<txn_id_t, std::vector<txn_id_t>> waits_for_;
    
    bool HasCycle(txn_id_t *youngest_txn_id);
    bool Dfs(txn_id_t txn_id, std::unordered_map<txn_id_t, int> &visited, std::vector<txn_id_t> &path, txn_id_t *youngest_txn_id);
    void BuildWaitsForGraph();
    
    void BackgroundCycleDetection();
    
    bool CanGrantLock(LockRequestQueue &queue, LockMode mode, txn_id_t txn_id);
};

} // namespace minidb
