#include "concurrency/lock_manager.h"
#include <iostream>
#include <algorithm>

LockManager::LockManager() {}

LockManager::~LockManager() {
    StopDeadlockDetector();
}

bool LockManager::AcquireShared(Transaction* txn, const RID& rid) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (txn->state == TransactionState::ABORTED) {
        return false;
    }

    LockRequestQueue& queue = lock_table_[rid];

    // Check if we already hold a lock
    for (auto& req : queue.request_queue) {
        if (req.txn_id == txn->txn_id) {
            if (req.granted) {
                return true; // Already granted S or X
            }
        }
    }

    // Add shared request
    queue.request_queue.push_back({txn->txn_id, LockMode::SHARED, false});
    auto req_it = std::prev(queue.request_queue.end());

    while (true) {
        if (txn->state == TransactionState::ABORTED) {
            queue.request_queue.erase(req_it);
            queue.cv.notify_all();
            return false;
        }

        // Check if we can grant: no EXCLUSIVE request before us
        bool can_grant = true;
        for (auto it = queue.request_queue.begin(); it != req_it; ++it) {
            if (it->lock_mode == LockMode::EXCLUSIVE) {
                can_grant = false;
                break;
            }
        }

        if (can_grant) {
            req_it->granted = true;
            txn->held_locks.push_back(rid);
            return true;
        }

        queue.cv.wait(lock);
    }
}

bool LockManager::AcquireExclusive(Transaction* txn, const RID& rid) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (txn->state == TransactionState::ABORTED) {
        return false;
    }

    LockRequestQueue& queue = lock_table_[rid];

    // Check if we already hold an exclusive lock
    for (auto& req : queue.request_queue) {
        if (req.txn_id == txn->txn_id) {
            if (req.granted && req.lock_mode == LockMode::EXCLUSIVE) {
                return true;
            }
            // If we have SHARED lock, this is a lock upgrade.
            // For simplicity, we just delete the SHARED and queue EXCLUSIVE
        }
    }

    // Queue exclusive request
    queue.request_queue.push_back({txn->txn_id, LockMode::EXCLUSIVE, false});
    auto req_it = std::prev(queue.request_queue.end());

    while (true) {
        if (txn->state == TransactionState::ABORTED) {
            queue.request_queue.erase(req_it);
            queue.cv.notify_all();
            return false;
        }

        // Can grant only if we are at the front of the queue
        if (queue.request_queue.begin() == req_it) {
            req_it->granted = true;
            txn->held_locks.push_back(rid);
            return true;
        }

        queue.cv.wait(lock);
    }
}

bool LockManager::Release(Transaction* txn, const RID& rid) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto q_iter = lock_table_.find(rid);
    if (q_iter == lock_table_.end()) {
        return false;
    }

    LockRequestQueue& queue = q_iter->second;
    for (auto it = queue.request_queue.begin(); it != queue.request_queue.end(); ++it) {
        if (it->txn_id == txn->txn_id) {
            queue.request_queue.erase(it);
            
            // Erase rid from txn held locks
            auto l_it = std::find(txn->held_locks.begin(), txn->held_locks.end(), rid);
            if (l_it != txn->held_locks.end()) {
                txn->held_locks.erase(l_it);
            }

            queue.cv.notify_all();
            if (queue.request_queue.empty()) {
                lock_table_.erase(q_iter);
            }
            return true;
        }
    }
    return false;
}

void LockManager::ReleaseAllLocks(Transaction* txn) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& rid : txn->held_locks) {
        auto q_iter = lock_table_.find(rid);
        if (q_iter != lock_table_.end()) {
            LockRequestQueue& queue = q_iter->second;
            queue.request_queue.remove_if([txn](const LockRequest& req) {
                return req.txn_id == txn->txn_id;
            });
            queue.cv.notify_all();
            if (queue.request_queue.empty()) {
                lock_table_.erase(q_iter);
            }
        }
    }
    txn->held_locks.clear();
}

void LockManager::StartDeadlockDetector(const std::unordered_map<TxId_t, Transaction*>* txn_map) {
    txn_map_ = txn_map;
    run_detector_ = true;
    detector_thread_ = std::thread(&LockManager::RunDeadlockDetection, this);
}

void LockManager::StopDeadlockDetector() {
    if (run_detector_) {
        run_detector_ = false;
        if (detector_thread_.joinable()) {
            detector_thread_.join();
        }
    }
}

void LockManager::RunDeadlockDetection() {
    while (run_detector_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        std::lock_guard<std::mutex> lock(mutex_);
        if (!txn_map_) continue;

        // Build waits-for graph
        std::unordered_map<TxId_t, std::vector<TxId_t>> adj;
        std::unordered_set<TxId_t> txns;

        for (const auto& [rid, queue] : lock_table_) {
            std::vector<TxId_t> holders;
            std::vector<TxId_t> waiters;

            for (const auto& req : queue.request_queue) {
                if (req.granted) {
                    holders.push_back(req.txn_id);
                } else {
                    waiters.push_back(req.txn_id);
                }
                txns.insert(req.txn_id);
            }

            for (TxId_t waiter : waiters) {
                for (TxId_t holder : holders) {
                    if (waiter != holder) {
                        adj[waiter].push_back(holder);
                    }
                }
            }
        }

        // Detect cycle
        std::unordered_set<TxId_t> visited;
        std::unordered_set<TxId_t> rec_stack;
        std::vector<TxId_t> cycle;
        TxId_t victim_txn_id = -1;

        for (TxId_t txn_id : txns) {
            if (visited.find(txn_id) == visited.end()) {
                cycle.clear();
                if (FindCycle(txn_id, adj, visited, rec_stack, cycle)) {
                    // Abort youngest transaction in the cycle (highest transaction ID)
                    victim_txn_id = *std::max_element(cycle.begin(), cycle.end());
                    break;
                }
            }
        }

        if (victim_txn_id != -1) {
            std::cout << "[Deadlock Detector] Cycle detected! Aborting transaction T" << victim_txn_id << std::endl;
            auto it = txn_map_->find(victim_txn_id);
            if (it != txn_map_->end()) {
                it->second->state = TransactionState::ABORTED;
                // Wake up all threads to check their state
                for (auto& [rid, queue] : lock_table_) {
                    queue.cv.notify_all();
                }
            }
        }
    }
}

bool LockManager::FindCycle(TxId_t curr_txn, std::unordered_map<TxId_t, std::vector<TxId_t>>& adj,
                            std::unordered_set<TxId_t>& visited, std::unordered_set<TxId_t>& rec_stack,
                            std::vector<TxId_t>& cycle) {
    visited.insert(curr_txn);
    rec_stack.insert(curr_txn);
    cycle.push_back(curr_txn);

    for (TxId_t neighbor : adj[curr_txn]) {
        if (visited.find(neighbor) == visited.end()) {
            if (FindCycle(neighbor, adj, visited, rec_stack, cycle)) {
                return true;
            }
        } else if (rec_stack.find(neighbor) != rec_stack.end()) {
            // Cycle found, isolate the cycle nodes
            auto it = std::find(cycle.begin(), cycle.end(), neighbor);
            if (it != cycle.end()) {
                cycle.erase(cycle.begin(), it);
            }
            return true;
        }
    }

    rec_stack.erase(curr_txn);
    cycle.pop_back();
    return false;
}
