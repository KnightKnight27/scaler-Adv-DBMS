#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/types.h"

namespace minidb {

class LockManager {
public:
    bool Lock(const std::string& resource, LockMode mode, int txn_id);
    void UnlockAll(int txn_id);
    bool HasDeadlock(int txn_id) const;
    bool LastFailureWasDeadlock() const { return last_failure_deadlock_; }
    std::string DeadlockVictim() const;

private:
    struct LockEntry {
        std::string resource;
        int holder = -1;
        LockMode granted = LockMode::SHARED;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, LockEntry> locks_;
    std::unordered_map<int, std::vector<std::string>> txn_locks_;
    std::unordered_map<int, int> waits_on_;
    bool last_failure_deadlock_ = false;

    bool CanGrant(const LockEntry& entry, LockMode mode, int txn_id) const;
    void Grant(LockEntry& entry, int txn_id, LockMode mode);
};

}  // namespace minidb
