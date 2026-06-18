#ifndef LOCK_MANAGER_H
#define LOCK_MANAGER_H

#include "common.h"
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <stdexcept>

enum class LockMode { SHARED, EXCLUSIVE };

struct LockRequest {
    TxID     xid;
    LockMode mode;
    bool     granted = false;
};

struct LockQueue {
    std::list<LockRequest> requests;
    std::mutex             mu;
    std::condition_variable cv;
};

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid)
        : std::runtime_error("Deadlock detected, aborting tx " + std::to_string(xid)) {}
};

void acquire_lock(const RowKey& key, TxID xid, LockMode mode);
void release_locks(TxID xid);

#endif