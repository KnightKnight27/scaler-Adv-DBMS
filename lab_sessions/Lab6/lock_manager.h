#ifndef LOCK_MANAGER_H
#define LOCK_MANAGER_H

#include "common.h"
#include "tx_registry.h"
#include <condition_variable>
#include <list>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

enum class LockMode { SHARED, EXCLUSIVE };

struct LockRequest {
    TxID     xid;
    LockMode mode;
    bool     granted = false;
};

struct LockQueue {
    std::list<LockRequest>  requests;
    std::mutex              mu;
    std::condition_variable cv;
};

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid)
        : std::runtime_error("Deadlock detected, aborting tx " + std::to_string(xid)) {}
};

// Strict two-phase locking with deadlock detection via a waits-for graph.
class LockManager {
public:
    explicit LockManager(TransactionRegistry& registry) : registry_(registry) {}

    // Blocks until the lock is granted. Throws DeadlockException if granting
    // would (or does) create a cycle in the waits-for graph.
    void acquire(const RowKey& key, TxID xid, LockMode mode);

    // Releases every lock held by the transaction and wakes any waiters.
    void release(TxID xid);

private:
    // DFS for a cycle starting at `start` over the waits-for graph.
    // Caller must hold mu_.
    bool has_cycle(TxID start) const;

    TransactionRegistry&                                       registry_;
    std::mutex                                                 mu_;          // guards lock_table_ and waits_for_
    std::unordered_map<RowKey, LockQueue>                      lock_table_;
    std::unordered_map<TxID, std::unordered_set<TxID>>         waits_for_;
};

#endif
