#ifndef LOCK_MANAGER_H
#define LOCK_MANAGER_H

#include "common/config.h"
#include "common/rid.h"
#include "concurrency/transaction.h"
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <list>

namespace minidb {

enum class LockMode { SHARED, EXCLUSIVE };

struct LockRequest {
    txn_id_t txn_id;
    LockMode lock_mode;
    bool is_granted;
};

struct LockHead {
    std::list<LockRequest> request_queue;
    std::condition_variable cv;
};

// Custom hash function for RID to allow use in std::unordered_map
struct RIDHash {
    std::size_t operator()(const RID& rid) const {
        return std::hash<page_id_t>{}(rid.GetPageId()) ^ (std::hash<uint32_t>{}(rid.GetSlotNum()) << 1);
    }
};

class LockManager {
public:
    LockManager() = default;
    ~LockManager() = default;

    // Acquires a Shared lock on rid for txn. Blocks if incompatible. Returns true if granted.
    bool LockShared(Transaction* txn, const RID& rid);

    // Acquires an Exclusive lock on rid for txn. Blocks if incompatible. Returns true if granted.
    bool LockExclusive(Transaction* txn, const RID& rid);

    // Releases all locks held by the transaction
    void ReleaseLocks(Transaction* txn);

private:
    // Helper to check lock compatibility
    bool IsCompatible(const RID& rid, LockMode mode, txn_id_t txn_id);

    std::mutex latch_;
    std::unordered_map<RID, LockHead, RIDHash> lock_table_;
};

} // namespace minidb

#endif // LOCK_MANAGER_H
