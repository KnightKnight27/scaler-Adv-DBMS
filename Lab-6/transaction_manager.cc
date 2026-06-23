// Lab 6 - Transaction Manager: MVCC + Two-Phase Locking (implementation)

#include "transaction_manager.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <list>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lab6 {

namespace {

// ───────────────────────────── transactions ─────────────────────────────

enum class TxStatus { kActive, kCommitted, kAborted };

struct Transaction {
    TxId     id;
    TxId     snapshot;        // sees commits with xid < snapshot
    TxStatus status = TxStatus::kActive;
    bool     shrinking = false;   // 2PL: true once it starts releasing locks
};

std::atomic<TxId>                       g_next_xid{1};
std::mutex                              g_tx_mutex;
std::unordered_map<TxId, Transaction>   g_transactions;

bool is_committed(TxId xid) {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::kCommitted;
}

TxId snapshot_of(TxId xid) {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    return g_transactions.at(xid).snapshot;
}

// ───────────────────────────── MVCC heap ─────────────────────────────

struct Version {
    std::string value;
    TxId        xmin;       // transaction that created this version
    TxId        xmax;       // transaction that deleted it (0 = still live)
};

std::mutex                                           g_heap_mutex;
// Each row key maps to a chain of versions, newest at the front.
std::unordered_map<RowKey, std::list<Version>>       g_heap;

// Is version v visible to a reader with the given snapshot?
bool is_visible(const Version& v, TxId snapshot, TxId reader) {
    // The creating transaction must be us, or committed before our snapshot.
    bool created_visible = (v.xmin == reader) ||
                           (is_committed(v.xmin) && v.xmin < snapshot);
    if (!created_visible) return false;

    // If nobody has deleted it, it's visible.
    if (v.xmax == 0) return true;

    // It's hidden only if the deleter is us, or committed before our snapshot.
    bool deleted_visible = (v.xmax == reader) ||
                           (is_committed(v.xmax) && v.xmax < snapshot);
    return !deleted_visible;
}

std::optional<std::string> mvcc_read(const RowKey& key, TxId xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    TxId snap = snapshot_of(xid);
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return std::nullopt;
    for (const Version& v : it->second) {
        if (is_visible(v, snap, xid)) return v.value;
    }
    return std::nullopt;
}

void mvcc_insert(const RowKey& key, const std::string& value, TxId xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    g_heap[key].push_front(Version{value, xid, 0});
}

void mvcc_update(const RowKey& key, const std::string& value, TxId xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    TxId snap = snapshot_of(xid);
    auto it = g_heap.find(key);
    if (it != g_heap.end()) {
        for (Version& v : it->second) {
            if (v.xmax == 0 && is_visible(v, snap, xid)) {
                v.xmax = xid;     // logically delete the old version
                break;
            }
        }
    }
    g_heap[key].push_front(Version{value, xid, 0});
}

void mvcc_delete(const RowKey& key, TxId xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    TxId snap = snapshot_of(xid);
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return;
    for (Version& v : it->second) {
        if (v.xmax == 0 && is_visible(v, snap, xid)) {
            v.xmax = xid;
            return;
        }
    }
}

// Undo this transaction's writes (used on abort): hide its own inserts,
// and revive versions it had marked as deleted.
void mvcc_rollback(TxId xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    for (auto& [key, chain] : g_heap) {
        for (Version& v : chain) {
            if (v.xmin == xid) v.xmax = xid;   // own insert -> make invisible
            else if (v.xmax == xid) v.xmax = 0;  // own delete -> undo it
        }
    }
}

// ───────────────────────────── lock manager ─────────────────────────────

struct LockRequest {
    TxId     xid;
    LockMode mode;
    bool     granted;
};

struct LockQueue {
    std::list<LockRequest>  requests;
    std::condition_variable cv;
};

std::mutex                                   g_lock_mutex;   // guards the whole table + waits-for
std::unordered_map<RowKey, LockQueue>        g_lock_table;
std::unordered_map<TxId, std::unordered_set<TxId>> g_waits_for;  // xid -> who it waits on

// DFS cycle check on the waits-for graph starting from `start`.
bool has_cycle(TxId start) {
    std::unordered_set<TxId> on_stack;
    std::function<bool(TxId)> dfs = [&](TxId node) -> bool {
        if (on_stack.count(node)) return true;
        on_stack.insert(node);
        auto it = g_waits_for.find(node);
        if (it != g_waits_for.end()) {
            for (TxId next : it->second) {
                if (dfs(next)) return true;
            }
        }
        on_stack.erase(node);
        return false;
    };
    return dfs(start);
}

// Does a granted request held by `holder` (mode hm) conflict with a new
// request of mode `want` from a different transaction?
bool conflicts(LockMode hm, LockMode want) {
    return hm == LockMode::kExclusive || want == LockMode::kExclusive;
}

void acquire_lock(const RowKey& key, TxId xid, LockMode mode) {
    // 2PL: no new locks once we've started releasing.
    {
        std::lock_guard<std::mutex> lk(g_tx_mutex);
        if (g_transactions.at(xid).shrinking) {
            throw std::runtime_error("2PL violation: lock requested in shrinking phase");
        }
    }

    std::unique_lock<std::mutex> lk(g_lock_mutex);
    LockQueue& q = g_lock_table[key];

    // Already hold something on this key? (shared can sit under exclusive)
    for (LockRequest& r : q.requests) {
        if (r.xid == xid && r.granted) {
            if (mode == LockMode::kShared) return;                 // already covered
            if (r.mode == LockMode::kExclusive) return;            // already exclusive
            // (shared -> exclusive upgrade is not needed by the demo)
        }
    }

    q.requests.push_back(LockRequest{xid, mode, false});

    while (true) {
        // Can we grant? Conflict only with EARLIER granted requests of
        // other transactions (FIFO keeps it fair / avoids starvation).
        std::unordered_set<TxId> blockers;
        for (LockRequest& r : q.requests) {
            if (r.xid == xid && !r.granted) break;   // reached our own request
            if (r.granted && r.xid != xid && conflicts(r.mode, mode)) {
                blockers.insert(r.xid);
            }
        }

        if (blockers.empty()) {
            for (LockRequest& r : q.requests) {
                if (r.xid == xid && !r.granted) { r.granted = true; break; }
            }
            g_waits_for.erase(xid);
            return;
        }

        // Record who we wait on; if that closes a cycle, deadlock.
        g_waits_for[xid] = blockers;
        if (has_cycle(xid)) {
            g_waits_for.erase(xid);
            q.requests.remove_if([&](const LockRequest& r) {
                return r.xid == xid && !r.granted;
            });
            throw DeadlockException(xid);
        }

        q.cv.wait(lk);   // released by some transaction's commit/abort
    }
}

void release_all_locks(TxId xid) {
    // 2PL: entering shrinking phase.
    {
        std::lock_guard<std::mutex> lk(g_tx_mutex);
        if (g_transactions.count(xid)) g_transactions.at(xid).shrinking = true;
    }
    std::lock_guard<std::mutex> lk(g_lock_mutex);
    for (auto& [key, q] : g_lock_table) {
        std::size_t before = q.requests.size();
        q.requests.remove_if([&](const LockRequest& r) { return r.xid == xid; });
        if (q.requests.size() != before) q.cv.notify_all();
    }
    g_waits_for.erase(xid);
}

}  // namespace

// ───────────────────────────── public API ─────────────────────────────

TxId TransactionManager::begin() {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    TxId xid = g_next_xid.fetch_add(1);
    // Snapshot = this xid: we see every transaction that committed with a
    // smaller id (i.e. before us). This is a simplified snapshot model.
    g_transactions[xid] = Transaction{xid, xid, TxStatus::kActive, false};
    return xid;
}

std::optional<std::string> TransactionManager::read(TxId xid, const RowKey& key) {
    acquire_lock(key, xid, LockMode::kShared);
    return mvcc_read(key, xid);
}

void TransactionManager::insert(TxId xid, const RowKey& key, const std::string& value) {
    acquire_lock(key, xid, LockMode::kExclusive);
    mvcc_insert(key, value, xid);
}

void TransactionManager::update(TxId xid, const RowKey& key, const std::string& value) {
    acquire_lock(key, xid, LockMode::kExclusive);
    mvcc_update(key, value, xid);
}

void TransactionManager::remove(TxId xid, const RowKey& key) {
    acquire_lock(key, xid, LockMode::kExclusive);
    mvcc_delete(key, xid);
}

void TransactionManager::commit(TxId xid) {
    {
        std::lock_guard<std::mutex> lk(g_tx_mutex);
        g_transactions.at(xid).status = TxStatus::kCommitted;
    }
    release_all_locks(xid);
    std::cout << "  [tx " << xid << "] COMMIT\n";
}

void TransactionManager::abort(TxId xid) {
    mvcc_rollback(xid);
    {
        std::lock_guard<std::mutex> lk(g_tx_mutex);
        g_transactions.at(xid).status = TxStatus::kAborted;
    }
    release_all_locks(xid);
    std::cout << "  [tx " << xid << "] ABORT\n";
}

}  // namespace lab6
