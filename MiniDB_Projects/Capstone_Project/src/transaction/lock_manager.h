#pragma once

#include "common/types.h"
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <string>
#include <functional>
#include <stdexcept>

/**
 * @enum LockMode
 * @brief Specifies the concurrency lock level for row resources.
 */
enum class LockMode {
    SHARED,     ///< Read lock: shared with other S locks, blocks X locks
    EXCLUSIVE   ///< Write lock: exclusive access, blocks all S and X locks
};

/**
 * @class DeadlockException
 * @brief Raised when waits-for dependency checks detect cycle loops.
 */
class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid)
        : std::runtime_error("Deadlock detected — aborting transaction " + std::to_string(xid)),
          victim_xid(xid) {}
    
    TxID victim_xid;
};

/**
 * @class LockManager
 * @brief Handles lock acquisition and releases conforming to Strict 2PL protocol rules.
 */
class LockManager {
public:
    LockManager() = default;
    ~LockManager();

    // Disable copying for thread-safe managers
    LockManager(const LockManager&) = delete;
    LockManager& operator=(const LockManager&) = delete;

    /**
     * @brief Requests a row lock on resource_key for transaction xid.
     * Blocks if blocked by conflicts. Raises DeadlockException on cycle discovery.
     */
    void acquireLock(TxID xid, const std::string& resource_key, LockMode mode);

    /**
     * @brief Releases all S and X locks held by transaction xid.
     */
    void releaseAll(TxID xid);

    /**
     * @brief Moves transaction state to the shrinking phase where no new locks can be acquired.
     */
    void beginShrinking(TxID xid);

    /**
     * @brief Outputs lock table status for diagnostic purposes.
     */
    void printLockTable() const;

private:
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

    bool hasConflict(const LockQueue& lq, TxID xid, LockMode mode) const;

    bool hasCycle(TxID start,
                  const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) const;

    LockQueue& getQueue(const std::string& key);

    std::unordered_map<std::string, LockQueue*> lock_table_;
    mutable std::mutex lt_mutex_; ///< Protects lock_table_

    std::unordered_map<TxID, std::unordered_set<TxID>> waits_for_;
    mutable std::mutex wf_mutex_; ///< Protects waits_for_ graph

    std::unordered_set<TxID> shrinking_;
    mutable std::mutex shrink_mutex_; ///< Protects shrinking_ transaction set
};
