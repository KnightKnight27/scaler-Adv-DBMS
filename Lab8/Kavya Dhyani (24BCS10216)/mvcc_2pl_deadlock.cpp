#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <thread>
#include <stdexcept>
#include <optional>
#include <atomic>
#include <string>
#include <chrono>
#include <functional>

// ─────────────────────────────────────────────
// Types
// ─────────────────────────────────────────────

using TxID = uint64_t;
using RowKey = std::string;

// ─────────────────────────────────────────────
// Transaction Manager State
// ─────────────────────────────────────────────

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID id;
    TxID snapshot_xid;
    TxStatus status = TxStatus::ACTIVE;
    bool in_shrinking = false;
};

static std::atomic<TxID> g_next_xid{1};
static std::mutex g_tx_mutex;
static std::unordered_map<TxID, Transaction> g_transactions;

TxID begin_transaction() {
    std::lock_guard lk(g_tx_mutex);
    TxID xid = g_next_xid.fetch_add(1);
    g_transactions[xid] = {xid, xid, TxStatus::ACTIVE, false};
    return xid;
}

bool is_committed(TxID xid) {
    std::lock_guard lk(g_tx_mutex);
    return g_transactions.count(xid) &&
           g_transactions[xid].status == TxStatus::COMMITTED;
}

bool is_aborted(TxID xid) {
    std::lock_guard lk(g_tx_mutex);
    return g_transactions.count(xid) &&
           g_transactions[xid].status == TxStatus::ABORTED;
}

// ─────────────────────────────────────────────
// MVCC
// ─────────────────────────────────────────────

struct RowVersion {
    std::string value;
    TxID xmin;
    TxID xmax;
};

static std::mutex g_heap_mutex;
static std::unordered_map<RowKey, std::list<RowVersion>> g_heap;

bool is_visible(const RowVersion& v, TxID snapshot, TxID reader) {
    bool xmin_ok =
        (v.xmin == reader) ||
        (is_committed(v.xmin) && v.xmin < snapshot);

    if (!xmin_ok) return false;

    if (v.xmax == 0) return true;

    bool xmax_hidden =
        (v.xmax == reader) ||
        (is_committed(v.xmax) && v.xmax < snapshot);

    return !xmax_hidden;
}

std::optional<std::string> mvcc_read_key(const RowKey& key, TxID xid) {
    std::lock_guard lk(g_heap_mutex);

    TxID snap;
    {
        std::lock_guard tlk(g_tx_mutex);
        snap = g_transactions[xid].snapshot_xid;
    }

    if (!g_heap.count(key)) return std::nullopt;

    for (auto& v : g_heap[key]) {
        if (is_visible(v, snap, xid))
            return v.value;
    }
    return std::nullopt;
}

void mvcc_insert(const RowKey& key, const std::string& value, TxID xid) {
    std::lock_guard lk(g_heap_mutex);
    g_heap[key].push_front({value, xid, 0});
}

void mvcc_update(const RowKey& key, const std::string& value, TxID xid) {
    std::lock_guard lk(g_heap_mutex);

    TxID snap;
    {
        std::lock_guard tlk(g_tx_mutex);
        snap = g_transactions[xid].snapshot_xid;
    }

    if (g_heap.count(key)) {
        for (auto& v : g_heap[key]) {
            if (is_visible(v, snap, xid) && v.xmax == 0) {
                v.xmax = xid;
                break;
            }
        }
    }

    g_heap[key].push_front({value, xid, 0});
}

void mvcc_delete(const RowKey& key, TxID xid) {
    std::lock_guard lk(g_heap_mutex);

    TxID snap;
    {
        std::lock_guard tlk(g_tx_mutex);
        snap = g_transactions[xid].snapshot_xid;
    }

    if (!g_heap.count(key)) return;

    for (auto& v : g_heap[key]) {
        if (is_visible(v, snap, xid) && v.xmax == 0) {
            v.xmax = xid;
            return;
        }
    }
}

// ─────────────────────────────────────────────
// Lock Manager (Strict 2PL + Deadlock Detection)
// ─────────────────────────────────────────────

enum class LockMode { SHARED, EXCLUSIVE };

struct LockRequest {
    TxID xid;
    LockMode mode;
    bool granted = false;
};

struct LockQueue {
    std::list<LockRequest> requests;
    std::mutex mu;
    std::condition_variable cv;
};

static std::mutex g_lm_mutex;
static std::unordered_map<RowKey, LockQueue> g_lock_table;
static std::unordered_map<TxID, std::unordered_set<TxID>> g_waits_for;

class DeadlockException : public std::runtime_error {
public:
    DeadlockException(TxID xid)
        : std::runtime_error("Deadlock detected in TX " + std::to_string(xid)) {}
};

bool has_cycle_util(
    TxID node,
    std::unordered_map<TxID, std::unordered_set<TxID>>& graph,
    std::unordered_set<TxID>& vis,
    std::unordered_set<TxID>& stack) {

    vis.insert(node);
    stack.insert(node);

    for (auto nb : graph[node]) {
        if (!vis.count(nb) && has_cycle_util(nb, graph, vis, stack))
            return true;
        if (stack.count(nb))
            return true;
    }

    stack.erase(node);
    return false;
}

bool has_cycle(TxID start,
               std::unordered_map<TxID, std::unordered_set<TxID>>& graph) {
    std::unordered_set<TxID> vis, stack;
    return has_cycle_util(start, graph, vis, stack);
}

void acquire_lock(const RowKey& key, TxID xid, LockMode mode) {
    LockQueue& q = g_lock_table[key];
    std::unique_lock ul(q.mu);

    q.requests.push_back({xid, mode, false});
    auto it = std::prev(q.requests.end());

    while (true) {
        bool conflict = false;
        std::unordered_set<TxID> blocking;

        for (auto& r : q.requests) {
            if (&r == &(*it)) break;
            if (!r.granted) continue;

            if (mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE) {
                conflict = true;
                blocking.insert(r.xid);
            }
        }

        if (!conflict) {
            it->granted = true;

            std::lock_guard lk(g_lm_mutex);
            g_waits_for.erase(xid);

            return;
        }

        {
            std::lock_guard lk(g_lm_mutex);
            g_waits_for[xid] = blocking;

            if (has_cycle(xid, g_waits_for)) {
                q.requests.erase(it);
                throw DeadlockException(xid);
            }
        }

        q.cv.wait(ul);
    }
}

void release_locks(TxID xid) {
    {
        std::lock_guard lk(g_tx_mutex);
        g_transactions[xid].in_shrinking = true;
    }

    for (auto& [k, q] : g_lock_table) {
        std::lock_guard lk(q.mu);
        q.requests.remove_if([&](auto& r){ return r.xid == xid; });
        q.cv.notify_all();
    }

    std::lock_guard lk(g_lm_mutex);
    g_waits_for.erase(xid);
}

// ─────────────────────────────────────────────
// Transaction API
// ─────────────────────────────────────────────

class TransactionManager {
public:
    TxID begin() {
        return begin_transaction();
    }

    std::optional<std::string> read(TxID xid, const RowKey& key) {
        acquire_lock(key, xid, LockMode::SHARED);
        return mvcc_read_key(key, xid);
    }

    void insert(TxID xid, const RowKey& key, const std::string& value) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_insert(key, value, xid);
    }

    void update(TxID xid, const RowKey& key, const std::string& value) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_update(key, value, xid);
    }

    void remove(TxID xid, const RowKey& key) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_delete(key, xid);
    }

    void commit(TxID xid) {
        {
            std::lock_guard lk(g_tx_mutex);
            g_transactions[xid].status = TxStatus::COMMITTED;
        }
        release_locks(xid);
        std::cout << "[TX " << xid << "] COMMITTED\n";
    }

    void abort(TxID xid) {
        {
            std::lock_guard lk(g_heap_mutex);
            for (auto& [k, chain] : g_heap) {
                for (auto& v : chain) {
                    if (v.xmin == xid) v.xmax = xid;
                    if (v.xmax == xid) v.xmax = 0;
                }
            }
        }

        {
            std::lock_guard lk(g_tx_mutex);
            g_transactions[xid].status = TxStatus::ABORTED;
        }

        release_locks(xid);
        std::cout << "[TX " << xid << "] ABORTED\n";
    }
};

// ─────────────────────────────────────────────
// Demo
// ─────────────────────────────────────────────

void print_val(std::optional<std::string> v, TxID xid, const RowKey& key) {
    std::cout << "[TX " << xid << "] " << key << " = "
              << (v ? *v : "<not visible>") << "\n";
}

int main() {
    TransactionManager tm;

    std::cout << "=== MVCC TEST ===\n";

    TxID t1 = tm.begin();
    tm.insert(t1, "balance", "1000");
    tm.commit(t1);

    TxID t2 = tm.begin();
    TxID t3 = tm.begin();

    tm.update(t3, "balance", "2000");
    tm.commit(t3);

    print_val(tm.read(t2, "balance"), t2, "balance");
    tm.commit(t2);

    std::cout << "\n=== LOCK TEST ===\n";

    TxID t4 = tm.begin();
    TxID t5 = tm.begin();

    print_val(tm.read(t4, "balance"), t4, "balance");
    print_val(tm.read(t5, "balance"), t5, "balance");

    tm.commit(t4);
    tm.commit(t5);

    std::cout << "\n=== DONE ===\n";
}