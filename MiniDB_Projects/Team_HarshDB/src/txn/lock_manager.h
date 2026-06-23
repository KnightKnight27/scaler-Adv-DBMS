#pragma once
// ---------------------------------------------------------------------------
// lock_manager.h - Strict Two-Phase Locking with deadlock detection.
//
// Direct descendant of Lab 6, encapsulated into a class. Locks are keyed by a
// string (MiniDB locks at row granularity: "table:page:slot"). Shared locks
// coexist; an exclusive lock conflicts with everything. A transaction may only
// acquire locks during its growing phase; release_all() (called at commit/abort)
// is the instantaneous shrinking phase of Strict 2PL.
//
// Before a transaction blocks on a lock we record a waits-for edge and run a DFS
// cycle check. A cycle means deadlock, and we throw DeadlockException so the
// caller can abort and break the cycle.
// ---------------------------------------------------------------------------
#include "../common.h"
#include <string>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <stdexcept>

namespace minidb {

enum class LockMode { SHARED, EXCLUSIVE };

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxId xid)
        : std::runtime_error("deadlock detected, aborting tx " + std::to_string(xid)) {}
};

class LockManager {
public:
    static std::string rid_key(const std::string& table, const RID& rid) {
        return table + ":" + std::to_string(rid.page_id) + ":" + std::to_string(rid.slot);
    }

    void acquire(const std::string& key, TxId xid, LockMode mode) {
        LockQueue& q = queue_for(key);
        std::unique_lock<std::mutex> ul(q.mu);

        // Already hold a compatible lock?
        for (auto& r : q.reqs)
            if (r.xid == xid && r.granted) {
                if (mode == LockMode::SHARED || r.mode == LockMode::EXCLUSIVE) return;
            }

        q.reqs.push_back({xid, mode, false});
        LockRequest* mine = &q.reqs.back();

        while (true) {
            bool conflict = false;
            std::unordered_set<TxId> blockers;
            for (auto& r : q.reqs) {
                if (&r == mine) break;
                if (!r.granted || r.xid == xid) continue;
                if (mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE) {
                    conflict = true; blockers.insert(r.xid);
                }
            }
            if (!conflict) { mine->granted = true; clear_waits(xid); return; }

            {
                std::lock_guard<std::mutex> g(graph_mu_);
                waits_for_[xid] = blockers;
                if (has_cycle(xid)) {
                    waits_for_.erase(xid);
                    q.reqs.remove_if([&](const LockRequest& r){ return r.xid == xid && !r.granted; });
                    throw DeadlockException(xid);
                }
            }
            q.cv.wait(ul);
        }
    }

    void release_all(TxId xid) {
        std::lock_guard<std::mutex> g(table_mu_);
        for (auto& [key, q] : table_) {
            std::unique_lock<std::mutex> ul(q->mu);
            q->reqs.remove_if([&](const LockRequest& r){ return r.xid == xid; });
            q->cv.notify_all();
        }
        clear_waits(xid);
    }

private:
    struct LockRequest { TxId xid; LockMode mode; bool granted; };
    struct LockQueue {
        std::list<LockRequest>  reqs;
        std::mutex              mu;
        std::condition_variable cv;
    };

    LockQueue& queue_for(const std::string& key) {
        std::lock_guard<std::mutex> g(table_mu_);
        auto& slot = table_[key];
        if (!slot) slot = std::make_unique<LockQueue>();
        return *slot;
    }

    void clear_waits(TxId xid) {
        std::lock_guard<std::mutex> g(graph_mu_);
        waits_for_.erase(xid);
    }

    bool has_cycle(TxId start) {
        std::unordered_set<TxId> visited, stack;
        std::function<bool(TxId)> dfs = [&](TxId n) -> bool {
            visited.insert(n); stack.insert(n);
            auto it = waits_for_.find(n);
            if (it != waits_for_.end())
                for (TxId nb : it->second) {
                    if (stack.count(nb)) return true;
                    if (!visited.count(nb) && dfs(nb)) return true;
                }
            stack.erase(n);
            return false;
        };
        return dfs(start);
    }

    std::mutex                                                   table_mu_;
    std::unordered_map<std::string, std::unique_ptr<LockQueue>>  table_;
    std::mutex                                                   graph_mu_;
    std::unordered_map<TxId, std::unordered_set<TxId>>           waits_for_;
};

} // namespace minidb
