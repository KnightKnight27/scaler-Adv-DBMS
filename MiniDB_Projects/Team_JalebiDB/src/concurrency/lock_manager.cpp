#include "concurrency/lock_manager.h"
#include <iostream>
#include <chrono>

namespace minidb {

LockManager::LockManager() {
    StartDeadlockDetection();
}

LockManager::~LockManager() {
    StopDeadlockDetection();
}

bool LockManager::AcquireShared(Transaction *txn, const RID &rid) {
    std::unique_lock<std::mutex> lock(latch_);

    if (txn->GetState() == TransactionState::ABORTED) {
        return false;
    }

    LockHead &head = lock_table_[rid];

    // Check if transaction already holds a lock on this resource
    for (auto it = head.request_queue.begin(); it != head.request_queue.end(); ++it) {
        if (it->txn_id == txn->GetTxnId()) {
            return true;
        }
    }
    
    // Add request to queue
    head.request_queue.push_back({txn, txn->GetTxnId(), LockMode::SHARED, false});
    auto req_it = std::prev(head.request_queue.end());

    while (txn->GetState() != TransactionState::ABORTED) {
        // Check if granted: no EXCLUSIVE lock request ahead in queue
        bool can_grant = true;
        for (auto it = head.request_queue.begin(); it != req_it; ++it) {
            if (it->lock_mode == LockMode::EXCLUSIVE) {
                can_grant = false;
                break;
            }
        }

        if (can_grant) {
            req_it->is_granted = true;
            txn->AddLock(rid);
            return true;
        }

        head.cv.wait(lock);
    }

    // If aborted, clean up request
    head.request_queue.erase(req_it);
    head.cv.notify_all();
    return false;
}

bool LockManager::AcquireExclusive(Transaction *txn, const RID &rid) {
    std::unique_lock<std::mutex> lock(latch_);

    if (txn->GetState() == TransactionState::ABORTED) {
        return false;
    }

    LockHead &head = lock_table_[rid];
    
    // Check if we already hold a lock on this resource
    auto req_it = head.request_queue.end();
    for (auto it = head.request_queue.begin(); it != head.request_queue.end(); ++it) {
        if (it->txn_id == txn->GetTxnId()) {
            req_it = it;
            break;
        }
    }

    if (req_it != head.request_queue.end()) {
        if (req_it->lock_mode == LockMode::EXCLUSIVE) {
            return true;
        }
        // Upgrade SHARED to EXCLUSIVE
        req_it->lock_mode = LockMode::EXCLUSIVE;
        req_it->is_granted = false;
    } else {
        // Add new request
        head.request_queue.push_back({txn, txn->GetTxnId(), LockMode::EXCLUSIVE, false});
        req_it = std::prev(head.request_queue.end());
    }

    while (txn->GetState() != TransactionState::ABORTED) {
        // Check if granted: must be the first request in queue
        if (head.request_queue.begin() == req_it) {
            req_it->is_granted = true;
            txn->AddLock(rid);
            return true;
        }

        head.cv.wait(lock);
    }

    // If aborted, clean up request
    head.request_queue.erase(req_it);
    head.cv.notify_all();
    return false;
}

bool LockManager::Release(Transaction *txn, const RID &rid) {
    std::lock_guard<std::mutex> lock(latch_);
    
    auto iter = lock_table_.find(rid);
    if (iter == lock_table_.end()) {
        return false;
    }

    LockHead &head = iter->second;
    for (auto it = head.request_queue.begin(); it != head.request_queue.end(); ++it) {
        if (it->txn_id == txn->GetTxnId()) {
            head.request_queue.erase(it);
            head.cv.notify_all();
            break;
        }
    }
    return true;
}

void LockManager::ReleaseAllLocks(Transaction *txn) {
    std::lock_guard<std::mutex> lock(latch_);
    
    for (const RID &rid : txn->GetLockSet()) {
        auto iter = lock_table_.find(rid);
        if (iter != lock_table_.end()) {
            LockHead &head = iter->second;
            for (auto it = head.request_queue.begin(); it != head.request_queue.end(); ++it) {
                if (it->txn_id == txn->GetTxnId()) {
                    head.request_queue.erase(it);
                    head.cv.notify_all();
                    break;
                }
            }
        }
    }
    txn->ClearLocks();
}

void LockManager::StartDeadlockDetection() {
    run_detection_ = true;
    detection_thread_ = std::thread(&LockManager::RunCycleDetection, this);
}

void LockManager::StopDeadlockDetection() {
    {
        std::lock_guard<std::mutex> lock(latch_);
        run_detection_ = false;
    }
    if (detection_thread_.joinable()) {
        detection_thread_.join();
    }
}

bool LockManager::DFS(txn_id_t curr, std::unordered_map<txn_id_t, std::vector<txn_id_t>> &adj,
                      std::unordered_set<txn_id_t> &visited, std::unordered_set<txn_id_t> &rec_stack,
                      txn_id_t &victim) {
    visited.insert(curr);
    rec_stack.insert(curr);

    for (txn_id_t neighbor : adj[curr]) {
        if (rec_stack.find(neighbor) != rec_stack.end()) {
            // Cycle detected! Select the youngest transaction in cycle (highest txn_id)
            victim = std::max(victim, curr);
            victim = std::max(victim, neighbor);
            return true;
        }
        if (visited.find(neighbor) == visited.end()) {
            if (DFS(neighbor, adj, visited, rec_stack, victim)) {
                victim = std::max(victim, curr);
                return true;
            }
        }
    }

    rec_stack.erase(curr);
    return false;
}

void LockManager::RunCycleDetection() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        std::unique_lock<std::mutex> lock(latch_);
        if (!run_detection_) {
            break;
        }

        // 1. Build waits-for graph
        std::unordered_map<txn_id_t, std::vector<txn_id_t>> adj;
        std::unordered_set<txn_id_t> all_txns;

        for (const auto &pair : lock_table_) {
            const LockHead &head = pair.second;
            
            std::vector<txn_id_t> holders;
            std::vector<txn_id_t> waiters;

            for (const auto &req : head.request_queue) {
                all_txns.insert(req.txn_id);
                if (req.is_granted) {
                    holders.push_back(req.txn_id);
                } else {
                    waiters.push_back(req.txn_id);
                }
            }

            // waiter waits for all holders
            for (txn_id_t w : waiters) {
                for (txn_id_t h : holders) {
                    if (w != h) {
                        adj[w].push_back(h);
                    }
                }
            }
        }

        // 2. Cycle detection via DFS
        std::unordered_set<txn_id_t> visited;
        std::unordered_set<txn_id_t> rec_stack;
        txn_id_t victim = INVALID_TXN_ID;
        bool has_cycle = false;

        for (txn_id_t txn : all_txns) {
            if (visited.find(txn) == visited.end()) {
                if (DFS(txn, adj, visited, rec_stack, victim)) {
                    has_cycle = true;
                    break;
                }
            }
        }

        // 3. Abort the victim
        if (has_cycle && victim != INVALID_TXN_ID) {
            std::cout << "[Deadlock Detector] Found cycle! Aborting youngest txn: " << victim << std::endl;
            for (auto &pair : lock_table_) {
                LockHead &head = pair.second;
                for (auto &req : head.request_queue) {
                    if (req.txn_id == victim) {
                        req.txn->SetState(TransactionState::ABORTED);
                    }
                }
                head.cv.notify_all();
            }
        }
    }
}

} // namespace minidb
