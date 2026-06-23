#include "concurrency/lock_manager.h"
#include <chrono>
#include <iostream>

namespace minidb {

bool LockManager::IsCompatible(const RID& rid, LockMode mode, txn_id_t txn_id) {
    auto it = lock_table_.find(rid);
    if (it == lock_table_.end()) {
        return true;
    }
    
    const auto& queue = it->second.request_queue;
    for (const auto& req : queue) {
        if (req.txn_id == txn_id) {
            continue; // Ignore our own requests
        }
        if (req.is_granted) {
            // Exclusive locks are incompatible with everything
            if (req.lock_mode == LockMode::EXCLUSIVE) {
                return false;
            }
            // Shared locks are incompatible with Exclusive requests
            if (mode == LockMode::EXCLUSIVE) {
                return false;
            }
        } else {
            // Starvation prevention: if there is an exclusive request before us,
            // we should wait (strict FIFO queue behavior)
            if (req.lock_mode == LockMode::EXCLUSIVE) {
                return false;
            }
        }
    }
    return true;
}

bool LockManager::LockShared(Transaction* txn, const RID& rid) {
    if (txn == nullptr) return true; // Auto-commit mode without transaction locks

    std::unique_lock<std::mutex> lock(latch_);

    if (txn->GetState() != TransactionState::GROWING) {
        txn->SetState(TransactionState::ABORTED);
        return false;
    }

    // Check if we already hold the lock
    if (txn->GetExclusiveLocks().count(rid) > 0 || txn->GetSharedLocks().count(rid) > 0) {
        return true;
    }

    LockHead& head = lock_table_[rid];
    
    // Create new shared lock request
    head.request_queue.push_back({txn->GetTxnId(), LockMode::SHARED, false});
    auto req_it = std::prev(head.request_queue.end());

    auto timeout = std::chrono::milliseconds(200);
    bool status = head.cv.wait_for(lock, timeout, [this, &rid, txn, req_it]() {
        // Can be granted if no other transaction holds an Exclusive lock
        for (auto it = lock_table_[rid].request_queue.begin(); it != req_it; ++it) {
            if (it->txn_id != txn->GetTxnId() && it->is_granted && it->lock_mode == LockMode::EXCLUSIVE) {
                return false;
            }
        }
        return true;
    });

    if (!status) {
        // Timeout / Deadlock detected
        head.request_queue.erase(req_it);
        if (head.request_queue.empty()) {
            lock_table_.erase(rid);
        } else {
            head.cv.notify_all();
        }
        txn->SetState(TransactionState::ABORTED);
        return false;
    }

    req_it->is_granted = true;
    txn->AddSharedLock(rid);
    return true;
}

bool LockManager::LockExclusive(Transaction* txn, const RID& rid) {
    if (txn == nullptr) return true;

    std::unique_lock<std::mutex> lock(latch_);

    if (txn->GetState() != TransactionState::GROWING) {
        txn->SetState(TransactionState::ABORTED);
        return false;
    }

    // Check if we already hold the lock
    if (txn->GetExclusiveLocks().count(rid) > 0) {
        return true;
    }

    LockHead& head = lock_table_[rid];

    // Check if upgrading from Shared to Exclusive
    bool upgrading = false;
    auto upgrade_it = head.request_queue.end();
    for (auto it = head.request_queue.begin(); it != head.request_queue.end(); ++it) {
        if (it->txn_id == txn->GetTxnId()) {
            upgrading = true;
            upgrade_it = it;
            break;
        }
    }

    if (upgrading) {
        upgrade_it->lock_mode = LockMode::EXCLUSIVE;
        upgrade_it->is_granted = false;
    } else {
        head.request_queue.push_back({txn->GetTxnId(), LockMode::EXCLUSIVE, false});
        upgrade_it = std::prev(head.request_queue.end());
    }

    auto timeout = std::chrono::milliseconds(200);
    bool status = head.cv.wait_for(lock, timeout, [this, &rid, txn, upgrade_it]() {
        // Exclusive lock requires being the only transaction in the queue
        // or all other active granted locks are cleared.
        for (auto it = lock_table_[rid].request_queue.begin(); it != upgrade_it; ++it) {
            if (it->txn_id != txn->GetTxnId() && it->is_granted) {
                return false;
            }
        }
        return true;
    });

    if (!status) {
        // Timeout / Deadlock detected
        if (upgrading) {
            // Restore to Shared
            upgrade_it->lock_mode = LockMode::SHARED;
            upgrade_it->is_granted = true;
        } else {
            head.request_queue.erase(upgrade_it);
            if (head.request_queue.empty()) {
                lock_table_.erase(rid);
            } else {
                head.cv.notify_all();
            }
        }
        txn->SetState(TransactionState::ABORTED);
        return false;
    }

    upgrade_it->is_granted = true;
    txn->AddExclusiveLock(rid);
    return true;
}

void LockManager::ReleaseLocks(Transaction* txn) {
    if (txn == nullptr) return;

    std::lock_guard<std::mutex> lock(latch_);

    // Release all shared locks
    for (const auto& rid : txn->GetSharedLocks()) {
        auto it = lock_table_.find(rid);
        if (it != lock_table_.end()) {
            auto& queue = it->second.request_queue;
            for (auto q_it = queue.begin(); q_it != queue.end(); ++q_it) {
                if (q_it->txn_id == txn->GetTxnId()) {
                    queue.erase(q_it);
                    break;
                }
            }
            if (queue.empty()) {
                lock_table_.erase(it);
            } else {
                it->second.cv.notify_all();
            }
        }
    }

    // Release all exclusive locks
    for (const auto& rid : txn->GetExclusiveLocks()) {
        auto it = lock_table_.find(rid);
        if (it != lock_table_.end()) {
            auto& queue = it->second.request_queue;
            for (auto q_it = queue.begin(); q_it != queue.end(); ++q_it) {
                if (q_it->txn_id == txn->GetTxnId()) {
                    queue.erase(q_it);
                    break;
                }
            }
            if (queue.empty()) {
                lock_table_.erase(it);
            } else {
                it->second.cv.notify_all();
            }
        }
    }

    txn->ClearLocks();
}

} // namespace minidb
