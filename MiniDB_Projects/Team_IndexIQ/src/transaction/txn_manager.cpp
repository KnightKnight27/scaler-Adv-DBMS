#include "transaction/txn_manager.h"
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <functional>
#include <stdexcept>
#include <iostream>

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct TxEntry {
    TxID     id;
    TxID     snapshot;
    TxStatus status      = TxStatus::ACTIVE;
    bool     shrinking   = false;
};

static std::atomic<TxID>                         g_next_xid{1};
static std::mutex                                g_tx_mu;
static std::unordered_map<TxID, TxEntry>         g_transactions;

static bool tx_committed(TxID xid) {
    std::lock_guard lk(g_tx_mu);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::COMMITTED;
}

struct Version {
    std::string value;
    TxID        xmin;
    TxID        xmax = 0;
};

static std::mutex                                          g_heap_mu;
static std::unordered_map<std::string, std::list<Version>> g_heap;

static bool visible(const Version& v, TxID snap, TxID reader) {
    bool xmin_ok = (v.xmin == reader) ||
                   (tx_committed(v.xmin) && v.xmin < snap);
    if (!xmin_ok) return false;
    if (v.xmax == 0) return true;
    return !(v.xmax == reader ||
             (tx_committed(v.xmax) && v.xmax < snap));
}

struct LockReq {
    TxID     xid;
    LockMode mode;
    bool     granted = false;
};

struct LockQueue {
    std::list<LockReq>      reqs;
    std::mutex              mu;
    std::condition_variable cv;
};

static std::mutex                                               g_lm_mu;
static std::unordered_map<std::string, LockQueue>              g_locks;
static std::unordered_map<TxID, std::unordered_set<TxID>>      g_waits;

static bool has_cycle(TxID start) {
    std::unordered_set<TxID> visited, stack;
    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        visited.insert(node); stack.insert(node);
        auto it = g_waits.find(node);
        if (it != g_waits.end()) {
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

TxID TransactionManager::begin() {
    std::lock_guard lk(g_tx_mu);
    TxID xid = g_next_xid.fetch_add(1);
    g_transactions[xid] = {xid, xid, TxStatus::ACTIVE, false};
    return xid;
}

void TransactionManager::acquire_lock(const std::string& res, TxID xid, LockMode mode) {
    {
        std::lock_guard lk(g_tx_mu);
        if (g_transactions.at(xid).shrinking)
            throw std::runtime_error("2PL: cannot acquire lock in shrinking phase");
    }

    LockQueue& lq = g_locks[res];
    std::unique_lock ul(lq.mu);

    for (auto& r : lq.reqs) {
        if (r.xid == xid && r.granted) {
            if (mode == LockMode::SHARED || r.mode == LockMode::EXCLUSIVE) return;
        }
    }

    lq.reqs.push_back({xid, mode, false});
    auto& my_req = lq.reqs.back();

    while (true) {
        bool conflict = false;
        std::unordered_set<TxID> blocking;
        for (auto& r : lq.reqs) {
            if (&r == &my_req) break;
            if (!r.granted) continue;
            if ((mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE)
                && r.xid != xid) {
                conflict = true;
                blocking.insert(r.xid);
            }
        }

        if (!conflict) {
            my_req.granted = true;
            { std::lock_guard lk(g_lm_mu); g_waits.erase(xid); }
            return;
        }

        {
            std::lock_guard lk(g_lm_mu);
            g_waits[xid] = blocking;
            if (has_cycle(xid)) {
                g_waits.erase(xid);
                lq.reqs.remove_if([&](const LockReq& r){ return r.xid == xid && !r.granted; });
                throw DeadlockException(xid);
            }
        }

        lq.cv.wait(ul);
    }
}

void TransactionManager::release_locks(TxID xid) {
    { std::lock_guard lk(g_tx_mu);
      if (g_transactions.count(xid)) g_transactions.at(xid).shrinking = true; }

    for (auto& [res, lq] : g_locks) {
        std::unique_lock ul(lq.mu);
        lq.reqs.remove_if([&](const LockReq& r){ return r.xid == xid; });
        lq.cv.notify_all();
    }

    { std::lock_guard lk(g_lm_mu); g_waits.erase(xid); }
}

void TransactionManager::commit(TxID xid) {
    { std::lock_guard lk(g_tx_mu);
      g_transactions.at(xid).status = TxStatus::COMMITTED; }
    release_locks(xid);
}

void TransactionManager::abort(TxID xid) {
    { std::lock_guard lk(g_heap_mu);
      for (auto& [k, chain] : g_heap) {
          for (auto& v : chain) {
              if (v.xmin == xid) v.xmax = xid;
              if (v.xmax == xid && v.xmin != xid) v.xmax = 0;
          }
      } }
    { std::lock_guard lk(g_tx_mu);
      g_transactions.at(xid).status = TxStatus::ABORTED; }
    release_locks(xid);
}

void TransactionManager::mvcc_write(TxID xid, const std::string& key, const std::string& value) {
    acquire_lock(key, xid, LockMode::EXCLUSIVE);
    std::lock_guard lk(g_heap_mu);
    TxID snap;
    { std::lock_guard tlk(g_tx_mu); snap = g_transactions.at(xid).snapshot; }
    auto it = g_heap.find(key);
    if (it != g_heap.end()) {
        for (auto& v : it->second)
            if (visible(v, snap, xid) && v.xmax == 0) { v.xmax = xid; break; }
    }
    g_heap[key].push_front({value, xid, 0});
}

void TransactionManager::mvcc_delete(TxID xid, const std::string& key) {
    acquire_lock(key, xid, LockMode::EXCLUSIVE);
    std::lock_guard lk(g_heap_mu);
    TxID snap;
    { std::lock_guard tlk(g_tx_mu); snap = g_transactions.at(xid).snapshot; }
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return;
    for (auto& v : it->second)
        if (visible(v, snap, xid) && v.xmax == 0) { v.xmax = xid; return; }
}

std::optional<std::string> TransactionManager::mvcc_read(TxID xid, const std::string& key) {
    std::lock_guard lk(g_heap_mu);
    TxID snap;
    { std::lock_guard tlk(g_tx_mu); snap = g_transactions.at(xid).snapshot; }
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return std::nullopt;
    for (auto& v : it->second)
        if (visible(v, snap, xid)) return v.value;
    return std::nullopt;
}
