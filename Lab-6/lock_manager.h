#pragma once
#ifndef LOCK_MANAGER_H
#define LOCK_MANAGER_H

#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

using TransactionID = int;

enum class LockType { SHARED, EXCLUSIVE };

struct Lock {
    TransactionID tx_id;
    LockType lock_type;
    Lock(TransactionID id, LockType type) : tx_id(id), lock_type(type) {}
};

class LockManager {
private:
    struct LockQueue {
        std::deque<Lock> granted_locks;
        std::deque<Lock> waiting_locks;
    };

    std::unordered_map<std::string, LockQueue> lock_table;
    std::mutex manager_mutex;

    bool canGrantLock(const std::string &resource, const Lock &new_lock);
    bool isConflict(const Lock &existing, const Lock &new_lock);

public:
    LockManager() = default;

    bool acquireLock(const std::string &resource, TransactionID tx_id, LockType lock_type);
    void releaseLock(const std::string &resource, TransactionID tx_id);
    void releaseAllLocks(TransactionID tx_id);
    bool isHeldBy(const std::string &resource, TransactionID tx_id);

    friend class DeadlockDetector;
    const std::unordered_map<std::string, LockQueue>& getLockTable() const { return lock_table; }
};

#endif // LOCK_MANAGER_H
