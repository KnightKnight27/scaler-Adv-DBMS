#include "lock_manager.h"
#include <functional>

bool LockManager::has_cycle(TxID start) const {
    std::unordered_set<TxID> visited, stack;
    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        visited.insert(node);
        stack.insert(node);
        auto it = waits_for_.find(node);
        if (it != waits_for_.end()) {
            for (TxID nb : it->second) {
                if (!visited.count(nb) && dfs(nb)) return true;
                if (stack.count(nb)) return true;
            }
        }
        stack.erase(node);
        return false;
    };
    return dfs(start);
}

void LockManager::acquire(const RowKey& key, TxID xid, LockMode mode) {
    if (registry_.in_shrinking(xid))
        throw std::runtime_error("2PL violation: cannot acquire lock in shrinking phase");

    LockQueue* lq_ptr;
    {
        std::lock_guard<std::mutex> lk(mu_);
        lq_ptr = &lock_table_[key];
    }
    LockQueue& lq = *lq_ptr;

    std::unique_lock<std::mutex> ul(lq.mu);

    // Already hold a strong-enough lock? Then nothing to do.
    for (const auto& r : lq.requests) {
        if (r.xid == xid && r.granted) {
            if (mode == LockMode::SHARED) return;
            if (r.mode == LockMode::EXCLUSIVE) return;
        }
    }

    lq.requests.push_back({xid, mode, false});
    auto& my_req = lq.requests.back();

    while (true) {
        bool conflict = false;
        std::unordered_set<TxID> blocking;
        for (auto& r : lq.requests) {
            if (&r == &my_req) break;            // only earlier requests can block us
            if (!r.granted) continue;
            if (mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE) {
                if (r.xid != xid) { conflict = true; blocking.insert(r.xid); }
            }
        }

        if (!conflict) {
            my_req.granted = true;
            std::lock_guard<std::mutex> lk(mu_);
            waits_for_.erase(xid);
            return;
        }

        {
            std::lock_guard<std::mutex> lk(mu_);
            waits_for_[xid] = blocking;
            if (has_cycle(xid)) {
                waits_for_.erase(xid);
                lq.requests.remove_if(
                    [&](const LockRequest& r) { return r.xid == xid && !r.granted; });
                throw DeadlockException(xid);
            }
        }
        lq.cv.wait(ul);
    }
}

void LockManager::release(TxID xid) {
    registry_.enter_shrinking(xid);

    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [key, lq] : lock_table_) {
        std::unique_lock<std::mutex> ul(lq.mu);
        lq.requests.remove_if([&](const LockRequest& r) { return r.xid == xid; });
        lq.cv.notify_all();
    }
    waits_for_.erase(xid);
}
