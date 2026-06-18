#include "lock_manager.h"
#include "mvcc_heap.h"
#include <functional>

static std::mutex g_lm_mutex;
static std::unordered_map<RowKey, LockQueue> g_lock_table;
static std::unordered_map<TxID, std::unordered_set<TxID>> g_waits_for;

bool has_cycle(TxID start, const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) {
    std::unordered_set<TxID> visited, stack;
    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        visited.insert(node);
        stack.insert(node);
        auto it = graph.find(node);
        if (it != graph.end()) {
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

void acquire_lock(const RowKey& key, TxID xid, LockMode mode) {
    {
        std::lock_guard<std::mutex> lk(g_tx_mutex);
        if (g_transactions.at(xid).in_shrinking)
            throw std::runtime_error("2PL violation: cannot acquire lock in shrinking phase");
    }

    g_lm_mutex.lock();
    LockQueue& lq = g_lock_table[key];
    g_lm_mutex.unlock();

    std::unique_lock<std::mutex> ul(lq.mu);

    for (auto& r : lq.requests) {
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
            if (&r == &my_req) break;
            if (!r.granted) continue;
            if (mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE) {
                if (r.xid != xid) { conflict = true; blocking.insert(r.xid); }
            }
        }

        if (!conflict) {
            my_req.granted = true;
            {
                std::lock_guard<std::mutex> lk(g_lm_mutex);
                g_waits_for.erase(xid);
            }
            return;
        }

        {
            std::lock_guard<std::mutex> lk(g_lm_mutex);
            g_waits_for[xid] = blocking;
            if (has_cycle(xid, g_waits_for)) {
                g_waits_for.erase(xid);
                lq.requests.remove_if([&](const LockRequest& r){ return r.xid == xid && !r.granted; });
                throw DeadlockException(xid);
            }
        }
        lq.cv.wait(ul);
    }
}

void release_locks(TxID xid) {
    {
        std::lock_guard<std::mutex> lk(g_tx_mutex);
        if (g_transactions.count(xid))
            g_transactions.at(xid).in_shrinking = true;
    }

    std::lock_guard<std::mutex> lm_lk(g_lm_mutex);
    for (auto& [key, lq] : g_lock_table) {
        std::unique_lock<std::mutex> ul(lq.mu);
        lq.requests.remove_if([&](const LockRequest& r){ return r.xid == xid; });
        lq.cv.notify_all();
    }

    g_waits_for.erase(xid);
}