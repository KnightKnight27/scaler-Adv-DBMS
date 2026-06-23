#include "transaction/lock_manager.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <iostream>

namespace minidb {

LockManager::LockManager() : enable_cycle_detection_(true) {
    deadlock_detection_thread_ = new std::thread(&LockManager::BackgroundCycleDetection, this);
}

LockManager::~LockManager() {
    enable_cycle_detection_ = false;
    if (deadlock_detection_thread_ != nullptr) {
        deadlock_detection_thread_->join();
        delete deadlock_detection_thread_;
    }
}

void LockManager::AddTransaction(Transaction *txn) {
    std::lock_guard<std::mutex> lock(latch_);
    txn_map_[txn->GetTransactionId()] = txn;
}

void LockManager::RemoveTransaction(txn_id_t txn_id) {
    std::lock_guard<std::mutex> lock(latch_);
    txn_map_.erase(txn_id);
}

bool LockManager::CanGrantLock(LockRequestQueue &queue, LockMode mode, txn_id_t txn_id) {
    for (const auto &req : queue.requests) {
        if (req.granted && req.txn_id != txn_id) {
            if (mode == LockMode::EXCLUSIVE || req.lock_mode == LockMode::EXCLUSIVE) {
                return false;
            }
        }
    }
    // Strict FIFO
    for (const auto &req : queue.requests) {
        if (!req.granted && req.txn_id != txn_id) {
            if (mode == LockMode::EXCLUSIVE || req.lock_mode == LockMode::EXCLUSIVE) {
                return false;
            }
        }
        if (req.txn_id == txn_id) break;
    }
    return true;
}

bool LockManager::LockShared(Transaction *txn, const std::string &resource_id) {
    if (txn->GetState() == TransactionState::ABORTED) return false;

    std::unique_lock<std::mutex> lock(latch_);
    auto &queue = lock_table_[resource_id];

    for (auto &req : queue.requests) {
        if (req.txn_id == txn->GetTransactionId()) {
            if (req.lock_mode == LockMode::EXCLUSIVE || req.lock_mode == LockMode::SHARED) {
                return true; // Already holding lock
            }
        }
    }

    queue.requests.push_back({txn->GetTransactionId(), LockMode::SHARED, false});
    
    queue.cv.wait(lock, [&]() {
        return txn->GetState() == TransactionState::ABORTED || CanGrantLock(queue, LockMode::SHARED, txn->GetTransactionId());
    });

    if (txn->GetState() == TransactionState::ABORTED) {
        // Remove from queue
        queue.requests.erase(std::remove_if(queue.requests.begin(), queue.requests.end(),
            [txn](const LockRequest &r) { return r.txn_id == txn->GetTransactionId(); }), queue.requests.end());
        queue.cv.notify_all();
        return false;
    }

    for (auto &req : queue.requests) {
        if (req.txn_id == txn->GetTransactionId()) {
            req.granted = true;
            break;
        }
    }
    txn->GetSharedLockSet().insert(resource_id);
    return true;
}

bool LockManager::LockExclusive(Transaction *txn, const std::string &resource_id) {
    if (txn->GetState() == TransactionState::ABORTED) return false;

    std::unique_lock<std::mutex> lock(latch_);
    auto &queue = lock_table_[resource_id];

    bool is_upgrade = false;
    for (auto &req : queue.requests) {
        if (req.txn_id == txn->GetTransactionId()) {
            if (req.lock_mode == LockMode::EXCLUSIVE) return true;
            if (req.lock_mode == LockMode::SHARED) {
                is_upgrade = true;
                req.lock_mode = LockMode::EXCLUSIVE;
                req.granted = false; // Temporarily ungrant to wait
                txn->GetSharedLockSet().erase(resource_id);
            }
        }
    }

    if (!is_upgrade) {
        queue.requests.push_back({txn->GetTransactionId(), LockMode::EXCLUSIVE, false});
    }

    queue.cv.wait(lock, [&]() {
        return txn->GetState() == TransactionState::ABORTED || CanGrantLock(queue, LockMode::EXCLUSIVE, txn->GetTransactionId());
    });

    if (txn->GetState() == TransactionState::ABORTED) {
        queue.requests.erase(std::remove_if(queue.requests.begin(), queue.requests.end(),
            [txn](const LockRequest &r) { return r.txn_id == txn->GetTransactionId(); }), queue.requests.end());
        queue.cv.notify_all();
        return false;
    }

    for (auto &req : queue.requests) {
        if (req.txn_id == txn->GetTransactionId()) {
            req.granted = true;
            break;
        }
    }
    txn->GetExclusiveLockSet().insert(resource_id);
    return true;
}

bool LockManager::Unlock(Transaction *txn, const std::string &resource_id) {
    std::unique_lock<std::mutex> lock(latch_);
    auto &queue = lock_table_[resource_id];
    
    queue.requests.erase(std::remove_if(queue.requests.begin(), queue.requests.end(),
        [txn](const LockRequest &r) { return r.txn_id == txn->GetTransactionId(); }), queue.requests.end());
    
    txn->GetSharedLockSet().erase(resource_id);
    txn->GetExclusiveLockSet().erase(resource_id);
    
    queue.cv.notify_all();
    return true;
}

void LockManager::BuildWaitsForGraph() {
    waits_for_.clear();
    for (const auto &pair : lock_table_) {
        const auto &queue = pair.second;
        std::vector<txn_id_t> granted_txns;
        for (const auto &req : queue.requests) {
            if (req.granted) granted_txns.push_back(req.txn_id);
        }
        for (const auto &req : queue.requests) {
            if (!req.granted) {
                for (auto g : granted_txns) {
                    if (g != req.txn_id) {
                        waits_for_[req.txn_id].push_back(g);
                    }
                }
            }
        }
    }
}

bool LockManager::Dfs(txn_id_t txn_id, std::unordered_map<txn_id_t, int> &visited, std::vector<txn_id_t> &path, txn_id_t *youngest_txn_id) {
    visited[txn_id] = 1; // 1 = visiting
    path.push_back(txn_id);
    
    for (auto neighbor : waits_for_[txn_id]) {
        if (visited[neighbor] == 1) {
            // Cycle found
            txn_id_t youngest = neighbor;
            for (auto it = std::find(path.begin(), path.end(), neighbor); it != path.end(); ++it) {
                if (*it > youngest) youngest = *it;
            }
            *youngest_txn_id = youngest;
            return true;
        } else if (visited[neighbor] == 0) {
            if (Dfs(neighbor, visited, path, youngest_txn_id)) return true;
        }
    }
    
    visited[txn_id] = 2; // 2 = visited
    path.pop_back();
    return false;
}

bool LockManager::HasCycle(txn_id_t *youngest_txn_id) {
    std::unordered_map<txn_id_t, int> visited;
    for (const auto &pair : waits_for_) visited[pair.first] = 0;
    
    for (const auto &pair : waits_for_) {
        if (visited[pair.first] == 0) {
            std::vector<txn_id_t> path;
            if (Dfs(pair.first, visited, path, youngest_txn_id)) {
                return true;
            }
        }
    }
    return false;
}

void LockManager::RunCycleDetection() {
    std::unique_lock<std::mutex> lock(latch_);
    BuildWaitsForGraph();
    txn_id_t youngest;
    if (HasCycle(&youngest)) {
        if (txn_map_.find(youngest) != txn_map_.end()) {
            Transaction *txn = txn_map_[youngest];
            txn->SetState(TransactionState::ABORTED);
            for (auto &pair : lock_table_) {
                pair.second.cv.notify_all();
            }
            std::cout << "\n[Deadlock Detector] Aborted transaction " << youngest << " to resolve cycle." << std::endl;
        }
    }
}

void LockManager::BackgroundCycleDetection() {
    while (enable_cycle_detection_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        RunCycleDetection();
    }
}

} // namespace minidb
