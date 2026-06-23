#include "transaction/lock_manager.h"
#include <chrono>

namespace minidb {

bool LockManager::LockShared(Transaction* txn, const std::string& resource) {
    std::unique_lock<std::mutex> lock(mutex_);

    auto& queue = lock_table_[resource];
    LockRequest req{txn->GetTxnId(), LockMode::SHARED, false};

    // Check if this txn already holds a lock on this resource
    for (auto& r : queue.requests) {
        if (r.txn_id == txn->GetTxnId()) {
            return true; // already locked
        }
    }

    queue.requests.push_back(req);
    auto it = std::prev(queue.requests.end());

    // Try to grant immediately
    if (TryGrant(queue, *it)) {
        it->granted = true;
        txn->AddLock(resource, LockMode::SHARED);
        return true;
    }

    // Wait with timeout (deadlock detection via timeout)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!it->granted) {
        if (queue.cv.wait_until(lock, deadline) == std::cv_status::timeout) {
            // Timeout — potential deadlock. Remove request and abort.
            queue.requests.erase(it);
            return false;
        }
    }

    txn->AddLock(resource, LockMode::SHARED);
    return true;
}

bool LockManager::LockExclusive(Transaction* txn, const std::string& resource) {
    std::unique_lock<std::mutex> lock(mutex_);

    auto& queue = lock_table_[resource];

    // Remove this transaction's shared request before requesting an upgrade.
    for (auto it = queue.requests.begin(); it != queue.requests.end(); ++it) {
        if (it->txn_id != txn->GetTxnId()) continue;
        if (it->mode == LockMode::EXCLUSIVE) return true;
        queue.requests.erase(it);
        break;
    }

    LockRequest req{txn->GetTxnId(), LockMode::EXCLUSIVE, false};
    queue.requests.push_back(req);
    auto it = std::prev(queue.requests.end());

    // Try to grant immediately
    if (TryGrant(queue, *it)) {
        it->granted = true;
        txn->AddLock(resource, LockMode::EXCLUSIVE);
        return true;
    }

    // Wait with timeout
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!it->granted) {
        if (queue.cv.wait_until(lock, deadline) == std::cv_status::timeout) {
            queue.requests.erase(it);
            return false;
        }
    }

    txn->AddLock(resource, LockMode::EXCLUSIVE);
    return true;
}

void LockManager::UnlockAll(Transaction* txn) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [resource, queue] : lock_table_) {
        auto it = queue.requests.begin();
        while (it != queue.requests.end()) {
            if (it->txn_id == txn->GetTxnId()) {
                it = queue.requests.erase(it);
            } else {
                ++it;
            }
        }
        // Try to grant waiting requests
        GrantWaiters(queue);
        queue.cv.notify_all();
    }

    txn->ClearLocks();
}

bool LockManager::TryGrant(LockQueue& queue, const LockRequest& req) {
    if (req.mode == LockMode::SHARED) {
        // Shared lock can be granted if no exclusive lock is held
        for (auto& r : queue.requests) {
            if (&r == &req) continue;
            if (r.granted && r.mode == LockMode::EXCLUSIVE) {
                return false;
            }
        }
        return true;
    } else {
        // Exclusive lock can be granted if no other lock is held
        for (auto& r : queue.requests) {
            if (&r == &req) continue;
            if (r.granted) return false;
        }
        return true;
    }
}

void LockManager::GrantWaiters(LockQueue& queue) {
    for (auto& r : queue.requests) {
        if (!r.granted && TryGrant(queue, r)) {
            r.granted = true;
        }
    }
}

} // namespace minidb
