#include "txn/lock_manager.h"
#include <iostream>
#include <algorithm>

// ============================================================
// TryGrant — check if a lock request can be granted immediately
// ============================================================

bool LockManager::TryGrant(LockEntry& entry, int txn_id, LockMode mode) {
    // Check existing granted requests
    for (auto& req : entry.requests) {
        if (!req.granted) continue;
        if (req.txn_id == txn_id) continue;  // same txn, ok

        if (mode == LockMode::EXCLUSIVE) {
            return false;  // someone else holds any lock → can't get exclusive
        }
        if (req.mode == LockMode::EXCLUSIVE) {
            return false;  // someone else holds exclusive → can't get shared
        }
        // Both SHARED → compatible, continue checking
    }
    return true;
}

// ============================================================
// LockShared
// ============================================================

bool LockManager::LockShared(int txn_id, const RID& rid) {
    std::unique_lock<std::mutex> lock(mu_);

    auto& entry = lock_table_[rid];

    // Check if we already hold a lock
    for (auto& req : entry.requests) {
        if (req.txn_id == txn_id && req.granted) {
            return true;  // already have it (shared or exclusive)
        }
    }

    // Add our request
    LockRequest our_req{txn_id, LockMode::SHARED, false};

    if (TryGrant(entry, txn_id, LockMode::SHARED)) {
        our_req.granted = true;
        entry.requests.push_back(our_req);
        txn_locks_[txn_id].push_back(rid);
        std::cout << "[Lock] Txn" << txn_id << " acquired SHARED on ("
                  << rid.page_id << "," << rid.slot_id << ")\n";
        return true;
    }

    // Need to wait — build wait-for edges
    entry.requests.push_back(our_req);
    int our_idx = entry.requests.size() - 1;

    for (auto& req : entry.requests) {
        if (req.granted && req.txn_id != txn_id) {
            wait_for_[txn_id].insert(req.txn_id);
        }
    }

    // Check for deadlock
    if (HasCycle(txn_id)) {
        // We are the victim — remove our request and wait-for edges
        entry.requests.erase(entry.requests.begin() + our_idx);
        wait_for_.erase(txn_id);
        std::cout << "[Deadlock] Txn" << txn_id << " detected as victim!\n";
        return false;
    }

    // Wait until granted
    std::cout << "[Lock] Txn" << txn_id << " waiting for SHARED on ("
              << rid.page_id << "," << rid.slot_id << ")\n";

    entry.cv.wait(lock, [&]() {
        return TryGrant(entry, txn_id, LockMode::SHARED);
    });

    // Granted!
    for (auto& req : entry.requests) {
        if (req.txn_id == txn_id && !req.granted) {
            req.granted = true;
            break;
        }
    }
    wait_for_.erase(txn_id);
    txn_locks_[txn_id].push_back(rid);
    std::cout << "[Lock] Txn" << txn_id << " acquired SHARED on ("
              << rid.page_id << "," << rid.slot_id << ") (was waiting)\n";
    return true;
}

// ============================================================
// LockExclusive
// ============================================================

bool LockManager::LockExclusive(int txn_id, const RID& rid) {
    std::unique_lock<std::mutex> lock(mu_);

    auto& entry = lock_table_[rid];

    // Check if we already hold an exclusive lock
    for (auto& req : entry.requests) {
        if (req.txn_id == txn_id && req.granted) {
            if (req.mode == LockMode::EXCLUSIVE) return true;  // already exclusive
            // We hold shared, try to upgrade — treat like new exclusive request
        }
    }

    LockRequest our_req{txn_id, LockMode::EXCLUSIVE, false};

    if (TryGrant(entry, txn_id, LockMode::EXCLUSIVE)) {
        our_req.granted = true;
        entry.requests.push_back(our_req);
        txn_locks_[txn_id].push_back(rid);
        std::cout << "[Lock] Txn" << txn_id << " acquired EXCLUSIVE on ("
                  << rid.page_id << "," << rid.slot_id << ")\n";
        return true;
    }

    // Need to wait
    entry.requests.push_back(our_req);
    int our_idx = entry.requests.size() - 1;

    for (auto& req : entry.requests) {
        if (req.granted && req.txn_id != txn_id) {
            wait_for_[txn_id].insert(req.txn_id);
        }
    }

    if (HasCycle(txn_id)) {
        entry.requests.erase(entry.requests.begin() + our_idx);
        wait_for_.erase(txn_id);
        std::cout << "[Deadlock] Txn" << txn_id << " detected as victim!\n";
        return false;
    }

    std::cout << "[Lock] Txn" << txn_id << " waiting for EXCLUSIVE on ("
              << rid.page_id << "," << rid.slot_id << ")\n";

    entry.cv.wait(lock, [&]() {
        return TryGrant(entry, txn_id, LockMode::EXCLUSIVE);
    });

    for (auto& req : entry.requests) {
        if (req.txn_id == txn_id && !req.granted) {
            req.granted = true;
            break;
        }
    }
    wait_for_.erase(txn_id);
    txn_locks_[txn_id].push_back(rid);
    std::cout << "[Lock] Txn" << txn_id << " acquired EXCLUSIVE on ("
              << rid.page_id << "," << rid.slot_id << ") (was waiting)\n";
    return true;
}

// ============================================================
// UnlockAll — release all locks held by a txn (strict 2PL)
// ============================================================

void LockManager::UnlockAll(int txn_id) {
    std::unique_lock<std::mutex> lock(mu_);

    auto it = txn_locks_.find(txn_id);
    if (it == txn_locks_.end()) return;

    for (auto& rid : it->second) {
        auto eit = lock_table_.find(rid);
        if (eit == lock_table_.end()) continue;

        auto& entry = eit->second;
        // Remove all requests from this txn
        entry.requests.erase(
            std::remove_if(entry.requests.begin(), entry.requests.end(),
                           [&](const LockRequest& r) { return r.txn_id == txn_id; }),
            entry.requests.end());

        // Wake up any waiters
        entry.cv.notify_all();
    }

    txn_locks_.erase(it);
    wait_for_.erase(txn_id);
    std::cout << "[Lock] Txn" << txn_id << " released all locks\n";
}

// ============================================================
// Wait-for graph deadlock detection (DFS)
// ============================================================

bool LockManager::HasCycle(int txn_id) {
    std::unordered_set<int> visited, in_stack;
    return DFS(txn_id, visited, in_stack);
}

bool LockManager::DFS(int node, std::unordered_set<int>& visited,
                       std::unordered_set<int>& in_stack) {
    visited.insert(node);
    in_stack.insert(node);

    auto it = wait_for_.find(node);
    if (it != wait_for_.end()) {
        for (int neighbor : it->second) {
            if (in_stack.count(neighbor)) return true;  // cycle!
            if (!visited.count(neighbor)) {
                if (DFS(neighbor, visited, in_stack)) return true;
            }
        }
    }

    in_stack.erase(node);
    return false;
}

// ============================================================
// Debug helpers
// ============================================================

std::string LockManager::GetLockInfo(int txn_id, const RID& rid) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = lock_table_.find(rid);
    if (it == lock_table_.end()) return "";
    for (auto& req : it->second.requests) {
        if (req.txn_id == txn_id && req.granted) {
            return req.mode == LockMode::SHARED ? "SHARED" : "EXCLUSIVE";
        }
    }
    return "";
}

std::vector<int> LockManager::GetHolders(const RID& rid) {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<int> holders;
    auto it = lock_table_.find(rid);
    if (it != lock_table_.end()) {
        for (auto& req : it->second.requests) {
            if (req.granted) holders.push_back(req.txn_id);
        }
    }
    return holders;
}
