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
#include <utility>
#include <atomic>
#include <sstream>
#include <cassert>
#include <functional>
#include <chrono>

// ─────────────────────────────────────────────
// 1. Transaction state
// ─────────────────────────────────────────────

using TxID   = uint64_t;
using RowKey = std::string;

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
    g_transactions[xid] = Transaction{xid, xid, TxStatus::ACTIVE, false};
    return xid;
}

bool is_committed(TxID xid) {
    std::lock_guard lk(g_tx_mutex);
    return g_transactions.count(xid) && g_transactions[xid].status == TxStatus::COMMITTED;
}

bool is_aborted(TxID xid) {
    std::lock_guard lk(g_tx_mutex);
    return g_transactions.count(xid) && g_transactions[xid].status == TxStatus::ABORTED;
}

// ─────────────────────────────────────────────
// 2. MVCC
// ─────────────────────────────────────────────

struct RowVersion {
    std::string value;
    TxID xmin;
    TxID xmax;
};

static std::mutex g_heap_mutex;
static std::unordered_map<RowKey, std::list<RowVersion>> g_heap;

bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) {
    bool xmin_ok =
        (v.xmin == reader_xid) ||
        (is_committed(v.xmin) && v.xmin < snapshot_xid);

    if (!xmin_ok) return false;

    if (v.xmax == 0) return true;

    bool xmax_block =
        (v.xmax != reader_xid) &&
        is_committed(v.xmax) &&
        v.xmax < snapshot_xid;

    return !xmax_block;
}

std::pair<bool, std::string> mvcc_read_key(const RowKey& key, TxID xid) {
    std::lock_guard lk(g_heap_mutex);

    TxID snap;
    {
        std::lock_guard tlk(g_tx_mutex);
        snap = g_transactions[xid].snapshot_xid;
    }

    auto it = g_heap.find(key);
    if (it == g_heap.end()) return {false, ""};

    for (auto& v : it->second) {
        if (is_visible(v, snap, xid))
            return {true, v.value};
    }
    return {false, ""};
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

    auto& chain = g_heap[key];

    for (auto& v : chain) {
        if (v.xmax == 0 && is_committed(v.xmin)) {
            v.xmax = xid;
            break;
        }
    }

    chain.push_front({value, xid, 0});
}

void mvcc_delete(const RowKey& key, TxID xid) {
    std::lock_guard lk(g_heap_mutex);

    TxID snap;
    {
        std::lock_guard tlk(g_tx_mutex);
        snap = g_transactions[xid].snapshot_xid;
    }

    auto it = g_heap.find(key);
    if (it == g_heap.end()) return;

    for (auto& v : it->second) {
        if (v.xmax == 0 && is_committed(v.xmin)) {
            v.xmax = xid;
            return;
        }
    }
}

// ─────────────────────────────────────────────
// 3. Lock Manager (Strict 2PL)
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
        : std::runtime_error("Deadlock detected, aborting tx " + std::to_string(xid)) {}
};

bool has_cycle(TxID start,
    const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) {

    std::unordered_set<TxID> visited, stack;

    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        visited.insert(node);
        stack.insert(node);

        auto it = graph.find(node);
        if (it != graph.end()) {
            for (auto nb : it->second) {
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
        std::lock_guard lk(g_tx_mutex);
        if (g_transactions[xid].in_shrinking)
            throw std::runtime_error("2PL violation");
    }

    LockQueue& q = g_lock_table[key];
    std::unique_lock ul(q.mu);

    q.requests.push_back({xid, mode, false});
    auto it_req = std::prev(q.requests.end());

    while (true) {
        bool conflict = false;
        std::unordered_set<TxID> blockers;

        for (auto it = q.requests.begin(); it != it_req; ++it) {
            if (it->granted &&
                (mode == LockMode::EXCLUSIVE || it->mode == LockMode::EXCLUSIVE)) {
                conflict = true;
                blockers.insert(it->xid);
            }
        }

        if (!conflict) {
            it_req->granted = true;

            std::lock_guard lk(g_lm_mutex);
            g_waits_for.erase(xid);

            return;
        }

        {
            std::lock_guard lk(g_lm_mutex);
            g_waits_for[xid] = blockers;

            if (has_cycle(xid, g_waits_for)) {
                g_waits_for.erase(xid);
                q.requests.remove_if([&](const LockRequest& r) {
                    return r.xid == xid && !r.granted;
                });
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
        q.requests.remove_if([&](const LockRequest& r) {
            return r.xid == xid;
        });
        q.cv.notify_all();
    }

    std::lock_guard lk(g_lm_mutex);
    g_waits_for.erase(xid);
}

// ─────────────────────────────────────────────
// 4. Transaction Manager
// ─────────────────────────────────────────────

class TransactionManager {
public:
    TxID begin() {
        return begin_transaction();
    }

    std::pair<bool, std::string> read(TxID xid, const RowKey& key) {
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
// 5. Demo
// ─────────────────────────────────────────────

void print(const std::pair<bool, std::string>& v, TxID xid) {
    std::cout << "[TX " << xid << "] READ = "
              << (v.first ? v.second : "<null>") << "\n";
}

int main() {
    TransactionManager tm;

    TxID t1 = tm.begin();
    tm.insert(t1, "x", "100");
    tm.commit(t1);

    TxID t2 = tm.begin();
    TxID t3 = tm.begin();

    tm.update(t3, "x", "200");
    tm.commit(t3);

    print(tm.read(t2, "x"), t2);
    tm.commit(t2);

    std::cout << "Done\n";
    return 0;
}