#pragma once
#include "txn/transaction.h"
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <vector>

namespace minidb {

enum class LockMode { SHARED, EXCLUSIVE };

struct LockRequest {
    int txn_id;
    LockMode mode;
    bool granted = false;
};

class LockManager {
public:
    bool lock_shared(Transaction* txn, RecordId rid);
    bool lock_exclusive(Transaction* txn, RecordId rid);
    bool unlock(Transaction* txn, RecordId rid);

    std::vector<std::pair<int, int>> get_wait_for_graph();
    void abort_txn(Transaction* txn);

private:
    std::mutex latch_;
    std::unordered_map<RecordId, std::vector<LockRequest>> lock_table_;
    std::unordered_map<RecordId, std::condition_variable> cvs_;
    
    bool can_grant_lock(const std::vector<LockRequest>& requests, int txn_id, LockMode mode);
};

} // namespace minidb
