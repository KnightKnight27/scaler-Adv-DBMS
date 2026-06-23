#include "transaction/lock_manager.h"
#include <chrono>

namespace minidb {

void LockManager::AddEdge(int t1, int t2) {
    waits_for_[t1].push_back(t2);
}

void LockManager::RemoveEdge(int t1, int t2) {
    if (waits_for_.find(t1) != waits_for_.end()) {
        waits_for_[t1].remove(t2);
    }
}

bool LockManager::Dfs(int txn_id, std::unordered_map<int, int>& visited, std::vector<int>& stack, int* cycle_txn_id) {
    visited[txn_id] = 1; // 1 means visiting
    stack.push_back(txn_id);
    
    for (int next : waits_for_[txn_id]) {
        if (visited[next] == 0) { // 0 means unvisited
            if (Dfs(next, visited, stack, cycle_txn_id)) return true;
        } else if (visited[next] == 1) {
            // Cycle detected! Pick the youngest transaction (highest ID) in cycle to abort
            int max_id = next;
            for (int id : stack) {
                if (id > max_id) max_id = id;
            }
            *cycle_txn_id = max_id;
            return true;
        }
    }
    
    visited[txn_id] = 2; // 2 means visited fully
    stack.pop_back();
    return false;
}

bool LockManager::DetectDeadlock(int* aborted_txn_id) {
    std::unique_lock<std::mutex> lock(latch_);
    std::unordered_map<int, int> visited;
    
    for (const auto& pair : waits_for_) {
        visited[pair.first] = 0;
        for (int neighbor : pair.second) visited[neighbor] = 0;
    }
    
    for (const auto& pair : visited) {
        if (pair.second == 0) {
            std::vector<int> stack;
            if (Dfs(pair.first, visited, stack, aborted_txn_id)) {
                return true;
            }
        }
    }
    return false;
}

bool LockManager::LockShared(Transaction* txn, const RecordId& rid) {
    if (txn->GetState() == TransactionState::ABORTED) return false;
    
    std::unique_lock<std::mutex> lock(latch_);
    LockRequestQueue& queue = lock_table_[rid];
    auto request = std::make_shared<LockRequest>(txn->GetTransactionId(), LockMode::SHARED);
    queue.request_queue_.push_back(request);
    
    // Add wait-for edges if we must wait
    if (queue.is_exclusive_locked_ || (!queue.request_queue_.empty() && queue.request_queue_.front()->txn_id_ != request->txn_id_ && queue.request_queue_.front()->lock_mode_ == LockMode::EXCLUSIVE)) {
        for (auto& req : queue.request_queue_) {
            if (req->granted_) {
                AddEdge(txn->GetTransactionId(), req->txn_id_);
            }
        }
    }

    auto can_grant = [&]() {
        if (queue.is_exclusive_locked_) return false;
        for (auto& req : queue.request_queue_) {
            if (req->txn_id_ == request->txn_id_) break;
            if (req->lock_mode_ == LockMode::EXCLUSIVE) return false;
        }
        return true;
    };
    
    bool success = queue.cv_.wait_for(lock, std::chrono::milliseconds(DEADLOCK_TIMEOUT_MS), can_grant);
    
    // Remove edges as we are no longer waiting
    for (auto& req : queue.request_queue_) {
        if (req->granted_) RemoveEdge(txn->GetTransactionId(), req->txn_id_);
    }

    if (!success) {
        queue.request_queue_.remove(request);
        txn->SetState(TransactionState::ABORTED);
        return false;
    }
    
    request->granted_ = true;
    queue.shared_lock_count_++;
    txn->GetSharedLockSet().insert(rid);
    
    return true;
}

bool LockManager::LockExclusive(Transaction* txn, const RecordId& rid) {
    if (txn->GetState() == TransactionState::ABORTED) return false;
    
    std::unique_lock<std::mutex> lock(latch_);
    LockRequestQueue& queue = lock_table_[rid];
    auto request = std::make_shared<LockRequest>(txn->GetTransactionId(), LockMode::EXCLUSIVE);
    queue.request_queue_.push_back(request);
    
    // Add wait-for edges
    if (queue.is_exclusive_locked_ || queue.shared_lock_count_ > 0) {
        for (auto& req : queue.request_queue_) {
            if (req->granted_) {
                AddEdge(txn->GetTransactionId(), req->txn_id_);
            }
        }
    }

    auto can_grant = [&]() {
        if (queue.is_exclusive_locked_ || queue.shared_lock_count_ > 0) return false;
        return queue.request_queue_.front()->txn_id_ == request->txn_id_;
    };
    
    bool success = queue.cv_.wait_for(lock, std::chrono::milliseconds(DEADLOCK_TIMEOUT_MS), can_grant);
    
    for (auto& req : queue.request_queue_) {
        if (req->granted_) RemoveEdge(txn->GetTransactionId(), req->txn_id_);
    }

    if (!success) {
        queue.request_queue_.remove(request);
        txn->SetState(TransactionState::ABORTED);
        return false;
    }
    
    request->granted_ = true;
    queue.is_exclusive_locked_ = true;
    txn->GetExclusiveLockSet().insert(rid);
    
    return true;
}

bool LockManager::Unlock(Transaction* txn, const RecordId& rid) {
    std::unique_lock<std::mutex> lock(latch_);
    if (lock_table_.find(rid) == lock_table_.end()) return false;
    LockRequestQueue& queue = lock_table_[rid];
    
    bool found = false;
    for (auto it = queue.request_queue_.begin(); it != queue.request_queue_.end(); ++it) {
        if ((*it)->txn_id_ == txn->GetTransactionId() && (*it)->granted_) {
            if ((*it)->lock_mode_ == LockMode::SHARED) {
                queue.shared_lock_count_--;
                txn->GetSharedLockSet().erase(rid);
            } else {
                queue.is_exclusive_locked_ = false;
                txn->GetExclusiveLockSet().erase(rid);
            }
            queue.request_queue_.erase(it);
            found = true;
            break;
        }
    }
    
    if (found) {
        // Also clean up any waits_for_ where others were waiting on this transaction
        for (auto& pair : waits_for_) {
            pair.second.remove(txn->GetTransactionId());
        }
        queue.cv_.notify_all();
    }
    return found;
}

} // namespace minidb
