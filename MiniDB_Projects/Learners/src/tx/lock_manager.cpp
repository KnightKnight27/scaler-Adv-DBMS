#include "lock_manager.h"
#include <algorithm>
#include <iostream>

std::string LockManager::make_resource_id(const std::string& table, std::pair<int, int> rid) const {
    return table + "|" + std::to_string(rid.first) + "," + std::to_string(rid.second);
}

bool LockManager::acquire_shared(int txn_id, const std::string& table, std::pair<int, int> rid) {
    std::string res_id = make_resource_id(table, rid);
    auto req = std::make_shared<LockRequest>(txn_id, 'S');

    LockGuard lck(mu);
    
    LockEntry& entry = lock_table[res_id];
    
    // Check if already holds S or X
    auto holder_it = entry.holders.find(txn_id);
    if (holder_it != entry.holders.end()) {
        return true;
    }

    // Check conflict: any holder of X
    bool conflict = false;
    for (const auto& holder : entry.holders) {
        if (holder.second == 'X') {
            conflict = true;
            break;
        }
    }

    if (!conflict && entry.waiters.empty()) {
        entry.holders[txn_id] = 'S';
        txn_locks[txn_id].insert(res_id);
        return true;
    }

    // Add to waiters and check deadlock
    entry.waiters.push_back(req);
    if (has_deadlock(txn_id)) {
        entry.waiters.pop_back();
        throw DeadlockException("Deadlock detected! Transaction " + std::to_string(txn_id) + " aborted.");
    }

    // Wait until lock is granted
    auto cv_ptr = req->cv;
    while (!req->granted) {
        cv_ptr->wait(mu);
    }
    return true;
}

bool LockManager::acquire_exclusive(int txn_id, const std::string& table, std::pair<int, int> rid) {
    std::string res_id = make_resource_id(table, rid);
    auto req = std::make_shared<LockRequest>(txn_id, 'X');

    LockGuard lck(mu);
    
    LockEntry& entry = lock_table[res_id];
    
    // Check if already holds X
    auto holder_it = entry.holders.find(txn_id);
    if (holder_it != entry.holders.end()) {
        if (holder_it->second == 'X') {
            return true;
        }
        // Upgrade from S to X if we are the only holder
        if (entry.holders.size() == 1) {
            entry.holders[txn_id] = 'X';
            return true;
        }
    }

    // Conflict if there are any other holders
    bool conflict = !entry.holders.empty();

    if (!conflict && entry.waiters.empty()) {
        entry.holders[txn_id] = 'X';
        txn_locks[txn_id].insert(res_id);
        return true;
    }

    // Add to waiters and check deadlock
    entry.waiters.push_back(req);
    if (has_deadlock(txn_id)) {
        entry.waiters.pop_back();
        throw DeadlockException("Deadlock detected! Transaction " + std::to_string(txn_id) + " aborted.");
    }

    // Wait until lock is granted
    auto cv_ptr = req->cv;
    while (!req->granted) {
        cv_ptr->wait(mu);
    }
    return true;
}

void LockManager::release_locks(int txn_id) {
    LockGuard lck(mu);
    auto it = txn_locks.find(txn_id);
    if (it == txn_locks.end()) {
        return;
    }

    std::unordered_set<std::string> resources = it->second;
    for (const auto& res_id : resources) {
        auto entry_it = lock_table.find(res_id);
        if (entry_it == lock_table.end()) {
            continue;
        }
        LockEntry& entry = entry_it->second;
        entry.holders.erase(txn_id);

        grant_next_waiters(res_id, entry);

        if (entry.holders.empty() && entry.waiters.empty()) {
            lock_table.erase(entry_it);
        }
    }
    txn_locks.erase(it);
}

void LockManager::grant_next_waiters(const std::string& resource_id, LockEntry& entry) {
    if (!entry.holders.empty()) {
        // If current holder is exclusive, nothing can be granted
        for (const auto& holder : entry.holders) {
            if (holder.second == 'X') return;
        }

        // Holders are shared, we can only grant shared locks from the waiters list
        std::vector<std::shared_ptr<LockRequest>> to_grant;
        for (auto req_it = entry.waiters.begin(); req_it != entry.waiters.end(); ) {
            auto req = *req_it;
            if (req->lock_type == 'S') {
                req_it = entry.waiters.erase(req_it);
                entry.holders[req->txn_id] = 'S';
                txn_locks[req->txn_id].insert(resource_id);
                to_grant.push_back(req);
            } else {
                break; // X lock request blocks further grants
            }
        }
        for (auto& req : to_grant) {
            req->granted = true;
            req->cv->notify_all();
        }
        return;
    }

    if (entry.waiters.empty()) {
        return;
    }

    auto next_req = entry.waiters[0];
    if (next_req->lock_type == 'X') {
        entry.waiters.erase(entry.waiters.begin());
        entry.holders[next_req->txn_id] = 'X';
        txn_locks[next_req->txn_id].insert(resource_id);
        next_req->granted = true;
        next_req->cv->notify_all();
    } else {
        std::vector<std::shared_ptr<LockRequest>> to_grant;
        while (!entry.waiters.empty() && entry.waiters[0]->lock_type == 'S') {
            auto req = entry.waiters[0];
            entry.waiters.erase(entry.waiters.begin());
            entry.holders[req->txn_id] = 'S';
            txn_locks[req->txn_id].insert(resource_id);
            to_grant.push_back(req);
        }
        for (auto& req : to_grant) {
            req->granted = true;
            req->cv->notify_all();
        }
    }
}

bool LockManager::has_deadlock(int start_txn_id) {
    std::unordered_map<int, std::unordered_set<int>> graph;
    
    for (const auto& item : lock_table) {
        const LockEntry& entry = item.second;
        std::vector<int> holders;
        for (const auto& holder : entry.holders) {
            holders.push_back(holder.first);
        }
        
        for (const auto& req : entry.waiters) {
            int waiter_id = req->txn_id;
            for (int holder_id : holders) {
                if (waiter_id != holder_id) {
                    graph[waiter_id].insert(holder_id);
                }
            }
            
            for (const auto& prior_req : entry.waiters) {
                if (prior_req == req) break;
                if (req->lock_type == 'X' || prior_req->lock_type == 'X') {
                    graph[waiter_id].insert(prior_req->txn_id);
                }
            }
        }
    }

    // Detect cycle from start_txn_id using DFS
    std::unordered_set<int> visited;
    std::unordered_set<int> rec_stack;

    std::function<bool(int)> dfs = [&](int node) -> bool {
        visited.insert(node);
        rec_stack.insert(node);

        auto it = graph.find(node);
        if (it != graph.end()) {
            for (int neighbor : it->second) {
                if (visited.find(neighbor) == visited.end()) {
                    if (dfs(neighbor)) return true;
                } else if (rec_stack.find(neighbor) != rec_stack.end()) {
                    return true;
                }
            }
        }

        rec_stack.erase(node);
        return false;
    };

    return dfs(start_txn_id);
}
