#include "transaction/lock_manager.h"
#include <iostream>

// ─── getQueue ─────────────────────────────────────────────────────────────────

LockManager::LockQueue& LockManager::getQueue(const std::string& key) {
    std::lock_guard<std::mutex> lt_lk(lt_mutex_);
    auto it = lock_table_.find(key);
    if (it == lock_table_.end()) {
        lock_table_[key] = new LockQueue();
    }
    return *lock_table_[key];
}

// ─── acquireLock ──────────────────────────────────────────────────────────────

void LockManager::acquireLock(TxID xid, const std::string& resource_key, LockMode mode) {
    // 2PL rule: cannot acquire new locks in shrinking phase
    {
        std::lock_guard<std::mutex> sk(shrink_mutex_);
        if (shrinking_.count(xid)) {
            throw std::runtime_error(
                "2PL violation: TX " + std::to_string(xid) +
                " is in shrinking phase and cannot acquire new locks");
        }
    }

    LockQueue& lq = getQueue(resource_key);
    std::unique_lock<std::mutex> ul(lq.mu);

    // Check if we already hold a compatible or stronger lock
    for (auto& r : lq.requests) {
        if (r.xid == xid && r.granted) {
            // Upgrade: if we already hold EXCLUSIVE, SHARED is already covered.
            // If we hold SHARED and want EXCLUSIVE, we need to re-queue (handled below).
            if (mode == LockMode::SHARED) return;
            if (r.mode == LockMode::EXCLUSIVE) return;
            // Shared → Exclusive upgrade: update in place if no other holders
        }
    }

    // Add our request to the queue
    lq.requests.push_back({xid, mode, false});
    LockRequest& my_req = lq.requests.back();

    while (true) {
        // Try to grant: check if any earlier GRANTED request conflicts with ours
        bool conflict = false;
        std::unordered_set<TxID> blocking;

        for (auto& r : lq.requests) {
            if (&r == &my_req) break;   // only look at requests before ours
            if (!r.granted) continue;

            // Conflict if either side is EXCLUSIVE
            if (mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE) {
                if (r.xid != xid) {
                    conflict = true;
                    blocking.insert(r.xid);
                }
            }
        }

        if (!conflict) {
            my_req.granted = true;
            // Clear waits-for entry since we are no longer waiting
            {
                std::lock_guard<std::mutex> wf_lk(wf_mutex_);
                waits_for_.erase(xid);
            }
            return;
        }

        // Update waits-for graph and check for cycle
        {
            std::lock_guard<std::mutex> wf_lk(wf_mutex_);
            waits_for_[xid] = blocking;

            if (hasCycle(xid, waits_for_)) {
                // Deadlock — remove our request and throw
                waits_for_.erase(xid);
                lq.requests.remove_if([&](const LockRequest& r) {
                    return r.xid == xid && !r.granted;
                });
                throw DeadlockException(xid);
            }
        }

        // Wait for a signal from a lock release
        lq.cv.wait(ul);
    }
}

// ─── releaseAll ───────────────────────────────────────────────────────────────

void LockManager::releaseAll(TxID xid) {
    beginShrinking(xid);

    std::lock_guard<std::mutex> lt_lk(lt_mutex_);
    for (auto& [key, queue_ptr] : lock_table_) {
        LockQueue& lq = *queue_ptr;
        std::unique_lock<std::mutex> ul(lq.mu);
        lq.requests.remove_if([&](const LockRequest& r) { return r.xid == xid; });
        lq.cv.notify_all();  // wake any waiters — they may now be unblocked
    }

    std::lock_guard<std::mutex> wf_lk(wf_mutex_);
    waits_for_.erase(xid);
}

// ─── beginShrinking ───────────────────────────────────────────────────────────

void LockManager::beginShrinking(TxID xid) {
    std::lock_guard<std::mutex> sk(shrink_mutex_);
    shrinking_.insert(xid);
}

// ─── hasCycle ─────────────────────────────────────────────────────────────────
// DFS on the waits-for graph starting from start.
// Returns true if a cycle is found (i.e., deadlock).

bool LockManager::hasCycle(TxID start,
    const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) const
{
    std::unordered_set<TxID> visited, on_stack;

    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        visited.insert(node);
        on_stack.insert(node);

        auto it = graph.find(node);
        if (it != graph.end()) {
            for (TxID neighbor : it->second) {
                if (!visited.count(neighbor)) {
                    if (dfs(neighbor)) return true;
                } else if (on_stack.count(neighbor)) {
                    return true;  // back edge = cycle
                }
            }
        }

        on_stack.erase(node);
        return false;
    };

    return dfs(start);
}

// ─── printLockTable ───────────────────────────────────────────────────────────

void LockManager::printLockTable() const {
    std::lock_guard<std::mutex> lt_lk(const_cast<std::mutex&>(lt_mutex_));
    std::cout << "=== Lock Table ===\n";
    for (auto& [key, queue_ptr] : lock_table_) {
        std::cout << "  Resource '" << key << "':\n";
        for (auto& r : queue_ptr->requests) {
            std::cout << "    TX " << r.xid
                      << " mode=" << (r.mode == LockMode::SHARED ? "S" : "X")
                      << " granted=" << (r.granted ? "yes" : "no") << "\n";
        }
    }
    std::cout << "==================\n";
}
