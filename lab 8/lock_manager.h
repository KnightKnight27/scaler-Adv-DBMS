#pragma once
#include "tx_types.h"
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <mutex>
#include <condition_variable>
#include <stdexcept>

enum class LockMode { SHARED, EXCLUSIVE };

struct LockRequest {
    TxID xid;
    LockMode mode;
    bool granted = false;
};

struct LockQueue {
    std::list<LockRequest> requests;
    std::mutex mu;
    std::condition_variable cv;
};

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid)
        : std::runtime_error("Deadlock detected, aborting transaction " + std::to_string(xid)) {}
};

class LockManager {
public:
    static LockManager& getInstance();

    void acquireLock(const RowKey& key, TxID xid, LockMode mode);
    void releaseLocks(TxID xid);

private:
    LockManager() = default;
    ~LockManager() = default;
    LockManager(const LockManager&) = delete;
    LockManager& operator=(const LockManager&) = delete;

    bool hasCycle(TxID start, const std::unordered_map<TxID, std::unordered_set<TxID>>& graph);

    std::mutex lm_mutex;
    std::unordered_map<RowKey, LockQueue> lock_table;
    std::unordered_map<TxID, std::unordered_set<TxID>> waits_for;
};
