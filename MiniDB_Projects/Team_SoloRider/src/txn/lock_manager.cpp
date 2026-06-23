#include "txn/lock_manager.h"
#include <algorithm>

namespace minidb {

bool LockManager::can_grant_lock(const std::vector<LockRequest>& requests, int txn_id, LockMode mode) {
    if (requests.empty()) return true;
    
    for (const auto& req : requests) {
        if (req.granted && req.txn_id != txn_id) {
            if (mode == LockMode::EXCLUSIVE || req.mode == LockMode::EXCLUSIVE) {
                return false;
            }
        }
    }
    
    // Check if there are earlier ungranted requests
    for (const auto& req : requests) {
        if (!req.granted && req.txn_id != txn_id) {
            return false; // FIFO behavior
        }
        if (req.txn_id == txn_id) break;
    }
    
    return true;
}

bool LockManager::lock_shared(Transaction* txn, RecordId rid) {
    if (txn->get_state() == TransactionState::SHRINKING) return false;
    
    std::unique_lock<std::mutex> lock(latch_);
    auto& requests = lock_table_[rid];
    
    // Check if already has lock
    for (auto& req : requests) {
        if (req.txn_id == txn->get_txn_id() && req.granted) return true;
    }
    
    requests.push_back({txn->get_txn_id(), LockMode::SHARED, false});
    
    cvs_[rid].wait(lock, [&]() {
        if (txn->get_state() == TransactionState::ABORTED) return true;
        return can_grant_lock(requests, txn->get_txn_id(), LockMode::SHARED);
    });
    
    if (txn->get_state() == TransactionState::ABORTED) {
        requests.erase(std::remove_if(requests.begin(), requests.end(), 
            [&](const LockRequest& r){ return r.txn_id == txn->get_txn_id(); }), requests.end());
        cvs_[rid].notify_all();
        return false;
    }
    
    for (auto& req : requests) {
        if (req.txn_id == txn->get_txn_id()) {
            req.granted = true;
            break;
        }
    }
    
    txn->add_shared_lock(rid);
    return true;
}

bool LockManager::lock_exclusive(Transaction* txn, RecordId rid) {
    if (txn->get_state() == TransactionState::SHRINKING) return false;
    
    std::unique_lock<std::mutex> lock(latch_);
    auto& requests = lock_table_[rid];
    
    requests.push_back({txn->get_txn_id(), LockMode::EXCLUSIVE, false});
    
    cvs_[rid].wait(lock, [&]() {
        if (txn->get_state() == TransactionState::ABORTED) return true;
        return can_grant_lock(requests, txn->get_txn_id(), LockMode::EXCLUSIVE);
    });
    
    if (txn->get_state() == TransactionState::ABORTED) {
        requests.erase(std::remove_if(requests.begin(), requests.end(), 
            [&](const LockRequest& r){ return r.txn_id == txn->get_txn_id(); }), requests.end());
        cvs_[rid].notify_all();
        return false;
    }
    
    for (auto& req : requests) {
        if (req.txn_id == txn->get_txn_id()) {
            req.granted = true;
            break;
        }
    }
    
    txn->add_exclusive_lock(rid);
    return true;
}

bool LockManager::unlock(Transaction* txn, RecordId rid) {
    std::unique_lock<std::mutex> lock(latch_);
    auto& requests = lock_table_[rid];
    
    bool found = false;
    requests.erase(std::remove_if(requests.begin(), requests.end(), 
        [&](const LockRequest& r){ 
            if (r.txn_id == txn->get_txn_id() && r.granted) {
                found = true;
                return true;
            }
            return false;
        }), requests.end());
        
    if (!found) return false;
    
    txn->set_state(TransactionState::SHRINKING);
    txn->remove_shared_lock(rid);
    txn->remove_exclusive_lock(rid);
    
    cvs_[rid].notify_all();
    return true;
}

std::vector<std::pair<int, int>> LockManager::get_wait_for_graph() {
    std::unique_lock<std::mutex> lock(latch_);
    std::vector<std::pair<int, int>> edges;
    
    for (const auto& [rid, requests] : lock_table_) {
        std::vector<int> granted_txns;
        for (const auto& req : requests) {
            if (req.granted) granted_txns.push_back(req.txn_id);
        }
        for (const auto& req : requests) {
            if (!req.granted) {
                for (int g_txn : granted_txns) {
                    if (g_txn != req.txn_id) {
                        edges.push_back({req.txn_id, g_txn});
                    }
                }
            }
        }
    }
    return edges;
}

void LockManager::abort_txn(Transaction* txn) {
    std::unique_lock<std::mutex> lock(latch_);
    txn->set_state(TransactionState::ABORTED);
    
    // Remove all requests for this txn
    for (auto& [rid, requests] : lock_table_) {
        requests.erase(std::remove_if(requests.begin(), requests.end(), 
            [&](const LockRequest& r){ return r.txn_id == txn->get_txn_id(); }), requests.end());
        cvs_[rid].notify_all();
    }
}

} // namespace minidb
