#include "transaction/LockManager.hpp"

using namespace std;

namespace minidb {

bool LockManager::isCompatible(LockMode held, LockType requested) {
    if (held == LockMode::NONE) return true;
    if (held == LockMode::SHARED && requested == LockType::SHARED) return true;
    return false;
}

void LockManager::grantLock(int txn_id, const string& resource, LockType type) {
    LockEntry& entry = locks_[resource];
    entry.holders.insert(txn_id);
    txn_locks_[txn_id].insert(resource);
    if (type == LockType::EXCLUSIVE) entry.mode = LockMode::EXCLUSIVE;
    else if (entry.mode != LockMode::EXCLUSIVE) entry.mode = LockMode::SHARED;
}

bool LockManager::acquireLock(int txn_id, const string& resource, LockType type) {
    lock_guard<mutex> guard(mutex_);
    LockEntry& entry = locks_[resource];
    if (entry.holders.count(txn_id)) {
        if (type == LockType::EXCLUSIVE && entry.mode == LockMode::SHARED && entry.holders.size() == 1)
            entry.mode = LockMode::EXCLUSIVE;
        return true;
    }
    if (isCompatible(entry.mode, type)) { grantLock(txn_id, resource, type); return true; }
    if (!entry.holders.empty()) {
        waits_on_[txn_id] = *entry.holders.begin();
        if (detectDeadlock(txn_id)) { waits_on_.erase(txn_id); return false; }
    }
    waits_on_.erase(txn_id);
    return false;
}

void LockManager::releaseAllLocks(int txn_id) {
    lock_guard<mutex> guard(mutex_);
    auto it = txn_locks_.find(txn_id);
    if (it == txn_locks_.end()) return;
    for (const string& resource : it->second) {
        LockEntry& entry = locks_[resource];
        entry.holders.erase(txn_id);
        if (entry.holders.empty()) entry.mode = LockMode::NONE;
    }
    txn_locks_.erase(txn_id);
    waits_on_.erase(txn_id);
}

bool LockManager::detectDeadlock(int waiting_txn) {
    set<int> visited;
    int current = waiting_txn;
    while (waits_on_.count(current)) {
        if (visited.count(current)) return true;
        visited.insert(current);
        current = waits_on_[current];
        if (current == waiting_txn) return true;
    }
    return false;
}

}  // namespace minidb
