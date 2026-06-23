#pragma once

#include <condition_variable>
#include <cstdint>
#include <list>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace minidb {

using TxID = uint64_t;
using RowKey = std::string;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

enum class LockMode { SHARED, EXCLUSIVE };

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid)
        : std::runtime_error("Deadlock detected, aborting tx " + std::to_string(xid)),
          victim_xid_(xid) {}

    TxID VictimXid() const { return victim_xid_; }

private:
    TxID victim_xid_;
};

class LockManager {
public:
    LockManager() = default;

    LockManager(const LockManager&) = delete;
    LockManager& operator=(const LockManager&) = delete;

    void AcquireLock(const RowKey& key, TxID xid, LockMode mode, bool in_shrinking);
    void ReleaseLocks(TxID xid);
    void MarkShrinking(TxID xid);

private:
    struct LockRequest {
        TxID xid = 0;
        LockMode mode = LockMode::SHARED;
        bool granted = false;
    };

    struct LockQueue {
        std::list<LockRequest> requests;
        std::mutex mu;
        std::condition_variable cv;
    };

    bool HasCycle(TxID start, const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) const;
    TxID FindYoungestInCycle(TxID start,
                             const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) const;
    void AbortDeadlockVictim(TxID victim);

    std::mutex lock_table_mutex_;
    std::unordered_map<RowKey, LockQueue> lock_table_;
    std::unordered_map<TxID, std::unordered_set<RowKey>> tx_locks_;

    std::mutex waits_for_mutex_;
    std::unordered_map<TxID, std::unordered_set<TxID>> waits_for_;
    std::unordered_set<TxID> aborted_txs_;
};

}  // namespace minidb
