#include "lock_manager.h"

bool LockManager::isConflict(const Lock &existing, const Lock &new_lock) {
    if (existing.tx_id == new_lock.tx_id) return false;
    if (existing.lock_type == LockType::EXCLUSIVE) return true;
    if (new_lock.lock_type == LockType::EXCLUSIVE) return true;
    return false;
}

bool LockManager::canGrantLock(const std::string &resource, const Lock &new_lock) {
    auto it = lock_table.find(resource);
    if (it == lock_table.end()) return true;

    const auto &queue = it->second;

    for (const auto &lock : queue.granted_locks) {
        if (isConflict(lock, new_lock)) {
            return false;
        }
    }

    if (!queue.waiting_locks.empty()) {
        return false;
    }

    return true;
}

bool LockManager::acquireLock(const std::string &resource, TransactionID tx_id, LockType lock_type) {
    std::lock_guard<std::mutex> lock(manager_mutex);

    Lock new_lock(tx_id, lock_type);

    if (canGrantLock(resource, new_lock)) {
        lock_table[resource].granted_locks.push_back(new_lock);
        return true;
    }

    lock_table[resource].waiting_locks.push_back(new_lock);
    return false;
}

void LockManager::releaseLock(const std::string &resource, TransactionID tx_id) {
    std::lock_guard<std::mutex> lock(manager_mutex);

    auto it = lock_table.find(resource);
    if (it == lock_table.end()) return;

    auto &queue = it->second;

    auto &granted = queue.granted_locks;
    auto grant_it = std::find_if(granted.begin(), granted.end(),
                                 [tx_id](const Lock &l) { return l.tx_id == tx_id; });
    if (grant_it != granted.end()) {
        granted.erase(grant_it);
    }

    auto &waiting = queue.waiting_locks;
    auto wait_it = std::find_if(waiting.begin(), waiting.end(),
                                [tx_id](const Lock &l) { return l.tx_id == tx_id; });
    if (wait_it != waiting.end()) {
        waiting.erase(wait_it);
    }
}

void LockManager::releaseAllLocks(TransactionID tx_id) {
    std::lock_guard<std::mutex> lock(manager_mutex);

    for (auto &pair : lock_table) {
        auto &queue = pair.second;

        auto &granted = queue.granted_locks;
        granted.erase(std::remove_if(granted.begin(), granted.end(),
                                     [tx_id](const Lock &l) { return l.tx_id == tx_id; }),
                      granted.end());

        auto &waiting = queue.waiting_locks;
        waiting.erase(std::remove_if(waiting.begin(), waiting.end(),
                                     [tx_id](const Lock &l) { return l.tx_id == tx_id; }),
                      waiting.end());
    }
}

bool LockManager::isHeldBy(const std::string &resource, TransactionID tx_id) {
    std::lock_guard<std::mutex> lock(manager_mutex);

    auto it = lock_table.find(resource);
    if (it == lock_table.end()) return false;

    const auto &granted = it->second.granted_locks;
    return std::find_if(granted.begin(), granted.end(),
                       [tx_id](const Lock &l) { return l.tx_id == tx_id; }) != granted.end();
}
