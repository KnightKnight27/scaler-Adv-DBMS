#include "transaction/lock_manager.h"
#include <iostream>

LockManager::~LockManager() {
    std::lock_guard<std::mutex> lt_lk(lt_mutex_);
    for (auto& [key, queue_ptr] : lock_table_) {
        delete queue_ptr;
    }
}

LockManager::LockQueue& LockManager::getQueue(const std::string& key) {
    std::lock_guard<std::mutex> lt_lk(lt_mutex_);
    auto it = lock_table_.find(key);
    if (it == lock_table_.end()) {
        lock_table_[key] = new LockQueue();
    }
    return *lock_table_[key];
}

void LockManager::acquireLock(TxID xid, const std::string& resource_key, LockMode mode) {
    {
        std::lock_guard<std::mutex> sk(shrink_mutex_);
        if (shrinking_.find(xid) != shrinking_.end()) {
            throw std::runtime_error("2PL violation: TX " + std::to_string(xid) +
                                     " is in shrinking phase and cannot acquire new locks");
        }
    }

    LockQueue& lq = getQueue(resource_key);
    std::unique_lock<std::mutex> ul(lq.mu);

    for (const auto& r : lq.requests) {
        if (r.xid == xid && r.granted) {
            if (mode == LockMode::SHARED || r.mode == LockMode::EXCLUSIVE) {
                return;
            }
        }
    }

    lq.requests.push_back({xid, mode, false});
    LockRequest& my_req = lq.requests.back();

    while (true) {
        bool conflict = false;
        std::unordered_set<TxID> blocking;

        for (const auto& r : lq.requests) {
            if (&r == &my_req) {
                break;
            }
            if (!r.granted) {
                continue;
            }

            if (mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE) {
                if (r.xid != xid) {
                    conflict = true;
                    blocking.insert(r.xid);
                }
            }
        }

        if (!conflict) {
            my_req.granted = true;
            {
                std::lock_guard<std::mutex> wf_lk(wf_mutex_);
                waits_for_.erase(xid);
            }
            return;
        }

        {
            std::lock_guard<std::mutex> wf_lk(wf_mutex_);
            waits_for_[xid] = blocking;

            if (hasCycle(xid, waits_for_)) {
                waits_for_.erase(xid);
                lq.requests.remove_if([&](const LockRequest& r) {
                    return r.xid == xid && !r.granted;
                });
                throw DeadlockException(xid);
            }
        }

        lq.cv.wait(ul);
    }
}

void LockManager::releaseAll(TxID xid) {
    beginShrinking(xid);

    std::lock_guard<std::mutex> lt_lk(lt_mutex_);
    for (auto& [key, queue_ptr] : lock_table_) {
        LockQueue& lq = *queue_ptr;
        std::unique_lock<std::mutex> ul(lq.mu);
        lq.requests.remove_if([&](const LockRequest& r) { return r.xid == xid; });
        lq.cv.notify_all();
    }

    std::lock_guard<std::mutex> wf_lk(wf_mutex_);
    waits_for_.erase(xid);
}

void LockManager::beginShrinking(TxID xid) {
    std::lock_guard<std::mutex> sk(shrink_mutex_);
    shrinking_.insert(xid);
}

bool LockManager::hasCycle(TxID start,
                           const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) const {
    std::unordered_set<TxID> visited;
    std::unordered_set<TxID> on_stack;

    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        visited.insert(node);
        on_stack.insert(node);

        const auto it = graph.find(node);
        if (it != graph.end()) {
            for (const TxID neighbor : it->second) {
                if (visited.find(neighbor) == visited.end()) {
                    if (dfs(neighbor)) {
                        return true;
                    }
                } else if (on_stack.find(neighbor) != on_stack.end()) {
                    return true;
                }
            }
        }

        on_stack.erase(node);
        return false;
    };

    return dfs(start);
}

void LockManager::printLockTable() const {
    std::lock_guard<std::mutex> lt_lk(lt_mutex_);
    std::cout << "=== Lock Table ===\n";
    for (const auto& [key, queue_ptr] : lock_table_) {
        std::cout << "  Resource '" << key << "':\n";
        for (const auto& r : queue_ptr->requests) {
            std::cout << "    TX " << r.xid
                      << " mode=" << (r.mode == LockMode::SHARED ? "S" : "X")
                      << " granted=" << (r.granted ? "yes" : "no") << "\n";
        }
    }
    std::cout << "==================\n";
}
