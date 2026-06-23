#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// lock_manager.h  –  Strict Two-Phase Locking (S2PL)
// ─────────────────────────────────────────────────────────────────────────────
#include "common/types.h"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <condition_variable>

namespace minidb {

enum class LockMode { SHARED, EXCLUSIVE };

struct LockEntry {
    txn_id_t txn_id;
    LockMode mode;
    bool     granted = false;
};

struct LockQueue {
    std::vector<LockEntry> entries;
    std::condition_variable cv;
    int  shared_count    = 0;
    bool has_exclusive   = false;
};

class LockManager {
public:
    LockManager() = default;

    bool LockShared   (txn_id_t tid, int32_t rid);
    bool LockExclusive(txn_id_t tid, int32_t rid);
    bool Unlock       (txn_id_t tid, int32_t rid);
    void UnlockAll    (txn_id_t tid);

private:
    std::mutex                              latch_;
    std::unordered_map<int32_t, LockQueue>  table_;

    void AddLock(txn_id_t tid, int32_t rid, LockMode mode);
};

} // namespace minidb
