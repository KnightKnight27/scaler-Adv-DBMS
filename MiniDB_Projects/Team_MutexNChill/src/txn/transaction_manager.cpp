#include "transaction_manager.h"

int TransactionManager::begin() {
    int id = next_txn_id++;
    active_txns.insert(id);
    return id;
}

bool TransactionManager::isActive(int txn_id) {
    return active_txns.count(txn_id) > 0;
}

std::string TransactionManager::lockKey(const std::string& table, int row_id) {
    return table + ":" + std::to_string(row_id);
}

// DFS on the waits-for graph to detect cycles.
// We start from 'start' and follow the waits_for edges.
// If we reach 'start' again, there is a cycle (= deadlock).
bool TransactionManager::hasCycle(int start) {
    int cur = start;
    // Follow the chain up to a reasonable depth.
    for (int steps = 0; steps < 100; steps++) {
        auto it = waits_for.find(cur);
        if (it == waits_for.end()) return false; // end of chain, no cycle
        cur = it->second;
        if (cur == start) return true; // cycle found
    }
    return false;
}

void TransactionManager::acquireShared(int txn_id, const std::string& table, int row_id) {
    std::string key = lockKey(table, row_id);
    auto it = lock_table.find(key);

    if (it == lock_table.end()) {
        // No existing lock — grant shared lock immediately.
        LockEntry entry;
        entry.mode = LockMode::SHARED;
        entry.holders.insert(txn_id);
        lock_table[key] = entry;
        held_by[txn_id].insert(key);
        return;
    }

    LockEntry& entry = it->second;
    if (entry.mode == LockMode::SHARED) {
        // Shared lock — another transaction can join.
        entry.holders.insert(txn_id);
        held_by[txn_id].insert(key);
        return;
    }

    // Exclusive lock held by someone else — check for deadlock.
    int blocker = *entry.holders.begin();
    if (blocker == txn_id) {
        // We already hold this lock.
        return;
    }

    waits_for[txn_id] = blocker;
    if (hasCycle(txn_id)) {
        waits_for.erase(txn_id);
        throw DeadlockException(txn_id);
    }
    throw LockConflictException(txn_id, blocker);
}

void TransactionManager::acquireExclusive(int txn_id, const std::string& table, int row_id) {
    std::string key = lockKey(table, row_id);
    auto it = lock_table.find(key);

    if (it == lock_table.end()) {
        // No existing lock — grant exclusive lock immediately.
        LockEntry entry;
        entry.mode = LockMode::EXCLUSIVE;
        entry.holders.insert(txn_id);
        lock_table[key] = entry;
        held_by[txn_id].insert(key);
        return;
    }

    LockEntry& entry = it->second;

    // If we already hold the only lock, it's fine (or we can upgrade).
    if (entry.holders.size() == 1 && entry.holders.count(txn_id)) {
        entry.mode = LockMode::EXCLUSIVE;
        return;
    }

    // Someone else holds a lock — check for deadlock.
    int blocker = -1;
    for (int h : entry.holders) {
        if (h != txn_id) { blocker = h; break; }
    }
    if (blocker == -1) return; // only we hold it

    waits_for[txn_id] = blocker;
    if (hasCycle(txn_id)) {
        waits_for.erase(txn_id);
        throw DeadlockException(txn_id);
    }
    throw LockConflictException(txn_id, blocker);
}

void TransactionManager::registerWait(int txn_id, int blocker_id) {
    waits_for[txn_id] = blocker_id;
}

void TransactionManager::releaseLocks(int txn_id) {
    auto it = held_by.find(txn_id);
    if (it == held_by.end()) return;

    for (const std::string& key : it->second) {
        auto lit = lock_table.find(key);
        if (lit == lock_table.end()) continue;
        lit->second.holders.erase(txn_id);
        if (lit->second.holders.empty()) {
            lock_table.erase(lit);
        }
    }
    held_by.erase(txn_id);
    waits_for.erase(txn_id);
}

void TransactionManager::commit(int txn_id) {
    releaseLocks(txn_id);
    active_txns.erase(txn_id);
}

void TransactionManager::abort(int txn_id) {
    releaseLocks(txn_id);
    active_txns.erase(txn_id);
}
