#include "transaction/lock_manager.h"

namespace minidb {

bool LockManager::CanGrant(const LockEntry& entry, LockMode mode, int txn_id) const {
    if (entry.holder == -1) return true;
    if (entry.holder == txn_id) return true;
    if (entry.granted == LockMode::SHARED && mode == LockMode::SHARED) return true;
    return false;
}

void LockManager::Grant(LockEntry& entry, int txn_id, LockMode mode) {
    if (entry.holder == -1) {
        entry.holder = txn_id;
        entry.granted = mode;
    } else if (entry.granted == LockMode::SHARED && mode == LockMode::EXCLUSIVE) {
        entry.granted = LockMode::EXCLUSIVE;
    }
    txn_locks_[txn_id].push_back(entry.resource);
}

bool LockManager::Lock(const std::string& resource, LockMode mode, int txn_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto& entry = locks_[resource];
    entry.resource = resource;

    if (CanGrant(entry, mode, txn_id)) {
        Grant(entry, txn_id, mode);
        last_failure_deadlock_ = false;
        return true;
    }

    waits_on_[txn_id] = entry.holder;
    if (HasDeadlock(txn_id)) {
        waits_on_.erase(txn_id);
        last_failure_deadlock_ = true;
        return false;
    }
    last_failure_deadlock_ = false;
    return false;
}

void LockManager::UnlockAll(int txn_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = txn_locks_.find(txn_id);
    if (it == txn_locks_.end()) return;
    for (const auto& resource : it->second) {
        locks_.erase(resource);
    }
    txn_locks_.erase(txn_id);
    waits_on_.erase(txn_id);
}

bool LockManager::HasDeadlock(int txn_id) const {
    int current = txn_id;
    for (int i = 0; i < 32; ++i) {
        auto it = waits_on_.find(current);
        if (it == waits_on_.end()) return false;
        if (it->second == txn_id) return true;
        current = it->second;
    }
    return false;
}

std::string LockManager::DeadlockVictim() const {
    if (waits_on_.empty()) return "";
    return std::to_string(waits_on_.begin()->first);
}

}  // namespace minidb
