#include "concurrency/lock_manager.h"

#include <algorithm>
#include <functional>

namespace minidb {

void LockManager::AbortDeadlockVictim(TxID victim) {
    {
        std::lock_guard<std::mutex> waits_lock(waits_for_mutex_);
        aborted_txs_.insert(victim);
        waits_for_.erase(victim);
    }

    std::vector<LockQueue*> queues;
    {
        std::lock_guard<std::mutex> table_lock(lock_table_mutex_);
        queues.reserve(lock_table_.size());
        for (auto& entry : lock_table_) {
            queues.push_back(&entry.second);
        }
        tx_locks_.erase(victim);
    }

    for (LockQueue* lq : queues) {
        std::unique_lock<std::mutex> queue_lock(lq->mu);
        lq->requests.remove_if([victim](const LockRequest& request) {
            return request.xid == victim;
        });
        lq->cv.notify_all();
    }
}

void LockManager::AcquireLock(const RowKey& key, TxID xid, LockMode mode, bool in_shrinking) {
    if (in_shrinking) {
        throw std::runtime_error("2PL violation: cannot acquire lock in shrinking phase");
    }

    LockQueue* lq = nullptr;
    {
        std::lock_guard<std::mutex> table_lock(lock_table_mutex_);
        lq = &lock_table_[key];
    }

    std::unique_lock<std::mutex> queue_lock(lq->mu);

    for (const auto& request : lq->requests) {
        if (request.xid == xid && request.granted) {
            if (mode == LockMode::SHARED) {
                return;
            }
            if (request.mode == LockMode::EXCLUSIVE) {
                return;
            }
        }
    }

    lq->requests.push_back({xid, mode, false});

    while (true) {
        {
            std::lock_guard<std::mutex> waits_lock(waits_for_mutex_);
            if (aborted_txs_.count(xid) != 0) {
                aborted_txs_.erase(xid);
                lq->requests.remove_if(
                    [xid](const LockRequest& request) { return request.xid == xid; });
                throw DeadlockException(xid);
            }
        }

        auto request_it = lq->requests.end();
        for (auto it = lq->requests.begin(); it != lq->requests.end(); ++it) {
            if (it->xid == xid && !it->granted) {
                request_it = it;
                break;
            }
        }
        if (request_it == lq->requests.end()) {
            throw DeadlockException(xid);
        }

        bool conflict = false;
        std::unordered_set<TxID> blocking;
        for (auto it = lq->requests.begin(); it != lq->requests.end(); ++it) {
            if (it == request_it) {
                break;
            }
            if (!it->granted) {
                continue;
            }
            if (mode == LockMode::EXCLUSIVE || it->mode == LockMode::EXCLUSIVE) {
                if (it->xid != xid) {
                    conflict = true;
                    blocking.insert(it->xid);
                }
            }
        }

        if (!conflict) {
            request_it->granted = true;
            {
                std::lock_guard<std::mutex> table_lock(lock_table_mutex_);
                tx_locks_[xid].insert(key);
            }
            std::lock_guard<std::mutex> waits_lock(waits_for_mutex_);
            waits_for_.erase(xid);
            return;
        }

        TxID victim_to_abort = 0;
        {
            std::lock_guard<std::mutex> waits_lock(waits_for_mutex_);
            waits_for_[xid] = blocking;
            if (HasCycle(xid, waits_for_)) {
                const TxID victim = FindYoungestInCycle(xid, waits_for_);
                if (xid == victim) {
                    waits_for_.erase(xid);
                    lq->requests.erase(request_it);
                    throw DeadlockException(victim);
                }
                victim_to_abort = victim;
            }
        }

        if (victim_to_abort != 0) {
            queue_lock.unlock();
            AbortDeadlockVictim(victim_to_abort);
            queue_lock.lock();
            continue;
        }

        lq->cv.wait(queue_lock);
    }
}

void LockManager::ReleaseLocks(TxID xid) {
    std::unordered_set<RowKey> keys;
    {
        std::lock_guard<std::mutex> table_lock(lock_table_mutex_);
        const auto it = tx_locks_.find(xid);
        if (it != tx_locks_.end()) {
            keys = std::move(it->second);
            tx_locks_.erase(it);
        }
    }

    for (const RowKey& key : keys) {
        LockQueue* lq = nullptr;
        {
            std::lock_guard<std::mutex> table_lock(lock_table_mutex_);
            const auto it = lock_table_.find(key);
            if (it == lock_table_.end()) {
                continue;
            }
            lq = &it->second;
        }

        std::unique_lock<std::mutex> queue_lock(lq->mu);
        lq->requests.remove_if([xid](const LockRequest& request) { return request.xid == xid; });
        lq->cv.notify_all();
    }

    std::lock_guard<std::mutex> waits_lock(waits_for_mutex_);
    waits_for_.erase(xid);
}

void LockManager::MarkShrinking(TxID xid) {
    (void)xid;
}

bool LockManager::HasCycle(
    TxID start,
    const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) const {
    std::unordered_set<TxID> visited;
    std::unordered_set<TxID> stack;

    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        visited.insert(node);
        stack.insert(node);
        const auto it = graph.find(node);
        if (it != graph.end()) {
            for (TxID neighbor : it->second) {
                if (!visited.count(neighbor) && dfs(neighbor)) {
                    return true;
                }
                if (stack.count(neighbor)) {
                    return true;
                }
            }
        }
        stack.erase(node);
        return false;
    };

    return dfs(start);
}

TxID LockManager::FindYoungestInCycle(
    TxID start,
    const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) const {
    std::unordered_set<TxID> reachable;
    std::function<void(TxID)> collect = [&](TxID node) {
        if (reachable.count(node) != 0) {
            return;
        }
        reachable.insert(node);
        const auto it = graph.find(node);
        if (it != graph.end()) {
            for (TxID neighbor : it->second) {
                collect(neighbor);
            }
        }
    };
    collect(start);

    for (const auto& entry : graph) {
        if (reachable.count(entry.first) != 0) {
            for (TxID holder : entry.second) {
                reachable.insert(holder);
            }
        }
    }

    TxID youngest = start;
    for (TxID node : reachable) {
        if (node > youngest) {
            youngest = node;
        }
    }
    return youngest;
}

}  // namespace minidb
