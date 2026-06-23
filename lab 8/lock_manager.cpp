#include "lock_manager.h"
#include "mvcc_heap.h"
#include <functional>

LockManager& LockManager::getInstance() {
    static LockManager instance;
    return instance;
}

bool LockManager::hasCycle(TxID start, const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) {
    std::unordered_set<TxID> visited;
    std::unordered_set<TxID> recStack;
    
    std::function<bool(TxID)> dfs = [&](TxID u) -> bool {
        visited.insert(u);
        recStack.insert(u);
        
        auto it = graph.find(u);
        if (it != graph.end()) {
            for (TxID v : it->second) {
                if (recStack.count(v)) return true;
                if (!visited.count(v) && dfs(v)) return true;
            }
        }
        
        recStack.erase(u);
        return false;
    };
    
    return dfs(start);
}

void LockManager::acquireLock(const RowKey& key, TxID xid, LockMode mode) {
    if (MvccHeap::getInstance().checkShrinking(xid)) {
        throw std::runtime_error("2PL Violation: Cannot acquire lock in shrinking phase for transaction " + std::to_string(xid));
    }

    LockQueue* lq = nullptr;
    {
        std::lock_guard<std::mutex> lk(lm_mutex);
        lq = &lock_table[key]; // Fetch or insert queue
    }

    std::unique_lock<std::mutex> ul(lq->mu);

    // Check if lock is already held
    for (auto& req : lq->requests) {
        if (req.xid == xid && req.granted) {
            if (mode == LockMode::SHARED) return; // Read lock is already held
            if (req.mode == LockMode::EXCLUSIVE) return; // Write lock is already held
            // Upgrade shared to exclusive (not implemented, we acquire directly below)
        }
    }

    lq->requests.push_back({xid, mode, false});
    auto& my_req = lq->requests.back();

    while (true) {
        bool conflict = false;
        std::unordered_set<TxID> blocking;

        for (auto& req : lq->requests) {
            if (&req == &my_req) break; // Look only at earlier requests
            if (!req.granted) continue;

            if (mode == LockMode::EXCLUSIVE || req.mode == LockMode::EXCLUSIVE) {
                if (req.xid != xid) {
                    conflict = true;
                    blocking.insert(req.xid);
                }
            }
        }

        if (!conflict) {
            my_req.granted = true;
            {
                std::lock_guard<std::mutex> lk(lm_mutex);
                waits_for.erase(xid);
            }
            return;
        }

        // Record waits-for dependency edges and test for cycle
        {
            std::lock_guard<std::mutex> lk(lm_mutex);
            waits_for[xid] = blocking;
            if (hasCycle(xid, waits_for)) {
                waits_for.erase(xid);
                lq->requests.remove_if([&](const LockRequest& req) {
                    return req.xid == xid && !req.granted;
                });
                throw DeadlockException(xid);
            }
        }

        lq->cv.wait(ul);
    }
}

void LockManager::releaseLocks(TxID xid) {
    MvccHeap::getInstance().setShrinking(xid);

    std::lock_guard<std::mutex> lk(lm_mutex);
    for (auto& [key, lq] : lock_table) {
        std::lock_guard<std::mutex> q_lk(lq.mu);
        lq.requests.remove_if([&](const LockRequest& req) {
            return req.xid == xid;
        });
        lq.cv.notify_all();
    }
    waits_for.erase(xid);
}
