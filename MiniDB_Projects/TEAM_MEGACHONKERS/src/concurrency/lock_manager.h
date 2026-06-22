#pragma once

#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include "concurrency/transaction.h"

namespace minidb {

class LockManager {
private:
    // Tracks the state of a specific row's lock
    struct LockRequestQueue {
        std::condition_variable cv_;
        bool is_exclusive_active_{false};
        int shared_count_{0};
        
        // In a full implementation, we'd queue actual LockRequest objects here 
        // to ensure fairness, but tracking counts is sufficient for our prototype.
    };

    std::unordered_map<std::string, LockRequestQueue> lock_table_;
    std::mutex latch_; // Protects the lock_table_ itself

public:
    LockManager() = default;

    // Called by SeqScanExecutor before reading a row
    bool LockShared(Transaction* txn, const std::string& lock_key);

    // Called by InsertExecutor before writing a row
    bool LockExclusive(Transaction* txn, const std::string& lock_key);

    // Called during COMMIT or ABORT
    bool Unlock(Transaction* txn, const std::string& lock_key);
};

} // namespace minidb